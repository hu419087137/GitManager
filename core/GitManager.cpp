#include "GitManager.h"
#include "LfsService.h"
#include "HostingService.h"
#include "HostingApiService.h"
#include "GitDiagnosticService.h"
#include "HookService.h"

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QPointer>
#include <QDateTime>
#include <QRegularExpression>
#include <QTextStream>

namespace Git {

namespace {

QString operationLabel(const QString& operation)
{
    if (operation.startsWith(QStringLiteral("lfs-")))
        return QStringLiteral("git lfs: %1").arg(operation.mid(4));
    return QStringLiteral("libgit2: %1").arg(operation);
}

} // namespace

GitManager::GitManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<RepositoryState>();
    qRegisterMetaType<Commit>();
    qRegisterMetaType<CommitHistoryQuery>();
    qRegisterMetaType<CommitHistoryPage>();
    qRegisterMetaType<HistoryRewritePreview>();
    qRegisterMetaType<RebasePlan>();
    qRegisterMetaType<HistoryOperationStatus>();
    qRegisterMetaType<LfsState>();
    qRegisterMetaType<QVector<HostingRemoteInfo>>();
    qRegisterMetaType<QVector<HostingChangeInfo>>();
    qRegisterMetaType<QVector<HostingIssueInfo>>();
    qRegisterMetaType<QVector<HostingReviewFile>>();
    qRegisterMetaType<GitDiagnosticReport>();
    qRegisterMetaType<HookResult>();
}

GitManager::~GitManager()
{
    if (_activeCancelFlag)
        _activeCancelFlag->store(true);
}

void GitManager::setRemoteCredentials(const QString& username,
                                      const QString& secret)
{
    _credentials.username = username;
    _credentials.secret = secret;
}

void GitManager::clearRemoteCredentials()
{
    _credentials = {};
}

void GitManager::enqueue(const QString& operation,
                         std::function<TaskResult(LibGit2Backend&)> work,
                         bool requiresRepository,
                         bool reportCompletion,
                         bool refreshAfter,
                         bool reportError,
                         bool refreshOnCancel)
{
    Task task;
    task.operation = operation;
    task.repositoryPath = _repoPath;
    task.generation = _generation;
    task.work = std::move(work);
    task.cancelFlag = std::make_shared<std::atomic_bool>(false);
    task.credentials = _credentials;
    task.requiresRepository = requiresRepository;
    task.reportCompletion = reportCompletion;
    task.refreshAfter = refreshAfter;
    task.reportError = reportError;
    task.refreshOnCancel = refreshOnCancel;
    _tasks.enqueue(std::move(task));
    startNext();
}

void GitManager::startNext()
{
    if (_busy || _tasks.isEmpty())
        return;

    _busy = true;
    const Task task = _tasks.dequeue();
    _activeCancelFlag = task.cancelFlag;
    _activeOperation = task.operation;
    emit sigBusyChanged(true);
    emit sigCommandStarted(operationLabel(task.operation));

    auto* watcher = new QFutureWatcher<TaskResult>(this);
    connect(watcher, &QFutureWatcher<TaskResult>::finished, this,
            [this, watcher, task] {
        const TaskResult result = watcher->result();
        watcher->deleteLater();

        const bool cancelled = task.cancelFlag->load();
        const bool sameRepository = task.repositoryPath == _repoPath;
        const bool current = task.generation == _generation && sameRepository;
        const bool success = !cancelled && result.error.isEmpty();
        emit sigCommandRun(operationLabel(task.operation), result.error, success);

        _busy = false;
        _activeCancelFlag.reset();
        _activeOperation.clear();

        if (current && !cancelled) {
            if (result.apply && (result.error.isEmpty() || result.applyOnError))
                result.apply(*this);

            if (!result.error.isEmpty()) {
                if (task.reportError)
                    emit sigError(result.error);
                if (task.reportCompletion)
                    emit sigOperationFinished(task.operation, false, result.error);
            } else {
                if (task.reportCompletion)
                    emit sigOperationFinished(task.operation, true, QStringLiteral("完成"));
                if (task.refreshAfter)
                    refresh();
            }
        } else if (cancelled && sameRepository && _isValid) {
            if (task.reportCompletion) {
                emit sigOperationFinished(task.operation, false,
                                          QStringLiteral("操作已取消"));
            }
            if (task.reportCompletion || task.refreshOnCancel)
                refresh();
        }

        if (!_busy) {
            if (_tasks.isEmpty())
                emit sigBusyChanged(false);
            else
                startNext();
        }
    });

    const QPointer<GitManager> receiver(this);
    watcher->setFuture(QtConcurrent::run([task, receiver]() -> TaskResult {
        LibGit2Backend backend;
        backend.setCancelFlag(task.cancelFlag.get());
        backend.setCredentials(task.credentials);
        backend.setProgressCallback([receiver](const QString& message, int percent) {
            if (!receiver)
                return;
            const QString text = percent >= 0
                ? QStringLiteral("%1 (%2%)").arg(message).arg(percent)
                : message;
            QMetaObject::invokeMethod(receiver, [receiver, text] {
                if (!receiver)
                    return;
                emit receiver->sigInfo(text);
                emit receiver->sigCommandOutput(text + QLatin1Char('\n'), false);
            }, Qt::QueuedConnection);
        });

        TaskResult result;
        if (task.cancelFlag->load())
            return result;
        if (task.requiresRepository
            && !backend.open(task.repositoryPath, &result.error))
            return result;
        result = task.work(backend);
        return result;
    }));
}

void GitManager::runBoolean(const QString& operation,
                            std::function<bool(LibGit2Backend&, QString*)> work,
                            bool refreshAfter,
                            bool refreshOnError)
{
    enqueue(operation, [work = std::move(work), refreshOnError](LibGit2Backend& backend) {
        TaskResult result;
        if (!work(backend, &result.error) && result.error.isEmpty())
            result.error = QStringLiteral("libgit2 操作失败。");
        if (refreshOnError && !result.error.isEmpty()) {
            const RepositoryState state = backend.snapshot(nullptr);
            result.applyOnError = true;
            result.apply = [state](GitManager& manager) {
                manager.publishState(state);
            };
        }
        return result;
    }, true, true, refreshAfter);
}

void GitManager::runHistoryOperation(
    const QString& operation,
    std::function<HistoryOperationResult(LibGit2Backend&, QString*)> work)
{
    enqueue(operation, [operation, work = std::move(work)](LibGit2Backend& backend) {
        TaskResult result;
        const HistoryOperationResult operationResult = work(backend, &result.error);
        if (result.error.isEmpty()) {
            result.apply = [operation, operationResult](GitManager& manager) {
                emit manager.sigHistoryOperationFinished(
                    operation, operationResult.status, operationResult.message);
            };
        } else {
            const RepositoryState state = backend.snapshot(nullptr);
            result.applyOnError = true;
            result.apply = [state](GitManager& manager) {
                manager.publishState(state);
            };
        }
        return result;
    }, true, false, true, true, true);
}

void GitManager::openRepository(const QString& path)
{
    cancelAll();
    _cache.clear();
    _repoPath = QDir(path).absolutePath();
    _isValid = false;
    ++_generation;
    const QString requestedPath = _repoPath;
    enqueue(QStringLiteral("open"), [requestedPath](LibGit2Backend& backend) {
        TaskResult result;
        const bool ok = backend.open(requestedPath, &result.error);
        const QString root = ok ? backend.rootPath() : requestedPath;
        result.applyOnError = true;
        result.apply = [ok, root, error = result.error](GitManager& manager) {
            manager._isValid = ok;
            if (ok)
                manager._repoPath = root;
            emit manager.sigRepositoryOpened(manager._repoPath, ok, error);
            if (ok)
                manager.refresh();
        };
        return result;
    }, false, false, false, false);
}

void GitManager::initRepository(const QString& path)
{
    cancelAll();
    _cache.clear();
    _repoPath = QDir(path).absolutePath();
    _isValid = false;
    ++_generation;
    const QString requestedPath = _repoPath;
    enqueue(QStringLiteral("init"), [requestedPath](LibGit2Backend& backend) {
        TaskResult result;
        const bool ok = backend.initialize(requestedPath, &result.error);
        result.applyOnError = true;
        result.apply = [ok, requestedPath, error = result.error](GitManager& manager) {
            manager._isValid = ok;
            emit manager.sigRepositoryOpened(requestedPath, ok, error);
            if (ok)
                manager.refresh();
        };
        return result;
    }, false, false, false, false);
}

void GitManager::cloneRepository(const QString& url, const QString& path)
{
    cancelAll();
    _cache.clear();
    _repoPath = QDir(path).absolutePath();
    _isValid = false;
    ++_generation;
    const QString requestedPath = _repoPath;
    enqueue(QStringLiteral("clone"), [url, requestedPath](LibGit2Backend& backend) {
        TaskResult result;
        const bool ok = backend.clone(url, requestedPath, &result.error);
        result.applyOnError = true;
        result.apply = [ok, requestedPath, error = result.error](GitManager& manager) {
            manager._isValid = ok;
            emit manager.sigRepositoryOpened(requestedPath, ok, error);
            if (ok)
                manager.refresh();
        };
        return result;
    }, false, false, false, false);
}

void GitManager::refresh()
{
    if (!_isValid)
        return;
    enqueue(QStringLiteral("refresh"), [](LibGit2Backend& backend) {
        TaskResult result;
        const RepositoryState state = backend.snapshot(&result.error);
        result.apply = [state](GitManager& manager) {
            manager._cache.storeState(manager._repoPath, state,
                                      QDateTime::currentMSecsSinceEpoch());
            manager.publishState(state);
        };
        return result;
    }, true, false, false, true, true);
}

void GitManager::refreshFromWatcher()
{
    if (!_isValid)
        return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    RepositoryState cachedState;
    if (_cache.tryGetState(_repoPath, nowMs, &cachedState)) {
        publishState(cachedState);
        return;
    }
    enqueue(QStringLiteral("refresh"), [repoPath = _repoPath, nowMs](LibGit2Backend& backend) {
        TaskResult result;
        const RepositoryState state = backend.snapshot(&result.error);
        result.apply = [state, repoPath, nowMs](GitManager& manager) {
            manager._cache.storeState(repoPath, state, nowMs);
            manager.publishState(state);
        };
        return result;
    }, true, false, false, true, true);
}

void GitManager::cancelAll()
{
    if (_activeCancelFlag)
        _activeCancelFlag->store(true);
    _tasks.clear();
    _cache.clear();
    ++_generation;
    resetCommitHistoryState();
    ++_diffRequest;
    ++_previewRequest;
}

void GitManager::setCommitHistoryQuery(const CommitHistoryQuery& query)
{
    CommitHistoryQuery normalized = query;
    normalized.searchText = normalized.searchText.trimmed();
    normalized.author = normalized.author.trimmed();
    normalized.path = normalized.path.trimmed();
    normalized.offset = 0;
    normalized.limit = 200;
    normalized.expectedRefsVersion.clear();
    if (_historyQuery.sameFilter(normalized)
        && (_historyLoaded || _historyLoading))
        return;
    _historyQuery = normalized;
    reloadCommitHistory();
}

void GitManager::reloadCommitHistory()
{
    discardQueuedHistoryTasks();
    ++_historyRequest;
    _historyRefsVersion.clear();
    _historyLoadedCount = 0;
    _historyHasMore = true;
    _historyLoaded = false;
    if (!_isValid) {
        if (_historyLoading) {
            _historyLoading = false;
            emit sigCommitHistoryLoading(false);
        }
        return;
    }
    requestCommitHistory(false);
}

void GitManager::loadMoreCommits()
{
    if (!_isValid || _historyLoading || !_historyHasMore)
        return;
    requestCommitHistory(true);
}

void GitManager::requestResetPreview(const QString& targetRevision)
{
    discardQueuedPreviewTasks();
    const quint64 requestId = ++_previewRequest;
    enqueue(QStringLiteral("preview-reset"),
            [targetRevision, requestId](LibGit2Backend& backend) {
        TaskResult result;
        const HistoryRewritePreview preview =
            backend.resetPreview(targetRevision, &result.error);
        result.apply = [preview, requestId](GitManager& manager) {
            if (requestId == manager._previewRequest)
                emit manager.sigResetPreviewReady(preview);
        };
        return result;
    }, true, false, false);
}

void GitManager::requestRebasePlan(const QString& targetRevision)
{
    discardQueuedPreviewTasks();
    const quint64 requestId = ++_previewRequest;
    enqueue(QStringLiteral("preview-rebase"),
            [targetRevision, requestId](LibGit2Backend& backend) {
        TaskResult result;
        const RebasePlan plan = backend.rebasePlan(targetRevision, &result.error);
        result.apply = [plan, requestId](GitManager& manager) {
            if (requestId == manager._previewRequest)
                emit manager.sigRebasePlanReady(plan);
        };
        return result;
    }, true, false, false);
}

void GitManager::mergeRevision(const QString& revision)
{
    runHistoryOperation(QStringLiteral("merge"),
                        [revision](auto& backend, auto* error) {
        return backend.mergeRevision(revision, error);
    });
}

void GitManager::rebaseOnto(const RebasePlan& plan, bool interactive)
{
    runHistoryOperation(interactive ? QStringLiteral("interactive-rebase")
                                    : QStringLiteral("rebase"),
                        [plan, interactive](auto& backend, auto* error) {
        return backend.rebaseOnto(plan, interactive, error);
    });
}

void GitManager::cherryPickCommit(const QString& hash, int mainline)
{
    runHistoryOperation(QStringLiteral("cherry-pick"),
                        [hash, mainline](auto& backend, auto* error) {
        return backend.cherryPickCommit(hash, mainline, error);
    });
}

void GitManager::revertCommit(const QString& hash, int mainline)
{
    runHistoryOperation(QStringLiteral("revert"),
                        [hash, mainline](auto& backend, auto* error) {
        return backend.revertCommit(hash, mainline, error);
    });
}

void GitManager::resetToCommit(const HistoryRewritePreview& preview,
                               ResetMode mode)
{
    runHistoryOperation(QStringLiteral("reset"),
                        [preview, mode](auto& backend, auto* error) {
        return backend.resetToCommit(preview, mode, error);
    });
}

void GitManager::publishState(const RepositoryState& state)
{
    const bool historyChanged = !_historyLoaded
        || state.refsVersion != _historyRefsVersion;
    const quint64 requestBeforeStateSignal = _historyRequest;
    emit sigStateReady(state);
    if (_isValid && historyChanged
        && requestBeforeStateSignal == _historyRequest) {
        reloadCommitHistory();
    }
}

void GitManager::requestCommitHistory(bool append)
{
    if (!_isValid || (append && (_historyLoading || !_historyHasMore)))
        return;

    CommitHistoryQuery request = _historyQuery;
    request.offset = append ? _historyLoadedCount : 0;
    request.limit = 200;
    request.expectedRefsVersion = append ? _historyRefsVersion : QString();
    const quint64 requestId = ++_historyRequest;
    const QString repositoryPath = _repoPath;
    _historyLoading = true;
    emit sigCommitHistoryLoading(true);

    enqueue(QStringLiteral("history"),
            [request, requestId, repositoryPath](LibGit2Backend& backend) {
        TaskResult result;
        CommitHistoryPage page;
        if (backend.open(repositoryPath, &result.error))
            page = backend.commitHistory(request, &result.error);
        const QString error = result.error;
        const bool success = error.isEmpty();
        result.applyOnError = true;
        result.apply = [page, error, success, requestId](GitManager& manager) {
            if (requestId != manager._historyRequest)
                return;
            manager._historyLoading = false;
            emit manager.sigCommitHistoryLoading(false);
            if (!success) {
                emit manager.sigError(error);
                return;
            }

            if (page.offset == 0 || page.resetRequired) {
                manager._historyLoadedCount = page.commits.size();
            } else if (page.offset == manager._historyLoadedCount) {
                manager._historyLoadedCount += page.commits.size();
            } else {
                manager.reloadCommitHistory();
                return;
            }
            manager._historyRefsVersion = page.refsVersion;
            manager._historyHasMore = page.hasMore;
            manager._historyLoaded = true;
            emit manager.sigCommitHistoryReady(page);
        };
        return result;
    }, false, false, false, false);
}

void GitManager::discardQueuedHistoryTasks()
{
    if (_activeOperation == QLatin1String("history") && _activeCancelFlag)
        _activeCancelFlag->store(true);

    QQueue<Task> remaining;
    while (!_tasks.isEmpty()) {
        Task task = _tasks.dequeue();
        if (task.operation == QLatin1String("history")) {
            task.cancelFlag->store(true);
            continue;
        }
        remaining.enqueue(std::move(task));
    }
    _tasks = std::move(remaining);
}

void GitManager::resetCommitHistoryState()
{
    ++_historyRequest;
    _historyRefsVersion.clear();
    _historyLoadedCount = 0;
    _historyHasMore = true;
    _historyLoaded = false;
    if (_historyLoading) {
        _historyLoading = false;
        emit sigCommitHistoryLoading(false);
    }
}

void GitManager::cancelDiffRequest()
{
    ++_diffRequest;
    discardQueuedDiffTasks();
}

void GitManager::discardQueuedDiffTasks()
{
    if (_activeOperation.startsWith(QLatin1String("diff-")) && _activeCancelFlag)
        _activeCancelFlag->store(true);

    QQueue<Task> remaining;
    while (!_tasks.isEmpty()) {
        Task task = _tasks.dequeue();
        if (task.operation.startsWith(QLatin1String("diff-"))) {
            task.cancelFlag->store(true);
            continue;
        }
        remaining.enqueue(std::move(task));
    }
    _tasks = std::move(remaining);
}

void GitManager::discardQueuedPreviewTasks()
{
    if (_activeOperation.startsWith(QLatin1String("preview-"))
        && _activeCancelFlag) {
        _activeCancelFlag->store(true);
    }

    QQueue<Task> remaining;
    while (!_tasks.isEmpty()) {
        Task task = _tasks.dequeue();
        if (task.operation.startsWith(QLatin1String("preview-"))) {
            task.cancelFlag->store(true);
            continue;
        }
        remaining.enqueue(std::move(task));
    }
    _tasks = std::move(remaining);
}

void GitManager::fetchFileDiff(const QString& path, bool staged, bool untracked)
{
    cancelDiffRequest();
    const quint64 requestId = _diffRequest;
    enqueue(QStringLiteral("diff-file"),
            [path, staged, untracked, requestId](LibGit2Backend& backend) {
        TaskResult result;
        const QString diff = backend.fileDiff(path, staged, untracked, &result.error);
        result.apply = [diff, path, staged, requestId](GitManager& manager) {
            if (requestId != manager._diffRequest)
                return;
            emit manager.sigDiffReady(diff, path, staged, true);
        };
        return result;
    });
}

void GitManager::fetchCommitDiff(const QString& hash)
{
    cancelDiffRequest();
    const quint64 requestId = _diffRequest;
    enqueue(QStringLiteral("diff-commit"), [hash, requestId](LibGit2Backend& backend) {
        TaskResult result;
        const QString diff = backend.commitDiff(hash, &result.error);
        result.apply = [diff, hash, requestId](GitManager& manager) {
            if (requestId != manager._diffRequest)
                return;
            emit manager.sigDiffReady(diff, hash.left(8), false, false);
        };
        return result;
    });
}

void GitManager::fetchRevisionDiff(const QString& baseRevision,
                                   const QString& targetRevision)
{
    cancelDiffRequest();
    const quint64 requestId = _diffRequest;
    enqueue(QStringLiteral("diff-compare"),
            [baseRevision, targetRevision, requestId](LibGit2Backend& backend) {
        TaskResult result;
        const QString diff = backend.revisionDiff(baseRevision, targetRevision,
                                                  &result.error);
        result.apply = [diff, baseRevision, targetRevision, requestId](GitManager& manager) {
            if (requestId != manager._diffRequest)
                return;
            emit manager.sigDiffReady(
                diff, QStringLiteral("%1..%2").arg(baseRevision, targetRevision),
                false, false);
        };
        return result;
    });
}

void GitManager::stageFile(const QString& path)
{
    runBoolean(QStringLiteral("stage"), [path](auto& backend, auto* error) {
        return backend.stage(path, error);
    });
}

void GitManager::unstageFile(const QString& path)
{
    runBoolean(QStringLiteral("unstage"), [path](auto& backend, auto* error) {
        return backend.unstage(path, error);
    });
}

void GitManager::stageAll()
{
    runBoolean(QStringLiteral("stage-all"), [](auto& backend, auto* error) {
        return backend.stageAll(error);
    });
}

void GitManager::unstageAll()
{
    runBoolean(QStringLiteral("unstage-all"), [](auto& backend, auto* error) {
        return backend.unstageAll(error);
    });
}

void GitManager::discardFile(const QString& path)
{
    runBoolean(QStringLiteral("discard"), [path](auto& backend, auto* error) {
        return backend.discard(path, error);
    });
}

void GitManager::removeUntracked(const QString& path)
{
    runBoolean(QStringLiteral("clean"), [path](auto& backend, auto* error) {
        return backend.removeUntracked(path, error);
    });
}

void GitManager::applyPatch(const QString& patch, bool cached, bool reverse)
{
    runBoolean(QStringLiteral("apply-patch"), [patch, cached, reverse](auto& backend, auto* error) {
        return backend.applyPatch(patch, cached, reverse, error);
    });
}

void GitManager::commit(const QString& message, bool amend, bool signoff)
{
    enqueue(QStringLiteral("commit"),
            [message, amend, signoff](LibGit2Backend& backend) {
        TaskResult result;
        HookService hooks(backend.rootPath());
        const HookResult preCommit = hooks.run(QStringLiteral("pre-commit"));
        if (!preCommit.success) {
            result.error = QStringLiteral("pre-commit hook failed (exit %1).\n%2")
                .arg(preCommit.exitCode).arg(preCommit.output);
            result.applyOnError = true;
            result.apply = [preCommit](GitManager& manager) {
                emit manager.sigHookFinished(preCommit);
            };
            return result;
        }
        if (!backend.commit(message, amend, signoff, &result.error))
            return result;
        const HookResult postCommit = hooks.run(QStringLiteral("post-commit"));
        result.apply = [preCommit, postCommit](GitManager& manager) {
            if (!preCommit.output.isEmpty()) emit manager.sigHookFinished(preCommit);
            if (!postCommit.output.isEmpty() || !postCommit.success)
                emit manager.sigHookFinished(postCommit);
        };
        return result;
    }, true, true, true);
}

void GitManager::checkoutBranch(const QString& name)
{
    runBoolean(QStringLiteral("checkout"), [name](auto& backend, auto* error) {
        return backend.checkoutBranch(name, error);
    });
}

void GitManager::createBranch(const QString& name, const QString& from)
{
    runBoolean(QStringLiteral("create-branch"), [name, from](auto& backend, auto* error) {
        return backend.createBranch(name, from, error);
    });
}

void GitManager::deleteBranch(const QString& name, bool force)
{
    runBoolean(QStringLiteral("delete-branch"), [name, force](auto& backend, auto* error) {
        return backend.deleteBranch(name, force, error);
    });
}

void GitManager::renameBranch(const QString& name, const QString& newName)
{
    runBoolean(QStringLiteral("rename-branch"), [name, newName](auto& backend, auto* error) {
        return backend.renameBranch(name, newName, error);
    });
}

void GitManager::unsetBranchUpstream(const QString& name)
{
    runBoolean(QStringLiteral("unset-upstream"), [name](auto& backend, auto* error) {
        return backend.unsetBranchUpstream(name, error);
    });
}

void GitManager::createTag(const QString& name, const QString& hash, const QString& message)
{
    runBoolean(QStringLiteral("create-tag"), [name, hash, message](auto& backend, auto* error) {
        return backend.createTag(name, hash, message, error);
    });
}

void GitManager::deleteTag(const QString& name)
{
    runBoolean(QStringLiteral("delete-tag"), [name](auto& backend, auto* error) {
        return backend.deleteTag(name, error);
    });
}

void GitManager::pushTag(const QString& remote, const QString& tagName)
{
    runBoolean(QStringLiteral("push-tag"), [remote, tagName](auto& backend, auto* error) {
        return backend.pushTag(remote, tagName, error);
    });
}

void GitManager::deleteRemoteTag(const QString& remote, const QString& tagName)
{
    runBoolean(QStringLiteral("delete-remote-tag"), [remote, tagName](auto& backend, auto* error) {
        return backend.deleteRemoteTag(remote, tagName, error);
    });
}

void GitManager::pruneRemote(const QString& name)
{
    runBoolean(QStringLiteral("prune-remote"), [name](auto& backend, auto* error) {
        return backend.pruneRemote(name, error);
    });
}

void GitManager::addRemote(const QString& name, const QString& url)
{
    runBoolean(QStringLiteral("add-remote"), [name, url](auto& backend, auto* error) {
        return backend.addRemote(name, url, error);
    });
}

void GitManager::removeRemote(const QString& name)
{
    runBoolean(QStringLiteral("remove-remote"), [name](auto& backend, auto* error) {
        return backend.removeRemote(name, error);
    });
}

void GitManager::addWorktree(const QString& name, const QString& path,
                             const QString& branchName)
{
    runBoolean(QStringLiteral("add-worktree"), [name, path, branchName](auto& backend, auto* error) {
        return backend.addWorktree(name, path, branchName, error);
    });
}

void GitManager::moveWorktree(const QString& name, const QString& path)
{
    runBoolean(QStringLiteral("move-worktree"), [name, path](auto& backend, auto* error) {
        return backend.moveWorktree(name, path, error);
    });
}

void GitManager::lockWorktree(const QString& name, const QString& reason)
{
    runBoolean(QStringLiteral("lock-worktree"), [name, reason](auto& backend, auto* error) {
        return backend.lockWorktree(name, reason, error);
    });
}

void GitManager::unlockWorktree(const QString& name)
{
    runBoolean(QStringLiteral("unlock-worktree"), [name](auto& backend, auto* error) {
        return backend.unlockWorktree(name, error);
    });
}

void GitManager::removeWorktree(const QString& name, bool force)
{
    runBoolean(QStringLiteral("remove-worktree"), [name, force](auto& backend, auto* error) {
        return backend.removeWorktree(name, force, error);
    });
}

void GitManager::addSubmodule(const QString& url, const QString& path)
{
    runBoolean(QStringLiteral("add-submodule"), [url, path](auto& backend, auto* error) {
        return backend.addSubmodule(url, path, error);
    });
}

void GitManager::updateSubmodule(const QString& name)
{
    runBoolean(QStringLiteral("update-submodule"), [name](auto& backend, auto* error) {
        return backend.updateSubmodule(name, error);
    });
}

void GitManager::syncSubmodule(const QString& name)
{
    runBoolean(QStringLiteral("sync-submodule"), [name](auto& backend, auto* error) {
        return backend.syncSubmodule(name, error);
    });
}

void GitManager::setSubmoduleBranch(const QString& name, const QString& branch)
{
    runBoolean(QStringLiteral("set-submodule-branch"),
               [name, branch](auto& backend, auto* error) {
        return backend.setSubmoduleBranch(name, branch, error);
    });
}

void GitManager::removeSubmodule(const QString& name, bool force)
{
    runBoolean(QStringLiteral("remove-submodule"),
               [name, force](auto& backend, auto* error) {
        return backend.removeSubmodule(name, force, error);
    });
}

void GitManager::requestLfsState()
{
    enqueue(QStringLiteral("lfs-status"), [](LibGit2Backend& backend) {
        TaskResult result;
        LfsService service(backend.rootPath());
        const LfsState state = service.state(true, &result.error);
        result.applyOnError = true;
        result.apply = [state, error = result.error](GitManager& manager) {
            emit manager.sigLfsStateReady(state, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::trackLfsPattern(const QString& pattern)
{
    runBoolean(QStringLiteral("lfs-track"), [pattern](auto& backend, auto* error) {
        return LfsService(backend.rootPath()).track(pattern, error);
    });
}

void GitManager::untrackLfsPattern(const QString& pattern)
{
    runBoolean(QStringLiteral("lfs-untrack"), [pattern](auto& backend, auto* error) {
        return LfsService(backend.rootPath()).untrack(pattern, error);
    });
}

void GitManager::lockLfsPath(const QString& path)
{
    runBoolean(QStringLiteral("lfs-lock"), [path](auto& backend, auto* error) {
        return LfsService(backend.rootPath()).lock(path, error);
    }, false);
}

void GitManager::unlockLfsPath(const QString& path, bool force)
{
    runBoolean(QStringLiteral("lfs-unlock"), [path, force](auto& backend, auto* error) {
        return LfsService(backend.rootPath()).unlock(path, force, error);
    }, false);
}

void GitManager::requestHostingRemotes()
{
    enqueue(QStringLiteral("hosting-remotes"), [](LibGit2Backend& backend) {
        TaskResult result;
        const RepositoryState state = backend.snapshot(&result.error);
        const QVector<RemoteInfo> remotes = backend.remotes(&result.error);
        QVector<HostingRemoteInfo> hosted;
        if (result.error.isEmpty()) {
            for (const RemoteInfo& remote : remotes) {
                const HostingRemoteInfo value = HostingService::describe(
                    remote, state.headHash, state.headName);
                if (value.provider != HostingProvider::Unknown)
                    hosted.append(value);
            }
        }
        result.applyOnError = true;
        result.apply = [hosted, error = result.error](GitManager& manager) {
            emit manager.sigHostingRemotesReady(hosted, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::setHostingToken(HostingProvider provider, const QString& token)
{
    const int key = static_cast<int>(provider);
    if (token.isEmpty())
        _hostingTokens.remove(key);
    else
        _hostingTokens.insert(key, token);
}

void GitManager::clearHostingTokens()
{
    _hostingTokens.clear();
}

void GitManager::requestHostingChanges(const HostingRemoteInfo& remote)
{
    const QString token = _hostingTokens.value(static_cast<int>(remote.provider));
    enqueue(QStringLiteral("hosting-changes"),
            [remote, token](LibGit2Backend&) {
        TaskResult result;
        const QVector<HostingChangeInfo> changes = HostingApiService().changes(
            remote, token, &result.error);
        result.applyOnError = true;
        result.apply = [remote, changes, error = result.error](GitManager& manager) {
            emit manager.sigHostingChangesReady(remote, changes, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::requestHostingIssues(const HostingRemoteInfo& remote)
{
    const QString token = _hostingTokens.value(static_cast<int>(remote.provider));
    enqueue(QStringLiteral("hosting-issues"),
            [remote, token](LibGit2Backend&) {
        TaskResult result;
        const QVector<HostingIssueInfo> issues = HostingApiService().issues(
            remote, token, &result.error);
        result.applyOnError = true;
        result.apply = [remote, issues, error = result.error](GitManager& manager) {
            emit manager.sigHostingIssuesReady(remote, issues, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::requestHostingReviewFiles(
    const HostingRemoteInfo& remote, const HostingChangeInfo& change)
{
    const QString token = _hostingTokens.value(static_cast<int>(remote.provider));
    enqueue(QStringLiteral("hosting-review-files"),
            [remote, change, token](LibGit2Backend&) {
        TaskResult result;
        const QVector<HostingReviewFile> files = HostingApiService().reviewFiles(
            remote, change, token, &result.error);
        result.applyOnError = true;
        result.apply = [remote, change, files, error = result.error](GitManager& manager) {
            emit manager.sigHostingReviewFilesReady(remote, change, files, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::postHostingReviewComment(
    const HostingRemoteInfo& remote, const HostingChangeInfo& change,
    const HostingReviewFile& file, int line, const QString& body)
{
    const QString token = _hostingTokens.value(static_cast<int>(remote.provider));
    enqueue(QStringLiteral("hosting-review-comment"),
            [remote, change, file, line, body, token](LibGit2Backend&) {
        TaskResult result;
        const bool success = HostingApiService().postReviewComment(
            remote, change, file, line, body, token, &result.error);
        result.applyOnError = true;
        result.apply = [success, error = result.error](GitManager& manager) {
            emit manager.sigHostingReviewCommentFinished(
                success, success ? QStringLiteral("Review comment submitted.") : error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::requestGitDiagnostics()
{
    enqueue(QStringLiteral("git-diagnostics"), [](LibGit2Backend& backend) {
        TaskResult result;
        const QVector<RemoteInfo> remotes = backend.remotes(&result.error);
        GitDiagnosticReport report;
        if (result.error.isEmpty())
            report = GitDiagnosticService(backend.rootPath()).inspect(remotes);
        result.applyOnError = true;
        result.apply = [report, error = result.error](GitManager& manager) {
            emit manager.sigGitDiagnosticsReady(report, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::testRemoteConnection(const QString& remoteUrl)
{
    enqueue(QStringLiteral("test-remote-connection"),
            [remoteUrl](LibGit2Backend& backend) {
        TaskResult result;
        const QString message = GitDiagnosticService(backend.rootPath())
                                    .testRemote(remoteUrl, &result.error);
        result.applyOnError = true;
        result.apply = [message, error = result.error](GitManager& manager) {
            emit manager.sigRemoteConnectionTestFinished(
                error.isEmpty(), error.isEmpty() ? message : error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::requestHooks()
{
    enqueue(QStringLiteral("list-hooks"), [](LibGit2Backend& backend) {
        TaskResult result;
        const QVector<HookInfo> hooks = HookService(backend.rootPath())
                                            .hooks(&result.error);
        result.applyOnError = true;
        result.apply = [hooks, error = result.error](GitManager& manager) {
            emit manager.sigHooksReady(hooks, error);
        };
        return result;
    }, true, false, false, false);
}

void GitManager::fetch(bool prune)
{
    runBoolean(QStringLiteral("fetch"), [prune](auto& backend, auto* error) {
        return backend.fetchAll(prune, error);
    });
}

void GitManager::pull()
{
    runBoolean(QStringLiteral("pull"), [](auto& backend, auto* error) {
        return backend.pullRebase(error);
    }, true, true);
}

void GitManager::push()
{
    runBoolean(QStringLiteral("push"), [](auto& backend, auto* error) {
        return backend.push({}, {}, false, error);
    });
}

void GitManager::pushWithLease()
{
    runBoolean(QStringLiteral("push-with-lease"), [](auto& backend, auto* error) {
        return backend.push({}, {}, false, error, true);
    });
}

void GitManager::pushSetUpstream(const QString& remote, const QString& branch)
{
    runBoolean(QStringLiteral("push-upstream"), [remote, branch](auto& backend, auto* error) {
        return backend.push(remote, branch, true, error);
    });
}

void GitManager::stashPush(const QString& message, bool includeUntracked)
{
    runBoolean(QStringLiteral("stash-push"), [message, includeUntracked](auto& backend, auto* error) {
        return backend.stashPush(message, includeUntracked, error);
    });
}

void GitManager::listStashes()
{
    enqueue(QStringLiteral("stash-list"), [](LibGit2Backend& backend) {
        TaskResult result;
        const QStringList stashes = backend.stashes(&result.error);
        result.apply = [stashes](GitManager& manager) {
            emit manager.sigStashesReady(stashes);
        };
        return result;
    });
}

size_t GitManager::stashIndex(const QString& ref)
{
    const auto match = QRegularExpression(QStringLiteral("\\{(\\d+)\\}")).match(ref);
    return match.hasMatch() ? match.captured(1).toULongLong() : 0;
}

void GitManager::stashApply(const QString& ref, bool pop)
{
    const size_t index = stashIndex(ref);
    runBoolean(pop ? QStringLiteral("stash-pop") : QStringLiteral("stash-apply"),
               [index, pop](auto& backend, auto* error) {
        return backend.stashApply(index, pop, error);
    }, true, true);
}

void GitManager::stashDrop(const QString& ref)
{
    const size_t index = stashIndex(ref);
    runBoolean(QStringLiteral("stash-drop"), [index](auto& backend, auto* error) {
        return backend.stashDrop(index, error);
    });
}

void GitManager::continueOperation(const QString& operation)
{
    runHistoryOperation(operation + QStringLiteral("-continue"),
                        [operation](auto& backend, auto* error) {
        return backend.continueHistoryOperation(operation, error);
    });
}

void GitManager::abortOperation(const QString& operation)
{
    runBoolean(operation + QStringLiteral("-abort"), [operation](auto& backend, auto* error) {
        return backend.abortOperation(operation, error);
    }, true, true);
}

void GitManager::resolveConflict(const QString& path, bool ours)
{
    runBoolean(QStringLiteral("resolve"), [path, ours](auto& backend, auto* error) {
        return backend.resolveConflict(path, ours, error);
    });
}

bool GitManager::addToGitIgnore(const QString& path)
{
    QFile file(QDir(_repoPath).filePath(QStringLiteral(".gitignore")));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        emit sigError(file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream << QLatin1Char('\n') << path << QLatin1Char('\n');
    refresh();
    return true;
}

} // namespace Git
