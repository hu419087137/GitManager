#ifndef GITMANAGER_H
#define GITMANAGER_H

#include "RepositoryCache.h"
#include "LibGit2Backend.h"
#include <QObject>
#include <QQueue>
#include <atomic>
#include <functional>
#include <memory>

namespace Git {

class GitManager : public QObject {
    Q_OBJECT
public:
    explicit GitManager(QObject* parent = nullptr);
    ~GitManager() override;

    QString repositoryPath() const { return _repoPath; }
    bool isValid() const { return _isValid; }
    bool isBusy() const { return _busy; }

    void setRemoteCredentials(const QString& username, const QString& secret);
    void clearRemoteCredentials();

    void openRepository(const QString& path);
    void initRepository(const QString& path);
    void cloneRepository(const QString& url, const QString& path);
    void refresh();
    void refreshFromWatcher();
    void cancelAll();
    void setCommitHistoryQuery(const Git::CommitHistoryQuery& query);
    void reloadCommitHistory();
    void loadMoreCommits();
    void requestResetPreview(const QString& targetRevision);
    void requestRebasePlan(const QString& targetRevision);
    void mergeRevision(const QString& revision);
    void rebaseOnto(const Git::RebasePlan& plan, bool interactive);
    void cherryPickCommit(const QString& commitHash, int mainline = 0);
    void revertCommit(const QString& commitHash, int mainline = 0);
    void resetToCommit(const Git::HistoryRewritePreview& preview,
                       Git::ResetMode mode);
    void cancelDiffRequest();
    void fetchFileDiff(const QString& filePath, bool staged, bool untracked = false);
    void fetchCommitDiff(const QString& commitHash);
    void fetchRevisionDiff(const QString& baseRevision,
                           const QString& targetRevision);
    void stageFile(const QString& filePath);
    void unstageFile(const QString& filePath);
    void stageAll();
    void unstageAll();
    bool addToGitIgnore(const QString& filePath);
    void commit(const QString& message, bool amend = false, bool signoff = false);
    void checkoutBranch(const QString& branchName);
    void createBranch(const QString& branchName, const QString& from = {});
    void deleteBranch(const QString& branchName, bool force = false);
    void renameBranch(const QString& branchName, const QString& newName);
    void unsetBranchUpstream(const QString& branchName);
    void pull();
    void push();
    void pushWithLease();
    void pushSetUpstream(const QString& remote, const QString& branch);
    void fetch(bool prune = true);
    void addRemote(const QString& name, const QString& url);
    void removeRemote(const QString& name);
    void addWorktree(const QString& name, const QString& path,
                     const QString& branchName);
    void moveWorktree(const QString& name, const QString& path);
    void lockWorktree(const QString& name, const QString& reason = {});
    void unlockWorktree(const QString& name);
    void removeWorktree(const QString& name, bool force);
    void addSubmodule(const QString& url, const QString& path);
    void updateSubmodule(const QString& name);
    void syncSubmodule(const QString& name);
    void setSubmoduleBranch(const QString& name, const QString& branch);
    void removeSubmodule(const QString& name, bool force);
    void requestLfsState();
    void trackLfsPattern(const QString& pattern);
    void untrackLfsPattern(const QString& pattern);
    void lockLfsPath(const QString& path);
    void unlockLfsPath(const QString& path, bool force = false);
    void requestHostingRemotes();
    void setHostingToken(Git::HostingProvider provider, const QString& token);
    void clearHostingTokens();
    void requestHostingChanges(const Git::HostingRemoteInfo& remote);
    void requestHostingIssues(const Git::HostingRemoteInfo& remote);
    void requestHostingReviewFiles(const Git::HostingRemoteInfo& remote,
                                   const Git::HostingChangeInfo& change);
    void postHostingReviewComment(const Git::HostingRemoteInfo& remote,
                                  const Git::HostingChangeInfo& change,
                                  const Git::HostingReviewFile& file,
                                  int line, const QString& body);
    void requestGitDiagnostics();
    void testRemoteConnection(const QString& remoteUrl);
    void requestHooks();
    void discardFile(const QString& filePath);
    void removeUntracked(const QString& filePath);
    void stashPush(const QString& message, bool includeUntracked);
    void listStashes();
    void stashApply(const QString& ref, bool pop);
    void stashDrop(const QString& ref);
    void continueOperation(const QString& operation);
    void abortOperation(const QString& operation);
    void resolveConflict(const QString& filePath, bool ours);
    void applyPatch(const QString& patch, bool cached, bool reverse);
    void createTag(const QString& tagName, const QString& commitHash,
                   const QString& message = {});
    void deleteTag(const QString& tagName);
    void pushTag(const QString& remote, const QString& tagName);
    void deleteRemoteTag(const QString& remote, const QString& tagName);
    void pruneRemote(const QString& name);

signals:
    void sigRepositoryOpened(const QString& path, bool success, const QString& error);
    void sigStateReady(const Git::RepositoryState& state);
    void sigCommitHistoryReady(const Git::CommitHistoryPage& page);
    void sigCommitHistoryLoading(bool loading);
    void sigResetPreviewReady(const Git::HistoryRewritePreview& preview);
    void sigRebasePlanReady(const Git::RebasePlan& plan);
    void sigHistoryOperationFinished(const QString& operation,
                                     Git::HistoryOperationStatus status,
                                     const QString& message);
    void sigDiffReady(const QString& diff, const QString& title,
                      bool staged, bool hunkActionsEnabled);
    void sigStashesReady(const QStringList& stashes);
    void sigLfsStateReady(const Git::LfsState& state, const QString& error);
    void sigHostingRemotesReady(const QVector<Git::HostingRemoteInfo>& remotes,
                                const QString& error);
    void sigHostingChangesReady(const Git::HostingRemoteInfo& remote,
                                const QVector<Git::HostingChangeInfo>& changes,
                                const QString& error);
    void sigHostingIssuesReady(const Git::HostingRemoteInfo& remote,
                               const QVector<Git::HostingIssueInfo>& issues,
                               const QString& error);
    void sigHostingReviewFilesReady(
        const Git::HostingRemoteInfo& remote,
        const Git::HostingChangeInfo& change,
        const QVector<Git::HostingReviewFile>& files,
        const QString& error);
    void sigHostingReviewCommentFinished(bool success, const QString& message);
    void sigGitDiagnosticsReady(const Git::GitDiagnosticReport& report,
                                const QString& error);
    void sigRemoteConnectionTestFinished(bool success, const QString& message);
    void sigHooksReady(const QVector<Git::HookInfo>& hooks, const QString& error);
    void sigHookFinished(const Git::HookResult& result);
    void sigOperationFinished(const QString& operation, bool success, const QString& message);
    void sigError(const QString& message);
    void sigInfo(const QString& message);
    void sigCommandStarted(const QString& command);
    void sigCommandOutput(const QString& output, bool standardError);
    void sigCommandRun(const QString& command, const QString& output, bool success);
    void sigBusyChanged(bool busy);

private:
    struct TaskResult {
        QString error;
        std::function<void(GitManager&)> apply;
        bool applyOnError {false};
    };

    struct Task {
        QString operation;
        QString repositoryPath;
        quint64 generation;
        std::function<TaskResult(LibGit2Backend&)> work;
        std::shared_ptr<std::atomic_bool> cancelFlag;
        RemoteCredentials credentials;
        bool requiresRepository {true};
        bool reportCompletion {false};
        bool refreshAfter {false};
        bool reportError {true};
        bool refreshOnCancel {false};
    };

    void enqueue(const QString& operation,
                 std::function<TaskResult(LibGit2Backend&)> work,
                 bool requiresRepository = true,
                 bool reportCompletion = false,
                 bool refreshAfter = false,
                 bool reportError = true,
                 bool refreshOnCancel = false);
    void startNext();
    void runBoolean(const QString& operation,
                    std::function<bool(LibGit2Backend&, QString*)> work,
                    bool refreshAfter = true,
                    bool refreshOnError = false);
    void runHistoryOperation(
        const QString& operation,
        std::function<HistoryOperationResult(LibGit2Backend&, QString*)> work);
    void publishState(const RepositoryState& state);
    void requestCommitHistory(bool append);
    void discardQueuedHistoryTasks();
    void discardQueuedDiffTasks();
    void discardQueuedPreviewTasks();
    void resetCommitHistoryState();
    static size_t stashIndex(const QString& ref);

    QString _repoPath;
    bool _isValid {false};
    bool _busy {false};
    quint64 _generation {0};
    QQueue<Task> _tasks;
    std::shared_ptr<std::atomic_bool> _activeCancelFlag;
    QString _activeOperation;
    RemoteCredentials _credentials;
    CommitHistoryQuery _historyQuery;
    QString _historyRefsVersion;
    int _historyLoadedCount {0};
    bool _historyHasMore {true};
    bool _historyLoaded {false};
    bool _historyLoading {false};
    quint64 _historyRequest {0};
    quint64 _diffRequest {0};
    quint64 _previewRequest {0};
    RepositoryCache _cache;
    QHash<int, QString> _hostingTokens;
};

} // namespace Git
#endif // GITMANAGER_H
