#include "core/LibGit2Backend.h"
#include <git2.h>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

class TestRepository : public QObject {
    Q_OBJECT

    struct FilterFixture {
        QString base;
        QString targetAdded;
        QString mainTip;
        QString featureTip;
        qint64 baseTime {0};
        qint64 targetTime {0};
        qint64 mainTipTime {0};
        qint64 featureTime {0};
    };

    static QString oidText(const git_oid* oid)
    {
        char value[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(value, sizeof(value), oid);
        return QString::fromLatin1(value);
    }

    static bool checkGit(int result, const QString& action, QString* error)
    {
        if (result >= 0)
            return true;
        const git_error* last = git_error_last();
        if (error) {
            *error = last && last->message
                ? QStringLiteral("%1: %2")
                      .arg(action, QString::fromUtf8(last->message))
                : action;
        }
        return false;
    }

    static bool createTree(git_repository* repo,
                           const QMap<QString, QByteArray>& files,
                           git_oid* treeOid, QString* error)
    {
        git_treebuilder* builder = nullptr;
        if (!checkGit(git_treebuilder_new(&builder, repo, nullptr),
                      QStringLiteral("Cannot create test tree"), error)) {
            return false;
        }
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            git_oid blobOid;
            if (!checkGit(git_blob_create_frombuffer(
                              &blobOid, repo, it.value().constData(),
                              static_cast<size_t>(it.value().size())),
                          QStringLiteral("Cannot create test blob"), error)) {
                git_treebuilder_free(builder);
                return false;
            }
            const QByteArray path = it.key().toUtf8();
            if (!checkGit(git_treebuilder_insert(nullptr, builder,
                                                 path.constData(), &blobOid,
                                                 GIT_FILEMODE_BLOB),
                          QStringLiteral("Cannot add test tree entry"), error)) {
                git_treebuilder_free(builder);
                return false;
            }
        }
        const bool written = checkGit(git_treebuilder_write(treeOid, builder),
                                      QStringLiteral("Cannot write test tree"), error);
        git_treebuilder_free(builder);
        return written;
    }

    static bool createCommit(git_repository* repo, const git_oid& treeOid,
                             const git_oid* parentOid, const QString& authorName,
                             const QString& authorEmail, qint64 timestamp,
                             const QString& message, git_oid* commitOid,
                             QString* error)
    {
        git_tree* tree = nullptr;
        if (!checkGit(git_tree_lookup(&tree, repo, &treeOid),
                      QStringLiteral("Cannot open test tree"), error)) {
            return false;
        }

        git_commit* parent = nullptr;
        if (parentOid
            && !checkGit(git_commit_lookup(&parent, repo, parentOid),
                         QStringLiteral("Cannot open test parent"), error)) {
            git_tree_free(tree);
            return false;
        }

        git_signature* signature = nullptr;
        const QByteArray name = authorName.toUtf8();
        const QByteArray email = authorEmail.toUtf8();
        if (!checkGit(git_signature_new(&signature, name.constData(),
                                        email.constData(), timestamp, 0),
                      QStringLiteral("Cannot create test signature"), error)) {
            git_commit_free(parent);
            git_tree_free(tree);
            return false;
        }

        const QByteArray commitMessage = message.toUtf8();
        const git_commit* parents[] = {parent};
        const int result = git_commit_create(
            commitOid, repo, nullptr, signature, signature, "UTF-8",
            commitMessage.constData(), tree, parent ? 1 : 0,
            parent ? parents : nullptr);
        git_signature_free(signature);
        git_commit_free(parent);
        git_tree_free(tree);
        return checkGit(result, QStringLiteral("Cannot create test commit"), error);
    }

    static bool updateBranch(git_repository* repo, const char* name,
                             const git_oid& target, bool makeHead,
                             QString* error)
    {
        git_reference* reference = nullptr;
        if (!checkGit(git_reference_create(&reference, repo, name, &target, 1,
                                           "history test"),
                      QStringLiteral("Cannot update test branch"), error)) {
            return false;
        }
        git_reference_free(reference);
        if (!makeHead)
            return true;
        return checkGit(git_repository_set_head(repo, name),
                        QStringLiteral("Cannot set test HEAD"), error);
    }

    static bool createEmptyCommitChain(git_repository* repo, int count,
                                       QVector<QString>* hashes, QString* error)
    {
        git_oid treeOid;
        if (!createTree(repo, {}, &treeOid, error))
            return false;

        git_oid parentOid;
        bool hasParent = false;
        hashes->clear();
        hashes->reserve(count);
        for (int index = 0; index < count; ++index) {
            git_oid commitOid;
            if (!createCommit(repo, treeOid, hasParent ? &parentOid : nullptr,
                              QStringLiteral("Bulk Author"),
                              QStringLiteral("bulk@example.com"),
                              1700000000 + index,
                              QStringLiteral("bulk-%1").arg(index),
                              &commitOid, error)) {
                return false;
            }
            hashes->append(oidText(&commitOid));
            parentOid = commitOid;
            hasParent = true;
        }
        return !hasParent
            || updateBranch(repo, "refs/heads/main", parentOid, true, error);
    }

    static bool createFilterFixture(git_repository* repo,
                                    FilterFixture* fixture, QString* error)
    {
        fixture->baseTime = 1700000000;
        fixture->targetTime = fixture->baseTime + 60;
        fixture->mainTipTime = fixture->baseTime + 120;
        fixture->featureTime = fixture->baseTime + 180;

        git_oid baseTree;
        git_oid targetTree;
        git_oid mainTipTree;
        git_oid featureTree;
        if (!createTree(repo, {{QStringLiteral("common.txt"), QByteArray("base\n")}},
                        &baseTree, error)
            || !createTree(repo,
                           {{QStringLiteral("common.txt"), QByteArray("base\n")},
                             {QStringLiteral("target[1].txt"), QByteArray("one\n")}},
                           &targetTree, error)
            || !createTree(repo,
                           {{QStringLiteral("common.txt"), QByteArray("base\n")},
                            {QStringLiteral("other.txt"), QByteArray("other\n")},
                             {QStringLiteral("target[1].txt"), QByteArray("one\n")}},
                           &mainTipTree, error)
            || !createTree(repo,
                           {{QStringLiteral("common.txt"), QByteArray("base\n")},
                             {QStringLiteral("target[1].txt"), QByteArray("feature\n")}},
                           &featureTree, error)) {
            return false;
        }

        git_oid base;
        git_oid targetAdded;
        git_oid mainTip;
        git_oid featureTip;
        if (!createCommit(repo, baseTree, nullptr, QStringLiteral("Root User"),
                          QStringLiteral("root@example.com"), fixture->baseTime,
                          QStringLiteral("base commit"), &base, error)
            || !createCommit(repo, targetTree, &base, QStringLiteral("Alice Chen"),
                             QStringLiteral("alice@example.com"),
                             fixture->targetTime,
                             QStringLiteral("Needle: add target"),
                             &targetAdded, error)
            || !createCommit(repo, mainTipTree, &targetAdded,
                             QStringLiteral("Bob Li"),
                             QStringLiteral("bob@example.com"),
                             fixture->mainTipTime,
                             QStringLiteral("update unrelated file"),
                             &mainTip, error)
            || !createCommit(repo, featureTree, &targetAdded,
                             QStringLiteral("Carol Wu"),
                             QStringLiteral("carol@example.com"),
                             fixture->featureTime,
                             QStringLiteral("feature target change"),
                             &featureTip, error)
            || !updateBranch(repo, "refs/heads/main", mainTip, true, error)
            || !updateBranch(repo, "refs/heads/feature", featureTip, false, error)) {
            return false;
        }

        fixture->base = oidText(&base);
        fixture->targetAdded = oidText(&targetAdded);
        fixture->mainTip = oidText(&mainTip);
        fixture->featureTip = oidText(&featureTip);
        return true;
    }

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
        error.clear();
        auto state = backend.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        Git::CommitHistoryQuery historyQuery;
        const auto history = backend.commitHistory(historyQuery, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(history.commits.size(), 1);
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

    void commitHistoryPaginatesBeyondOneThousandCommits()
    {
        constexpr int commitCount = 1005;
        constexpr int pageSize = 200;
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));

        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        QVector<QString> expectedHashes;
        QVERIFY2(createEmptyCommitChain(repo, commitCount, &expectedHashes, &error),
                 qPrintable(error));
        git_repository_free(repo);

        QSet<QString> seen;
        QString refsVersion;
        int offset = 0;
        int pageCount = 0;
        while (true) {
            Git::CommitHistoryQuery query;
            query.offset = offset;
            query.limit = pageSize;
            query.expectedRefsVersion = refsVersion;
            error.clear();
            const auto page = backend.commitHistory(query, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(page.offset, offset);
            QVERIFY(!page.resetRequired);
            if (refsVersion.isEmpty())
                refsVersion = page.refsVersion;
            else
                QCOMPARE(page.refsVersion, refsVersion);

            for (const Git::Commit& commit : page.commits) {
                QVERIFY2(!seen.contains(commit.hash),
                         qPrintable(QStringLiteral("Duplicate commit %1")
                                        .arg(commit.hash)));
                seen.insert(commit.hash);
            }
            offset += page.commits.size();
            ++pageCount;
            QVERIFY2(pageCount <= (commitCount + pageSize - 1) / pageSize,
                     "Pagination did not make progress");
            if (!page.hasMore)
                break;
        }

        QCOMPARE(pageCount, 6);
        QCOMPARE(offset, commitCount);
        QCOMPARE(seen.size(), commitCount);
        for (const QString& hash : expectedHashes)
            QVERIFY2(seen.contains(hash), qPrintable(QStringLiteral("Missing %1").arg(hash)));

        Git::CommitHistoryQuery exactBoundary;
        exactBoundary.offset = commitCount - pageSize;
        exactBoundary.limit = pageSize;
        exactBoundary.expectedRefsVersion = refsVersion;
        const auto exactPage = backend.commitHistory(exactBoundary, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(exactPage.commits.size(), pageSize);
        QVERIFY(!exactPage.hasMore);

        Git::CommitHistoryQuery oneExtra = exactBoundary;
        --oneExtra.offset;
        const auto extraPage = backend.commitHistory(oneExtra, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(extraPage.commits.size(), pageSize);
        QVERIFY(extraPage.hasMore);
    }

    void commitHistoryResetsAfterReferenceChange()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        QVector<QString> hashes;
        QVERIFY2(createEmptyCommitChain(repo, 5, &hashes, &error), qPrintable(error));

        Git::CommitHistoryQuery firstQuery;
        firstQuery.limit = 2;
        const auto firstPage = backend.commitHistory(firstQuery, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(firstPage.commits.size(), 2);
        QVERIFY(firstPage.hasMore);
        QVERIFY(!firstPage.refsVersion.isEmpty());

        git_oid sideTarget;
        QCOMPARE(git_oid_fromstr(&sideTarget, hashes.first().toLatin1().constData()), 0);
        QVERIFY2(updateBranch(repo, "refs/heads/side", sideTarget, false, &error),
                 qPrintable(error));
        git_repository_free(repo);

        Git::CommitHistoryQuery continued;
        continued.offset = 2;
        continued.limit = 2;
        continued.expectedRefsVersion = firstPage.refsVersion;
        error.clear();
        const auto resetPage = backend.commitHistory(continued, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(resetPage.resetRequired);
        QCOMPARE(resetPage.offset, 0);
        QVERIFY(resetPage.refsVersion != firstPage.refsVersion);
        QCOMPARE(resetPage.commits.size(), 2);
        QCOMPARE(resetPage.commits.first().hash, firstPage.commits.first().hash);
    }

    void headSwitchChangesReferenceVersionAtSameCommit()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        QVector<QString> hashes;
        QVERIFY2(createEmptyCommitChain(repo, 1, &hashes, &error), qPrintable(error));
        git_oid target;
        QCOMPARE(git_oid_fromstr(&target, hashes.first().toLatin1().constData()), 0);
        QVERIFY2(updateBranch(repo, "refs/heads/side", target, false, &error),
                 qPrintable(error));

        Git::CommitHistoryQuery query;
        const auto mainPage = backend.commitHistory(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(mainPage.commits.size(), 1);
        QVERIFY(mainPage.commits.first().refs.contains(QStringLiteral("HEAD -> main")));

        QCOMPARE(git_repository_set_head(repo, "refs/heads/side"), 0);
        git_repository_free(repo);
        error.clear();
        const auto sidePage = backend.commitHistory(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(sidePage.commits.size(), 1);
        QVERIFY(sidePage.refsVersion != mainPage.refsVersion);
        QVERIFY(sidePage.commits.first().refs.contains(QStringLiteral("HEAD -> side")));
        QVERIFY(!sidePage.commits.first().refs.contains(QStringLiteral("HEAD -> main")));
    }

    void commitHistoryFilters()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        FilterFixture fixture;
        QVERIFY2(createFilterFixture(repo, &fixture, &error), qPrintable(error));
        git_repository_free(repo);

        Git::CommitHistoryQuery search;
        search.searchText = QStringLiteral("nEeDlE");
        auto page = backend.commitHistory(search, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().hash, fixture.targetAdded);

        Git::CommitHistoryQuery author;
        author.author = QStringLiteral("ALICE@EXAMPLE.COM");
        page = backend.commitHistory(author, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().authorName, QStringLiteral("Alice Chen"));

        author.author = QStringLiteral("alice chen");
        page = backend.commitHistory(author, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().hash, fixture.targetAdded);

        Git::CommitHistoryQuery dates;
        dates.fromDate = QDateTime::fromSecsSinceEpoch(fixture.targetTime, Qt::UTC);
        dates.toDate = dates.fromDate;
        page = backend.commitHistory(dates, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().hash, fixture.targetAdded);

        Git::CommitHistoryQuery feature;
        feature.branch = QStringLiteral("feature");
        page = backend.commitHistory(feature, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 3);
        QCOMPARE(page.commits.first().hash, fixture.featureTip);

        Git::CommitHistoryQuery allBranches;
        allBranches.branch = QStringLiteral("*");
        page = backend.commitHistory(allBranches, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 4);
        QSet<QString> allHashes;
        bool sharedParentLaneClosed = false;
        for (const Git::Commit& commit : page.commits) {
            allHashes.insert(commit.hash);
            if (commit.hash == fixture.targetAdded) {
                sharedParentLaneClosed = true;
                QVERIFY(!commit.activeLanes.contains(commit.hash));
            }
        }
        QVERIFY(allHashes.contains(fixture.mainTip));
        QVERIFY(allHashes.contains(fixture.featureTip));
        QVERIFY(sharedParentLaneClosed);

        Git::CommitHistoryQuery path;
#ifdef Q_OS_WIN
        path.path = QStringLiteral(".\\target[1].txt");
#else
        path.path = QStringLiteral("./target[1].txt");
#endif
        page = backend.commitHistory(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().hash, fixture.targetAdded);
    }

    void commitHistoryIsEmptyForUnbornRepository()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        Git::CommitHistoryQuery query;
        const auto page = backend.commitHistory(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(page.commits.isEmpty());
        QVERIFY(!page.hasMore);
        QVERIFY(!page.resetRequired);
        QCOMPARE(page.offset, 0);
        QVERIFY(!page.refsVersion.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRepository)
#include "TestRepository.moc"
