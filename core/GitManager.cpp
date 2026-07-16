#include "GitManager.h"
#include "parsers/BranchParser.h"
#include "parsers/StatusParser.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace Git {
namespace {

const QByteArray kRecordSeparator("---COMMIT_BEGIN---");

QString resultMessage(const GitResult& result)
{
    QString detail = QString::fromUtf8(result.standardError).trimmed();
    if (detail.isEmpty()) {
        switch (result.error) {
        case GitError::FailedToStart: detail = QStringLiteral("无法启动 git，请检查 PATH。"); break;
        case GitError::TimedOut: detail = QStringLiteral("Git 命令执行超时。"); break;
        case GitError::Cancelled: detail = QStringLiteral("操作已取消。"); break;
        case GitError::NonZeroExit: detail = QStringLiteral("Git 命令失败。"); break;
        default: break;
        }
    }
    return QStringLiteral("%1\n命令：%2\n退出码：%3")
        .arg(detail, GitCommandRunner::formatCommand(result.arguments))
        .arg(result.exitCode);
}

} // namespace

GitManager::GitManager(QObject* parent)
    : QObject(parent), _runner(this)
{
    qRegisterMetaType<GitResult>();
    qRegisterMetaType<RepositoryState>();

    connect(&_runner, &GitCommandRunner::sigStarted, this,
            [this](quint64 id, const QString&, const QString& command) {
        _contexts[id].command = command;
        emit sigCommandStarted(command);
    });
    connect(&_runner, &GitCommandRunner::sigOutput, this,
            [this](quint64 id, const QByteArray& data, bool error) {
        const QString operation = _contexts.value(id).operation;
        if (!operation.startsWith(QStringLiteral("refresh-"))
            && !operation.startsWith(QStringLiteral("diff-"))
            && operation != QStringLiteral("open"))
            emit sigCommandOutput(QString::fromUtf8(data), error);
    });
    connect(&_runner, &GitCommandRunner::sigFinished,
            this, &GitManager::handleResult);
    connect(&_runner, &GitCommandRunner::sigBusyChanged,
            this, &GitManager::sigBusyChanged);
}

void GitManager::openRepository(const QString& path)
{
    cancelAll();
    _repoPath = QDir(path).absolutePath();
    _isValid = false;
    ++_generation;
    run(QStringLiteral("open"), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
}

void GitManager::refresh()
{
    if (!_isValid)
        return;
    ++_generation;
    _pendingState = {};
    _pendingState.rootPath = _repoPath;
    _pendingRefreshParts = 3;
    run(QStringLiteral("refresh-status"), {QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("status"), QStringLiteral("--porcelain=v2"), QStringLiteral("-z"),
        QStringLiteral("--branch"), QStringLiteral("--untracked-files=all")});
    run(QStringLiteral("refresh-log"), {QStringLiteral("log"), QStringLiteral("--topo-order"),
        QStringLiteral("--format=---COMMIT_BEGIN---%n%H%n%h%n%P%n%an%n%aI%n%s%n%D"),
        QStringLiteral("-n1000")});
    run(QStringLiteral("refresh-branches"), {QStringLiteral("for-each-ref"),
        QStringLiteral("--format=%(refname)%09%(refname:short)%09%(objectname:short)%09%(upstream:short)%09%(upstream:track,nobracket)%00"),
        QStringLiteral("refs/heads"), QStringLiteral("refs/remotes")});
}

void GitManager::cancelAll()
{
    _runner.cancelAll();
    _contexts.clear();
    _pendingRefreshParts = 0;
}

void GitManager::fetchFileDiff(const QString& filePath, bool staged, bool untracked)
{
    QStringList args;
    QString operation = QStringLiteral("diff-file:") + filePath;
    if (untracked) {
#ifdef Q_OS_WIN
        args = {QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--"),
                QStringLiteral("NUL"), filePath};
#else
        args = {QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--"),
                QStringLiteral("/dev/null"), filePath};
#endif
        operation = QStringLiteral("diff-untracked:") + filePath;
    } else {
        args = {QStringLiteral("diff")};
        if (staged) args << QStringLiteral("--cached");
        args << QStringLiteral("--") << filePath;
    }
    run(operation, args);
}

void GitManager::fetchCommitDiff(const QString& hash)
{
    run(QStringLiteral("diff-commit:") + hash.left(8),
        {QStringLiteral("show"), QStringLiteral("--stat"), QStringLiteral("-p"), hash});
}

void GitManager::stageFile(const QString& path) { run(QStringLiteral("stage"), {"add", "--", path}); }
void GitManager::unstageFile(const QString& path) { run(QStringLiteral("unstage"), {"reset", "-q", "HEAD", "--", path}); }
void GitManager::stageAll() { run(QStringLiteral("stage-all"), {"add", "-A"}); }
void GitManager::unstageAll() { run(QStringLiteral("unstage-all"), {"reset", "-q"}); }

bool GitManager::addToGitIgnore(const QString& path)
{
    QFile file(QDir(_repoPath).filePath(QStringLiteral(".gitignore")));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        emit sigError(QStringLiteral("无法打开 .gitignore：%1").arg(file.errorString()));
        return false;
    }
    QTextStream(&file) << '\n' << path << '\n';
    refresh();
    return true;
}

void GitManager::commit(const QString& message) { run(QStringLiteral("commit"), {"commit", "-m", message}); }
void GitManager::checkoutBranch(const QString& name) { run(QStringLiteral("checkout"), {"checkout", name}); }
void GitManager::createBranch(const QString& name, const QString& from)
{
    QStringList args = {"checkout", "-b", name};
    if (!from.isEmpty()) args << from;
    run(QStringLiteral("create-branch"), args);
}
void GitManager::deleteBranch(const QString& name, bool force)
{ run(QStringLiteral("delete-branch"), {"branch", force ? "-D" : "-d", name}); }
void GitManager::pull()
{ run(QStringLiteral("pull"), {"pull", "--rebase", "--autostash"}, true); }
void GitManager::push() { run(QStringLiteral("push"), {"push"}, true); }
void GitManager::createTag(const QString& name, const QString& hash, const QString& message)
{
    QStringList args = {"tag"};
    if (!message.isEmpty()) args << "-a";
    args << name;
    if (!message.isEmpty()) args << "-m" << message;
    if (!hash.isEmpty()) args << hash;
    run(QStringLiteral("create-tag"), args);
}
void GitManager::deleteTag(const QString& name)
{ run(QStringLiteral("delete-tag"), {"tag", "-d", name}); }

quint64 GitManager::run(const QString& operation, const QStringList& args, bool network, int timeout)
{
    const quint64 id = _runner.enqueue(operation, _repoPath, args, network, timeout);
    _contexts.insert(id, {_generation, operation, {}});
    return id;
}

void GitManager::handleResult(const GitResult& result)
{
    const RequestContext context = _contexts.take(result.requestId);
    emit sigCommandRun(context.command, QString::fromUtf8(result.standardError).trimmed(), result.success());
    if (context.generation != _generation || result.workingDirectory != _repoPath)
        return;

    if (result.operation == QStringLiteral("open")) {
        _isValid = result.success() && !result.standardOutput.trimmed().isEmpty();
        emit sigRepositoryOpened(_repoPath, _isValid, _isValid ? QString() : resultMessage(result));
        if (_isValid) refresh();
        return;
    }
    if (result.operation.startsWith(QStringLiteral("refresh-"))) {
        handleRefreshPart(result);
        return;
    }
    const bool diffWithChanges = result.operation.startsWith(QStringLiteral("diff-untracked:"))
                                 && result.exitCode == 1;
    if (result.operation.startsWith(QStringLiteral("diff-"))) {
        if (result.success() || diffWithChanges) {
            emit sigDiffReady(QString::fromUtf8(result.standardOutput), result.operation.section(':', 1));
        } else emitResultError(result);
        return;
    }
    if (!result.success()) {
        emitResultError(result);
        emit sigOperationFinished(result.operation, false, resultMessage(result));
        return;
    }
    const QString output = QString::fromUtf8(result.standardOutput).trimmed();
    emit sigOperationFinished(result.operation, true, output);
    refresh();
}

void GitManager::handleRefreshPart(const GitResult& result)
{
    if (!result.success()) {
        // 空仓库没有 log 是合法状态。
        if (result.operation != QStringLiteral("refresh-log"))
            emitResultError(result);
    } else if (result.operation == QStringLiteral("refresh-status")) {
        const StatusSummary status = StatusParser::parse(result.standardOutput);
        _pendingState.headName = status.headName;
        _pendingState.headHash = status.headHash;
        _pendingState.upstream = status.upstream;
        _pendingState.ahead = status.ahead;
        _pendingState.behind = status.behind;
        _pendingState.detached = status.detached;
        _pendingState.unborn = status.unborn;
        _pendingState.files = status.files;
    } else if (result.operation == QStringLiteral("refresh-log")) {
        _pendingState.commits = parseLog(result.standardOutput);
    } else if (result.operation == QStringLiteral("refresh-branches")) {
        _pendingState.branches = BranchParser::parse(result.standardOutput, _pendingState.headName);
    }
    if (--_pendingRefreshParts == 0)
        emit sigStateReady(_pendingState);
}

void GitManager::emitResultError(const GitResult& result)
{
    emit sigError(resultMessage(result));
}

QVector<Commit> GitManager::parseLog(const QByteArray& output)
{
    QVector<Commit> commits;
    const QString text = QString::fromUtf8(output);
    const QStringList records = text.split(QString::fromLatin1(kRecordSeparator), Qt::SkipEmptyParts);
    for (QString record : records) {
        QStringList lines = record.split('\n');
        while (!lines.isEmpty() && lines.first().isEmpty()) lines.removeFirst();
        if (lines.size() < 7) continue;
        Commit commit;
        commit.hash = lines[0].trimmed();
        commit.shortHash = lines[1].trimmed();
        commit.parents = lines[2].split(' ', Qt::SkipEmptyParts);
        commit.authorName = lines[3];
        commit.date = QDateTime::fromString(lines[4], Qt::ISODate);
        commit.subject = lines[5];
        for (const QString& ref : lines[6].split(','))
            if (!ref.trimmed().isEmpty()) commit.refs << ref.trimmed();
        commits.append(commit);
    }
    assignLanes(commits);
    return commits;
}

void GitManager::assignLanes(QVector<Commit>& commits)
{
    QVector<QString> lanes;
    for (Commit& commit : commits) {
        int lane = lanes.indexOf(commit.hash);
        if (lane < 0) {
            lane = lanes.indexOf(QString());
            if (lane < 0) { lane = lanes.size(); lanes.append(QString()); }
        }
        commit.lane = lane;
        if (commit.parents.isEmpty()) lanes[lane].clear();
        else lanes[lane] = lanes.contains(commit.parents.first()) ? QString() : commit.parents.first();
        for (int p = 1; p < commit.parents.size(); ++p) {
            if (lanes.contains(commit.parents[p])) continue;
            int freeLane = lanes.indexOf(QString());
            if (freeLane < 0) lanes.append(commit.parents[p]);
            else lanes[freeLane] = commit.parents[p];
        }
        commit.activeLanes = lanes;
        while (!commit.activeLanes.isEmpty() && commit.activeLanes.last().isEmpty())
            commit.activeLanes.removeLast();
    }
}

} // namespace Git
