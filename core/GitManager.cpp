#include "GitManager.h"

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QTextStream>

namespace Git {

namespace {

QString operationLabel(const QString& operation)
{
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
    runBoolean(QStringLiteral("commit"), [message, amend, signoff](auto& backend, auto* error) {
        return backend.commit(message, amend, signoff, error);
    });
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
