#ifndef LIBGIT2BACKEND_H
#define LIBGIT2BACKEND_H

#include "RepositoryState.h"
#include <QStringList>
#include <atomic>
#include <functional>
#include <git2/types.h>

namespace Git {

struct InteractiveRebaseState;

struct RemoteCredentials {
    QString username;
    QString secret;
};

class LibGit2Backend {
public:
    using ProgressCallback = std::function<void(const QString&, int)>;

    LibGit2Backend();
    ~LibGit2Backend();
    LibGit2Backend(const LibGit2Backend&) = delete;
    LibGit2Backend& operator=(const LibGit2Backend&) = delete;

    void setCredentials(const RemoteCredentials& credentials);
    void setProgressCallback(ProgressCallback callback);
    void setCancelFlag(const std::atomic_bool* cancelFlag);

    bool open(const QString& path, QString* error = nullptr);
    bool initialize(const QString& path, QString* error = nullptr);
    bool clone(const QString& url, const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const { return _repository != nullptr; }
    QString rootPath() const;

    RepositoryState snapshot(QString* error = nullptr) const;
    CommitHistoryPage commitHistory(const CommitHistoryQuery& query,
                                    QString* error = nullptr) const;
    QString fileDiff(const QString& path, bool staged, bool untracked,
                     QString* error = nullptr) const;
    QString commitDiff(const QString& hash, QString* error = nullptr) const;
    QString revisionDiff(const QString& baseRevision,
                         const QString& targetRevision,
                         QString* error = nullptr) const;
    ExternalDiffInput externalDiffInput(const QString& path, bool staged,
                                        bool untracked,
                                        QString* error = nullptr) const;
    ExternalMergeInput externalMergeInput(const QString& path,
                                          QString* error = nullptr) const;

    bool stage(const QString& path, QString* error = nullptr);
    bool unstage(const QString& path, QString* error = nullptr);
    bool stageAll(QString* error = nullptr);
    bool unstageAll(QString* error = nullptr);
    bool discard(const QString& path, QString* error = nullptr);
    bool removeUntracked(const QString& path, QString* error = nullptr);
    bool applyPatch(const QString& patch, bool cached, bool reverse,
                    QString* error = nullptr);

    bool commit(const QString& message, bool amend, bool signoff,
                QString* error = nullptr);
    HistoryRewritePreview resetPreview(const QString& revision,
                                       QString* error = nullptr) const;
    RebasePlan rebasePlan(const QString& revision,
                          QString* error = nullptr) const;
    HistoryOperationResult mergeRevision(const QString& revision,
                                         QString* error = nullptr);
    HistoryOperationResult rebaseOnto(const RebasePlan& plan, bool interactive,
                                      QString* error = nullptr);
    HistoryOperationResult cherryPickCommit(const QString& revision,
                                             unsigned int mainline = 0,
                                             QString* error = nullptr);
    HistoryOperationResult revertCommit(const QString& revision,
                                        unsigned int mainline = 0,
                                        QString* error = nullptr);
    HistoryOperationResult resetToCommit(const HistoryRewritePreview& preview,
                                         ResetMode mode,
                                         QString* error = nullptr);
    bool checkoutBranch(const QString& name, QString* error = nullptr);
    bool createBranch(const QString& name, const QString& from,
                      QString* error = nullptr);
    bool deleteBranch(const QString& name, bool force, QString* error = nullptr);
    bool renameBranch(const QString& name, const QString& newName,
                      QString* error = nullptr);
    bool unsetBranchUpstream(const QString& name, QString* error = nullptr);
    bool createTag(const QString& name, const QString& target,
                   const QString& message, QString* error = nullptr);
    bool deleteTag(const QString& name, QString* error = nullptr);
    bool pushTag(const QString& remote, const QString& tagName,
                 QString* error = nullptr);
    bool deleteRemoteTag(const QString& remote, const QString& tagName,
                         QString* error = nullptr);
    bool pruneRemote(const QString& name, QString* error = nullptr);

    bool addRemote(const QString& name, const QString& url, QString* error = nullptr);
    bool removeRemote(const QString& name, QString* error = nullptr);
    QVector<RemoteInfo> remotes(QString* error = nullptr) const;
    bool fetchAll(bool prune, QString* error = nullptr);
    bool push(const QString& remote, const QString& branch, bool setUpstream,
              QString* error = nullptr, bool forceWithLease = false);
    bool pullRebase(QString* error = nullptr);
    QVector<WorktreeInfo> worktrees(QString* error = nullptr) const;
    bool addWorktree(const QString& name, const QString& path,
                     const QString& branchName, QString* error = nullptr);
    bool moveWorktree(const QString& name, const QString& path,
                      QString* error = nullptr);
    bool lockWorktree(const QString& name, const QString& reason = {},
                      QString* error = nullptr);
    bool unlockWorktree(const QString& name, QString* error = nullptr);
    bool removeWorktree(const QString& name, bool force,
                        QString* error = nullptr);
    QVector<SubmoduleInfo> submodules(QString* error = nullptr) const;
    bool addSubmodule(const QString& url, const QString& path,
                      QString* error = nullptr);
    bool updateSubmodule(const QString& name, QString* error = nullptr);
    bool syncSubmodule(const QString& name, QString* error = nullptr);
    bool setSubmoduleBranch(const QString& name, const QString& branch,
                            QString* error = nullptr);
    bool removeSubmodule(const QString& name, bool force,
                         QString* error = nullptr);

    QStringList stashes(QString* error = nullptr) const;
    bool stashPush(const QString& message, bool includeUntracked,
                   QString* error = nullptr);
    bool stashApply(size_t index, bool pop, QString* error = nullptr);
    bool stashDrop(size_t index, QString* error = nullptr);

    bool resolveConflict(const QString& path, bool ours, QString* error = nullptr);
    HistoryOperationResult continueHistoryOperation(
        const QString& operation, QString* error = nullptr);
    bool continueOperation(const QString& operation, QString* error = nullptr);
    bool abortOperation(const QString& operation, QString* error = nullptr);

    static QString lastError(const QString& fallback);
    static QString reversePatch(const QString& patch);

private:
    bool fetchRemote(const QString& name, bool prune, QString* error);
    bool finishRebase(git_rebase* rebase, QString* error);
    HistoryOperationResult finishRebaseResult(git_rebase* rebase,
                                              QString* error);
    HistoryOperationResult finishInteractiveRebase(
        git_rebase* rebase, InteractiveRebaseState state, QString* error);
    bool createStateCommit(const QString& operation, QString* error);
    bool isCancelled() const;
    void progress(const QString& text, int percent = -1) const;
    git_repository* _repository {nullptr};
    RemoteCredentials _credentials;
    ProgressCallback _progressCallback;
    const std::atomic_bool* _cancelFlag {nullptr};
};

} // namespace Git
#endif // LIBGIT2BACKEND_H
