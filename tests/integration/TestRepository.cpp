#include "core/GitCommandRunner.h"
#include "core/parsers/StatusParser.h"
#include "core/parsers/BranchParser.h"
#include <QProcess>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class TestRepository : public QObject {
    Q_OBJECT

    static QByteArray git(const QString& directory, const QStringList& args)
    {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(QStringLiteral("git"), args);
        if (!process.waitForFinished(10000)) return {};
        return process.readAllStandardOutput();
    }

private slots:
    void temporaryRepositoryStatus()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        git(dir.path(), {"init", "-b", "feature/login"});
        QFile file(dir.filePath(QStringLiteral("中文 file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one\n"); file.close();
        git(dir.path(), {"add", "--", "中文 file.txt"});
        file.open(QIODevice::Append); file.write("two\n"); file.close();
        const QByteArray output = git(dir.path(), {"-c", "core.quotepath=false", "status",
            "--porcelain=v2", "-z", "--branch", "--untracked-files=all"});
        const auto status = Git::StatusParser::parse(output);
        QVERIFY(status.unborn);
        QCOMPARE(status.headName, QString("feature/login"));
        QCOMPARE(status.files.size(), 1);
        QVERIFY(status.files[0].isStaged());
        QVERIFY(status.files[0].isUnstaged());

        // unborn 分支尚无 ref；提交后验证带斜杠的本地分支分类。
        git(dir.path(), {"config", "user.name", "Test User"});
        git(dir.path(), {"config", "user.email", "test@example.com"});
        git(dir.path(), {"commit", "-m", "initial"});
        const auto branches = Git::BranchParser::parse(git(dir.path(), {"for-each-ref",
            "--format=%(refname)%09%(refname:short)%09%(objectname:short)%09%(upstream:short)%09%(upstream:track,nobracket)%00",
            "refs/heads", "refs/remotes"}), QString("feature/login"));
        QCOMPARE(branches.size(), 1);
        QVERIFY(!branches[0].isRemote);
        QVERIFY(branches[0].isCurrent);
    }

    void detachedHeadAndStashConflict()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        git(dir.path(), {"init", "-b", "main"});
        git(dir.path(), {"config", "user.name", "Test User"});
        git(dir.path(), {"config", "user.email", "test@example.com"});
        QFile file(dir.filePath(QStringLiteral("conflict.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n"); file.close();
        git(dir.path(), {"add", "conflict.txt"});
        git(dir.path(), {"commit", "-m", "base"});
        git(dir.path(), {"checkout", "--detach"});
        auto output = git(dir.path(), {"status", "--porcelain=v2", "-z", "--branch"});
        auto status = Git::StatusParser::parse(output);
        QVERIFY(status.detached);
        git(dir.path(), {"checkout", "main"});

        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write("stashed\n"); file.close();
        git(dir.path(), {"stash", "push", "-m", "test stash"});
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write("committed\n"); file.close();
        git(dir.path(), {"add", "conflict.txt"});
        git(dir.path(), {"commit", "-m", "conflicting change"});
        git(dir.path(), {"stash", "apply"});
        output = git(dir.path(), {"status", "--porcelain=v2", "-z", "--branch"});
        status = Git::StatusParser::parse(output);
        QCOMPARE(status.files.size(), 1);
        QVERIFY(status.files[0].conflicted);
    }

    void runnerReportsFailureAndTimeout()
    {
        QTemporaryDir dir;
        Git::GitCommandRunner runner;
        QSignalSpy spy(&runner, &Git::GitCommandRunner::sigFinished);
        runner.enqueue("failure", dir.path(), {"not-a-real-command"});
        QVERIFY(spy.wait(5000));
        auto result = qvariant_cast<Git::GitResult>(spy.takeFirst().at(0));
        QCOMPARE(result.error, Git::GitError::NonZeroExit);

        runner.enqueue("timeout", dir.path(), {"hash-object", "--stdin"}, false, 50);
        QVERIFY(spy.wait(5000));
        result = qvariant_cast<Git::GitResult>(spy.takeFirst().at(0));
        QCOMPARE(result.error, Git::GitError::TimedOut);
    }

    void runnerCanBeCancelled()
    {
        QTemporaryDir dir;
        Git::GitCommandRunner runner;
        QSignalSpy spy(&runner, &Git::GitCommandRunner::sigFinished);
        runner.enqueue("cancel", dir.path(), {"hash-object", "--stdin"}, false, 5000);
        QTimer::singleShot(50, &runner, &Git::GitCommandRunner::cancelCurrent);
        QVERIFY(spy.wait(5000));
        const auto result = qvariant_cast<Git::GitResult>(spy.takeFirst().at(0));
        QCOMPARE(result.error, Git::GitError::Cancelled);
    }
};

QTEST_GUILESS_MAIN(TestRepository)
#include "TestRepository.moc"
