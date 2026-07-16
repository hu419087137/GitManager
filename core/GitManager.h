#ifndef GITMANAGER_H
#define GITMANAGER_H

#include "GitCommandRunner.h"
#include "RepositoryState.h"
#include <QHash>
#include <QObject>

namespace Git {

class GitManager : public QObject {
    Q_OBJECT

public:
    explicit GitManager(QObject* parent = nullptr);

    QString repositoryPath() const { return _repoPath; }
    bool isValid() const { return _isValid; }
    bool isBusy() const { return _runner.isBusy(); }

    void openRepository(const QString& path);
    void refresh();
    void cancelAll();
    void fetchFileDiff(const QString& filePath, bool staged, bool untracked = false);
    void fetchCommitDiff(const QString& commitHash);

    void stageFile(const QString& filePath);
    void unstageFile(const QString& filePath);
    void stageAll();
    void unstageAll();
    bool addToGitIgnore(const QString& filePath);
    void commit(const QString& message);
    void checkoutBranch(const QString& branchName);
    void createBranch(const QString& branchName, const QString& from = {});
    void deleteBranch(const QString& branchName, bool force = false);
    void pull();
    void push();
    void createTag(const QString& tagName, const QString& commitHash,
                   const QString& message = {});
    void deleteTag(const QString& tagName);

signals:
    void sigRepositoryOpened(const QString& path, bool success, const QString& error);
    void sigStateReady(const Git::RepositoryState& state);
    void sigDiffReady(const QString& diff, const QString& title);
    void sigOperationFinished(const QString& operation, bool success, const QString& message);
    void sigError(const QString& message);
    void sigInfo(const QString& message);
    void sigCommandStarted(const QString& command);
    void sigCommandOutput(const QString& output, bool standardError);
    void sigCommandRun(const QString& command, const QString& output, bool success);
    void sigBusyChanged(bool busy);

private:
    struct RequestContext { quint64 generation; QString operation; QString command; };

    quint64 run(const QString& operation, const QStringList& arguments,
                bool network = false, int timeoutMs = -1);
    void handleResult(const GitResult& result);
    void handleRefreshPart(const GitResult& result);
    void emitResultError(const GitResult& result);
    static QVector<Commit> parseLog(const QByteArray& output);
    static void assignLanes(QVector<Commit>& commits);

    GitCommandRunner _runner;
    QString _repoPath;
    bool _isValid {false};
    quint64 _generation {0};
    QHash<quint64, RequestContext> _contexts;
    RepositoryState _pendingState;
    int _pendingRefreshParts {0};
};

} // namespace Git

#endif // GITMANAGER_H
