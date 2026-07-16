#include "GitManager.h"
#include <QProcess>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

namespace Git {

static constexpr int kGitTimeoutMs = 15000;

// 提交记录分隔符，必须不出现在正常提交消息中
static const QString kRecordSep = QStringLiteral("---COMMIT_BEGIN---");

// -------- 构造 --------

GitManager::GitManager(QObject* parent)
    : QObject(parent)
{}

// -------- 仓库打开 --------

bool GitManager::openRepository(const QString& path)
{
    _repoPath = path;
    _isValid  = false;

    bool ok = false;
    const QString out = runGit({"rev-parse", "--git-dir"}, &ok);
    _isValid = ok && !out.trimmed().isEmpty();
    return _isValid;
}

QString GitManager::currentBranch()
{
    return runGit({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed();
}

// -------- 数据查询 --------

QVector<Commit> GitManager::fetchLog(int maxCount)
{
    // 每条提交用 kRecordSep 开头，字段之间用 \n 分隔
    const QString fmt = kRecordSep
        + "\n%H\n%h\n%P\n%an\n%aI\n%s\n%D";

    const QString output = runGit({
        "log",
        "--topo-order",
        QStringLiteral("--format=%1").arg(fmt),
        QStringLiteral("-n%1").arg(maxCount)
    });

    QVector<Commit> commits;
    const QStringList records = output.split(kRecordSep, Qt::SkipEmptyParts);

    for (const QString& record : records) {
        QStringList lines = record.split('\n');
        // 跳过空行
        while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
            lines.removeFirst();
        if (lines.size() < 7)
            continue;

        Commit c;
        c.hash      = lines[0].trimmed();
        c.shortHash = lines[1].trimmed();

        const QString parentsStr = lines[2].trimmed();
        if (!parentsStr.isEmpty())
            c.parents = parentsStr.split(' ', Qt::SkipEmptyParts);

        c.authorName = lines[3].trimmed();
        c.date       = QDateTime::fromString(lines[4].trimmed(), Qt::ISODateWithMs);
        if (!c.date.isValid())
            c.date = QDateTime::fromString(lines[4].trimmed(), Qt::ISODate);

        c.subject = lines[5].trimmed();

        // refs 字段：逗号分隔的分支/标签名
        const QString refsStr = lines[6].trimmed();
        if (!refsStr.isEmpty()) {
            for (const QString& ref : refsStr.split(',')) {
                const QString r = ref.trimmed();
                if (!r.isEmpty())
                    c.refs.append(r);
            }
        }

        commits.append(c);
    }

    assignLanes(commits);
    return commits;
}

QVector<Branch> GitManager::fetchBranches()
{
    const QString current = currentBranch();

    // for-each-ref 一次性获取本地和远端分支
    const QString output = runGit({
        "for-each-ref",
        "--format=%(refname:short)\t%(objectname:short)\t%(upstream:short)",
        "refs/heads",
        "refs/remotes"
    });

    QVector<Branch> branches;
    for (const QString& line : output.split('\n', Qt::SkipEmptyParts)) {
        const QStringList parts = line.split('\t');
        if (parts.size() < 2)
            continue;

        Branch b;
        b.name      = parts[0];
        b.hash      = parts[1];
        b.upstream  = (parts.size() >= 3) ? parts[2] : QString();
        b.isRemote  = b.name.contains('/');
        b.isCurrent = (b.name == current);
        branches.append(b);
    }
    return branches;
}

QVector<File> GitManager::fetchStatus()
{
    const QString output = runGit({"status", "--porcelain=v1", "-u"});
    QVector<File> files;

    for (const QString& line : output.split('\n', Qt::SkipEmptyParts)) {
        if (line.size() < 4)
            continue;

        File f;
        f.indexStatus = static_cast<File::Status>(line[0].toLatin1());
        f.workStatus  = static_cast<File::Status>(line[1].toLatin1());

        const QString pathPart = line.mid(3);
        // 重命名格式："old -> new"
        if (pathPart.contains(" -> ")) {
            const QStringList renamed = pathPart.split(" -> ");
            f.originalPath = renamed[0].trimmed().remove('"');
            f.path         = renamed[1].trimmed().remove('"');
        } else {
            f.path = pathPart.trimmed().remove('"');
        }

        files.append(f);
    }
    return files;
}

QString GitManager::fetchFileDiff(const QString& filePath, bool staged)
{
    QStringList args = {"diff"};
    if (staged)
        args << "--cached";
    args << "--" << filePath;
    return runGit(args);
}

QString GitManager::fetchCommitDiff(const QString& commitHash)
{
    return runGit({"show", "--stat", "-p", commitHash});
}

// -------- Git 操作 --------

bool GitManager::stageFile(const QString& filePath)
{
    bool ok = false;
    runGit({"add", "--", filePath}, &ok);
    return ok;
}

bool GitManager::unstageFile(const QString& filePath)
{
    bool ok = false;
    runGit({"restore", "--staged", "--", filePath}, &ok);
    return ok;
}

bool GitManager::stageAll()
{
    bool ok = false;
    runGit({"add", "-A"}, &ok);
    return ok;
}

bool GitManager::unstageAll()
{
    bool ok = false;
    runGit({"restore", "--staged", "."}, &ok);
    return ok;
}

bool GitManager::addToGitIgnore(const QString& filePath)
{
    const QString ignorePath = _repoPath + QStringLiteral("/.gitignore");
    QFile file(ignorePath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        emit sigError(QStringLiteral("Cannot open .gitignore: %1").arg(file.errorString()));
        return false;
    }
    QTextStream out(&file);
    out << '\n' << filePath << '\n';
    return true;
}

bool GitManager::commit(const QString& message)
{
    bool ok = false;
    runGit({"commit", "-m", message}, &ok);
    return ok;
}

bool GitManager::checkoutBranch(const QString& branchName)
{
    bool ok = false;
    runGit({"checkout", branchName}, &ok);
    return ok;
}

bool GitManager::createBranch(const QString& branchName, const QString& from)
{
    bool ok = false;
    QStringList args = {"checkout", "-b", branchName};
    if (!from.isEmpty())
        args << from;
    runGit(args, &ok);
    return ok;
}

bool GitManager::deleteBranch(const QString& branchName, bool force)
{
    bool ok = false;
    runGit({"branch", force ? "-D" : "-d", branchName}, &ok);
    return ok;
}

bool GitManager::pull()
{
    // 1. 检查工作区是否有未提交变更（包含暂存和未暂存）
    const QString statusOut = runGit({"status", "--porcelain"});
    const bool isDirty = !statusOut.trimmed().isEmpty();

    // 2. 有未提交变更则先贮藏，避免 rebase 冲突
    bool stashed = false;
    if (isDirty) {
        emit sigInfo(QStringLiteral("Stashing uncommitted changes…"));
        bool ok = false;
        runGit({"stash", "push", "--include-untracked",
                "-m", "GitManager: auto-stash before pull --rebase"}, &ok);
        if (!ok) {
            emit sigError(QStringLiteral("git stash failed, pull aborted."));
            return false;
        }
        stashed = true;
    }

    // 3. pull --rebase
    emit sigInfo(QStringLiteral("Pulling (rebase)…"));
    bool pullOk = false;
    runGit({"pull", "--rebase"}, &pullOk);

    // 4. 无论 pull 是否成功，都把贮藏 pop 回来，保护现场
    if (stashed) {
        emit sigInfo(QStringLiteral("Restoring stashed changes…"));
        bool popOk = false;
        runGit({"stash", "pop"}, &popOk);
        if (!popOk) {
            // stash pop 失败（通常是 rebase 后有冲突），提示用户手动处理
            emit sigError(QStringLiteral(
                "git stash pop failed — resolve conflicts manually "
                "(run 'git stash pop' after fixing)."));
        }
    }

    return pullOk;
}

bool GitManager::push()
{
    bool ok = false;
    runGit({"push"}, &ok);
    return ok;
}

bool GitManager::createTag(const QString& tagName,
                           const QString& commitHash,
                           const QString& message)
{
    bool ok = false;
    QStringList args;
    if (message.isEmpty()) {
        // 轻量标签
        args = {"tag", tagName};
    } else {
        // 附注标签
        args = {"tag", "-a", tagName, "-m", message};
    }
    if (!commitHash.isEmpty())
        args << commitHash;
    runGit(args, &ok);
    return ok;
}

bool GitManager::deleteTag(const QString& tagName)
{
    bool ok = false;
    runGit({"tag", "-d", tagName}, &ok);
    return ok;
}

// -------- Private --------

// 需要在终端面板显示输出的写操作子命令
static const QSet<QString> kWriteSubCommands = {
    "pull", "push", "commit", "stash", "add", "restore",
    "checkout", "branch", "fetch", "merge", "rebase", "reset", "tag"
};

QString GitManager::runGit(const QStringList& args, bool* ok) const
{
    if (ok) *ok = false;

    QProcess proc;
    proc.setWorkingDirectory(_repoPath);
    proc.start("git", args);

    const QString cmdStr = QStringLiteral("git %1").arg(args.join(' '));

    if (!proc.waitForFinished(kGitTimeoutMs)) {
        proc.kill();
        auto* self = const_cast<GitManager*>(this);
        const QString msg = QStringLiteral("git timeout: %1").arg(cmdStr);
        emit self->sigError(msg);
        emit self->sigCommandRun(cmdStr, msg, false);
        return {};
    }

    const QString stdOut  = QString::fromUtf8(proc.readAllStandardOutput());
    const QString stdErr  = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    const bool    success = (proc.exitCode() == 0);

    // 写操作展示完整输出；只读查询只记录命令行（输出置空，减少噪音）
    const bool isWrite = !args.isEmpty() && kWriteSubCommands.contains(args[0]);
    {
        QString logOutput;
        if (!success)
            logOutput = stdErr;
        else if (isWrite)
            logOutput = stdOut.trimmed();

        emit const_cast<GitManager*>(this)->sigCommandRun(cmdStr, logOutput, success);
    }

    if (!success) {
        if (!stdErr.isEmpty())
            emit const_cast<GitManager*>(this)->sigError(stdErr);
        return {};
    }

    if (ok) *ok = true;
    return stdOut;
}

void GitManager::assignLanes(QVector<Commit>& commits)
{
    // lanes[i] = 该列期望出现的下一个提交 hash；为空则该列空闲
    QVector<QString> lanes;

    for (Commit& commit : commits) {
        // 1. 找到本提交所在的列（由之前某行分配的）
        int lane = -1;
        for (int i = 0; i < lanes.size(); ++i) {
            if (lanes[i] == commit.hash) {
                lane = i;
                break;
            }
        }

        // 若还未被追踪，分配到第一个空闲列
        if (lane < 0) {
            for (int i = 0; i < lanes.size(); ++i) {
                if (lanes[i].isEmpty()) {
                    lane = i;
                    break;
                }
            }
            if (lane < 0) {
                lane = lanes.size();
                lanes.append(QString());
            }
        }

        commit.lane = lane;

        // 2. 将该列指向第一个父提交（线段延续）
        // 注意：若第一个父提交已被其他列追踪（菱形合并场景），
        // 则释放本列而非重复追踪，否则绘图时会出现连线断开。
        if (!commit.parents.isEmpty()) {
            const QString& p0 = commit.parents[0];
            bool alreadyTracked = false;
            for (int i = 0; i < lanes.size(); ++i) {
                if (i != lane && lanes[i] == p0) {
                    alreadyTracked = true;
                    break;
                }
            }
            lanes[lane] = alreadyTracked ? QString() : p0;
        } else {
            lanes[lane].clear(); // 历史链终止，释放列
        }

        // 3. 额外父提交（merge commit）分配到空闲列
        for (int p = 1; p < commit.parents.size(); ++p) {
            const QString& ph = commit.parents[p];
            bool tracked = false;
            for (const QString& l : lanes) {
                if (l == ph) { tracked = true; break; }
            }
            if (tracked)
                continue;

            int freeLane = -1;
            for (int i = 0; i < lanes.size(); ++i) {
                if (lanes[i].isEmpty()) { freeLane = i; break; }
            }
            if (freeLane < 0)
                lanes.append(ph);
            else
                lanes[freeLane] = ph;
        }

        // 4. 快照当前各列状态（去除尾部空闲列）
        commit.activeLanes = lanes;
        while (!commit.activeLanes.isEmpty() && commit.activeLanes.last().isEmpty())
            commit.activeLanes.removeLast();
    }
}

} // namespace Git
