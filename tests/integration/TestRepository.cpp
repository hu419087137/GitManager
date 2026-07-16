#include "core/LibGit2Backend.h"
#include <git2.h>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class TestRepository : public QObject {
    Q_OBJECT

    static void configure(git_repository* repo)
    {
        git_config* config = nullptr;
        QCOMPARE(git_repository_config(&config, repo), 0);
        QCOMPARE(git_config_set_string(config, "user.name", "Test User"), 0);
        QCOMPARE(git_config_set_string(config, "user.email", "test@example.com"), 0);
        git_config_free(config);
    }

private slots:
    void initTestCase() { QVERIFY(git_libgit2_init() > 0); }
    void cleanupTestCase() { git_libgit2_shutdown(); }

    void statusCommitAndBranch()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo); git_repository_free(repo);

        QFile file(dir.filePath(QStringLiteral("中文 file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("one\n"); file.close();
        QVERIFY2(backend.stage(QStringLiteral("中文 file.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("initial"), false, false, &error), qPrintable(error));
        auto state = backend.snapshot(&error);
        QCOMPARE(state.commits.size(), 1);
        QVERIFY2(backend.createBranch(QStringLiteral("feature/login"), {}, &error), qPrintable(error));
        state = backend.snapshot(&error);
        bool found = false;
        for (const auto& branch : state.branches)
            if (branch.name == QStringLiteral("feature/login") && !branch.isRemote) found = true;
        QVERIFY(found);
    }

    void diffAndPatchRoundTrip()
    {
        QTemporaryDir dir; Git::LibGit2Backend backend; QString error;
        QVERIFY(backend.initialize(dir.path(), &error));
        git_repository* repo=nullptr; git_repository_open(&repo,dir.path().toUtf8().constData()); configure(repo); git_repository_free(repo);
        const QString path = QStringLiteral("中文 patch.txt");
        QFile file(dir.filePath(path)); file.open(QIODevice::WriteOnly); file.write("one\ntwo"); file.close();
        QVERIFY(backend.stage(path,&error)); QVERIFY(backend.commit("base",false,false,&error));
        file.open(QIODevice::WriteOnly|QIODevice::Truncate); file.write("ONE\ntwo"); file.close();
        const QString patch=backend.fileDiff(path,false,false,&error); QVERIFY(!patch.isEmpty());
        QVERIFY2(backend.applyPatch(patch,true,false,&error),qPrintable(error));
        auto state=backend.snapshot(&error); QVERIFY(state.files[0].isStaged());
        QVERIFY2(backend.applyPatch(patch,true,true,&error),qPrintable(error));
        state=backend.snapshot(&error); QVERIFY(!state.files[0].isStaged()); QVERIFY(state.files[0].isUnstaged());
    }

    void stashAndDiscard()
    {
        QTemporaryDir dir; Git::LibGit2Backend backend; QString error;
        QVERIFY(backend.initialize(dir.path(), &error));
        git_repository* repo=nullptr; git_repository_open(&repo,dir.path().toUtf8().constData()); configure(repo); git_repository_free(repo);
        QFile file(dir.filePath("a.txt")); file.open(QIODevice::WriteOnly); file.write("base\n"); file.close();
        QVERIFY(backend.stage("a.txt",&error)); QVERIFY(backend.commit("base",false,false,&error));
        file.open(QIODevice::WriteOnly|QIODevice::Truncate); file.write("change\n"); file.close();
        QVERIFY2(backend.stashPush("saved",true,&error),qPrintable(error));
        QCOMPARE(backend.stashes(&error).size(),1);
        QVERIFY(backend.stashApply(0,true,&error));
        QVERIFY(backend.discard("a.txt",&error));
    }

    void literalPathAndAbortSafety()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo); git_repository_free(repo);

        QFile literal(dir.filePath(QStringLiteral("file[1].txt")));
        QVERIFY(literal.open(QIODevice::WriteOnly));
        literal.write("base\n"); literal.close();
        QVERIFY(backend.stage(QStringLiteral("file[1].txt"), &error));
        QVERIFY(backend.commit(QStringLiteral("base"), false, false, &error));
        QVERIFY(literal.open(QIODevice::WriteOnly | QIODevice::Truncate));
        literal.write("changed\n"); literal.close();
        QVERIFY2(backend.discard(QStringLiteral("file[1].txt"), &error), qPrintable(error));
        QVERIFY(literal.open(QIODevice::ReadOnly));
        QCOMPARE(literal.readAll().replace("\r\n", "\n"), QByteArray("base\n"));

        QFile untracked(dir.filePath(QStringLiteral("keep-untracked.txt")));
        QVERIFY(untracked.open(QIODevice::WriteOnly));
        untracked.write("keep\n"); untracked.close();
        QVERIFY(!backend.abortOperation(QStringLiteral("merge"), &error));
        QVERIFY(QFileInfo::exists(untracked.fileName()));
    }

    void stagesAndUnstagesRenameAsOneChange()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo); git_repository_free(repo);

        QFile original(dir.filePath(QStringLiteral("old-name.txt")));
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("rename me\n"); original.close();
        QVERIFY(backend.stage(QStringLiteral("old-name.txt"), &error));
        QVERIFY(backend.commit(QStringLiteral("base"), false, false, &error));
        QVERIFY(QFile::rename(original.fileName(),
                              dir.filePath(QStringLiteral("new-name.txt"))));

        QVERIFY2(backend.stage(QStringLiteral("new-name.txt"), &error), qPrintable(error));
        auto state = backend.snapshot(&error);
        QCOMPARE(state.files.size(), 1);
        QCOMPARE(state.files[0].path, QStringLiteral("new-name.txt"));
        QCOMPARE(state.files[0].originalPath, QStringLiteral("old-name.txt"));
        QVERIFY(state.files[0].isStaged());

        QVERIFY2(backend.unstage(QStringLiteral("new-name.txt"), &error), qPrintable(error));
        state = backend.snapshot(&error);
        QCOMPARE(state.files.size(), 1);
        QCOMPARE(state.files[0].path, QStringLiteral("new-name.txt"));
        QCOMPARE(state.files[0].originalPath, QStringLiteral("old-name.txt"));
        QVERIFY(!state.files[0].isStaged());
        QVERIFY(state.files[0].isUnstaged());

        QVERIFY2(backend.discard(QStringLiteral("new-name.txt"), &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("old-name.txt"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("new-name.txt"))));
        QVERIFY(backend.snapshot(&error).files.isEmpty());
    }

    void pushUsesConfiguredUpstreamTarget()
    {
        QTemporaryDir localDir;
        QTemporaryDir remoteDir;
        QVERIFY(localDir.isValid());
        QVERIFY(remoteDir.isValid());

        git_repository* bare = nullptr;
        QCOMPARE(git_repository_init(&bare,
                                     remoteDir.path().toUtf8().constData(), 1), 0);
        git_repository_free(bare);

        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(localDir.path(), &error), qPrintable(error));
        git_repository* local = nullptr;
        QCOMPARE(git_repository_open(&local,
                                     localDir.path().toUtf8().constData()), 0);
        configure(local);
        git_repository_free(local);

        QFile file(localDir.filePath(QStringLiteral("push.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n"); file.close();
        QVERIFY(backend.stage(QStringLiteral("push.txt"), &error));
        QVERIFY(backend.commit(QStringLiteral("base"), false, false, &error));
        const QString baseBranch = backend.snapshot(&error).headName;
        QVERIFY(!baseBranch.isEmpty());
        QVERIFY(backend.addRemote(QStringLiteral("origin"), remoteDir.path(), &error));
        QVERIFY2(backend.push(QStringLiteral("origin"), baseBranch, true, &error),
                 qPrintable(error));

        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write("feature\n"); file.close();
        QVERIFY(backend.stage(QStringLiteral("push.txt"), &error));
        QVERIFY(backend.commit(QStringLiteral("feature"), false, false, &error));

        QCOMPARE(git_repository_open(&local,
                                     localDir.path().toUtf8().constData()), 0);
        git_reference* feature = nullptr;
        QCOMPARE(git_branch_lookup(&feature, local, "feature", GIT_BRANCH_LOCAL), 0);
        const QByteArray upstream = QStringLiteral("origin/%1")
            .arg(baseBranch).toUtf8();
        QCOMPARE(git_branch_set_upstream(feature, upstream.constData()), 0);
        git_reference_free(feature);
        git_repository_free(local);

        QVERIFY2(backend.push({}, {}, false, &error), qPrintable(error));

        QCOMPARE(git_repository_open(&bare,
                                     remoteDir.path().toUtf8().constData()), 0);
        git_oid remoteTarget;
        const QByteArray remoteRef = QStringLiteral("refs/heads/%1")
            .arg(baseBranch).toUtf8();
        QCOMPARE(git_reference_name_to_id(&remoteTarget, bare,
                                          remoteRef.constData()), 0);
        git_oid unexpected;
        QCOMPARE(git_reference_name_to_id(&unexpected, bare,
                                          "refs/heads/feature"), GIT_ENOTFOUND);
        git_repository_free(bare);

        QCOMPARE(git_repository_open(&local,
                                     localDir.path().toUtf8().constData()), 0);
        git_reference* head = nullptr;
        QCOMPARE(git_repository_head(&head, local), 0);
        QCOMPARE(git_oid_cmp(&remoteTarget, git_reference_target(head)), 0);
        git_reference_free(head);
        git_repository_free(local);
    }
};

QTEST_GUILESS_MAIN(TestRepository)
#include "TestRepository.moc"
