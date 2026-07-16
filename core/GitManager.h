#ifndef GITMANAGER_H
#define GITMANAGER_H

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
    void cancelAll();
    void setCommitHistoryQuery(const Git::CommitHistoryQuery& query);
    void reloadCommitHistory();
    void loadMoreCommits();
    void cancelDiffRequest();
    void fetchFileDiff(const QString& filePath, bool staged, bool untracked = false);
    void fetchCommitDiff(const QString& commitHash);
    void stageFile(const QString& filePath);
    void unstageFile(const QString& filePath);
    void stageAll();
    void unstageAll();
    bool addToGitIgnore(const QString& filePath);
    void commit(const QString& message, bool amend = false, bool signoff = false);
    void checkoutBranch(const QString& branchName);
    void createBranch(const QString& branchName, const QString& from = {});
    void deleteBranch(const QString& branchName, bool force = false);
    void pull();
    void push();
    void pushSetUpstream(const QString& remote, const QString& branch);
    void fetch(bool prune = true);
    void addRemote(const QString& name, const QString& url);
    void removeRemote(const QString& name);
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

signals:
    void sigRepositoryOpened(const QString& path, bool success, const QString& error);
    void sigStateReady(const Git::RepositoryState& state);
    void sigCommitHistoryReady(const Git::CommitHistoryPage& page);
    void sigCommitHistoryLoading(bool loading);
    void sigDiffReady(const QString& diff, const QString& title,
                      bool staged, bool hunkActionsEnabled);
    void sigStashesReady(const QStringList& stashes);
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
    };

    void enqueue(const QString& operation,
                 std::function<TaskResult(LibGit2Backend&)> work,
                 bool requiresRepository = true,
                 bool reportCompletion = false,
                 bool refreshAfter = false,
                 bool reportError = true);
    void startNext();
    void runBoolean(const QString& operation,
                    std::function<bool(LibGit2Backend&, QString*)> work,
                    bool refreshAfter = true,
                    bool refreshOnError = false);
    void publishState(const RepositoryState& state);
    void requestCommitHistory(bool append);
    void discardQueuedHistoryTasks();
    void discardQueuedDiffTasks();
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
};

} // namespace Git
#endif // GITMANAGER_H
