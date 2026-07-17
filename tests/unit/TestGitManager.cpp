#include "core/GitManager.h"

#include <git2.h>
#include <QDir>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestGitManager : public QObject {
    Q_OBJECT

    static bool checkGit(int result, const QString& action, QString* error)
    {
        if (result >= 0)
            return true;
        const git_error* last = git_error_last();
        if (error) {
            *error = last && last->message
                ? QStringLiteral("%1: %2").arg(action,
                                                QString::fromUtf8(last->message))
                : action;
        }
        return false;
    }

    static QString oidText(const git_oid* oid)
    {
        char value[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(value, sizeof(value), oid);
        return QString::fromLatin1(value);
    }

    static bool createRepository(const QString& path, int commitCount,
                                 const QString& subjectPrefix,
                                 QVector<QString>* hashes, QString* error)
    {
        if (hashes) {
            hashes->clear();
            hashes->reserve(commitCount);
        }

        git_repository* repository = nullptr;
        const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
        if (!checkGit(git_repository_init(&repository, nativePath.constData(), 0),
                      QStringLiteral("Cannot initialize test repository"), error)) {
            return false;
        }
        if (commitCount == 0) {
            git_repository_free(repository);
            return true;
        }

        git_treebuilder* builder = nullptr;
        if (!checkGit(git_treebuilder_new(&builder, repository, nullptr),
                      QStringLiteral("Cannot create test tree"), error)) {
            git_repository_free(repository);
            return false;
        }
        git_oid treeOid;
        const bool treeWritten = checkGit(
            git_treebuilder_write(&treeOid, builder),
            QStringLiteral("Cannot write test tree"), error);
        git_treebuilder_free(builder);
        if (!treeWritten) {
            git_repository_free(repository);
            return false;
        }

        git_tree* tree = nullptr;
        if (!checkGit(git_tree_lookup(&tree, repository, &treeOid),
                      QStringLiteral("Cannot open test tree"), error)) {
            git_repository_free(repository);
            return false;
        }

        git_oid parentOid;
        bool hasParent = false;
        for (int index = 0; index < commitCount; ++index) {
            git_commit* parent = nullptr;
            if (hasParent
                && !checkGit(git_commit_lookup(&parent, repository, &parentOid),
                             QStringLiteral("Cannot open test parent"), error)) {
                git_tree_free(tree);
                git_repository_free(repository);
                return false;
            }

            git_signature* signature = nullptr;
            if (!checkGit(git_signature_new(&signature, "History Author",
                                            "history@example.com",
                                            1700000000 + index, 0),
                          QStringLiteral("Cannot create test signature"), error)) {
                git_commit_free(parent);
                git_tree_free(tree);
                git_repository_free(repository);
                return false;
            }

            const QString subject = QStringLiteral("%1 commit %2")
                                        .arg(subjectPrefix)
                                        .arg(index, 4, 10, QLatin1Char('0'));
            const QByteArray message = subject.toUtf8();
            const git_commit* parents[] = {parent};
            git_oid commitOid;
            const int result = git_commit_create(
                &commitOid, repository, nullptr, signature, signature, "UTF-8",
                message.constData(), tree, parent ? 1 : 0,
                parent ? parents : nullptr);
            git_signature_free(signature);
            git_commit_free(parent);
            if (!checkGit(result, QStringLiteral("Cannot create test commit"), error)) {
                git_tree_free(tree);
                git_repository_free(repository);
                return false;
            }

            parentOid = commitOid;
            hasParent = true;
            if (hashes)
                hashes->append(oidText(&commitOid));
        }
        git_tree_free(tree);

        git_reference* branch = nullptr;
        const bool branchCreated = checkGit(
            git_reference_create(&branch, repository, "refs/heads/main",
                                 &parentOid, 1, "GitManager test history"),
            QStringLiteral("Cannot create test branch"), error);
        git_reference_free(branch);
        const bool headSet = branchCreated
            && checkGit(git_repository_set_head(repository, "refs/heads/main"),
                        QStringLiteral("Cannot set test HEAD"), error);
        git_repository_free(repository);
        return headSet;
    }

    static Git::CommitHistoryPage pageAt(const QSignalSpy& spy, int index)
    {
        return qvariant_cast<Git::CommitHistoryPage>(spy.at(index).at(0));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(git_libgit2_init() > 0);
        qRegisterMetaType<Git::CommitHistoryPage>();
    }

    void cleanupTestCase()
    {
        git_libgit2_shutdown();
    }

    void loadsFirstPageAndContinuesWithoutDuplicates()
    {
        constexpr int commitCount = 250;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> expectedHashes;
        QVERIFY2(createRepository(directory.path(), commitCount,
                                  QStringLiteral("page"), &expectedHashes, &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy openedSpy(&manager, &Git::GitManager::sigRepositoryOpened);
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        QVERIFY(openedSpy.isValid());
        QVERIFY(historySpy.isValid());

        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 15000);
        QVERIFY(openedSpy.at(0).at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        const Git::CommitHistoryPage first = pageAt(historySpy, 0);
        QCOMPARE(first.offset, 0);
        QCOMPARE(first.commits.size(), 200);
        QVERIFY(first.hasMore);
        QVERIFY(!first.resetRequired);
        QVERIFY(!first.refsVersion.isEmpty());

        manager.loadMoreCommits();
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 2, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        const Git::CommitHistoryPage second = pageAt(historySpy, 1);
        QCOMPARE(second.offset, 200);
        QCOMPARE(second.commits.size(), commitCount - 200);
        QVERIFY(!second.hasMore);
        QVERIFY(!second.resetRequired);
        QCOMPARE(second.refsVersion, first.refsVersion);

        QSet<QString> seen;
        for (const Git::Commit& commit : first.commits) {
            QVERIFY2(!seen.contains(commit.hash), qPrintable(commit.hash));
            seen.insert(commit.hash);
        }
        for (const Git::Commit& commit : second.commits) {
            QVERIFY2(!seen.contains(commit.hash), qPrintable(commit.hash));
            seen.insert(commit.hash);
        }
        QCOMPARE(seen.size(), commitCount);
        for (const QString& hash : expectedHashes)
            QVERIFY2(seen.contains(hash), qPrintable(hash));
        QCOMPARE(errorSpy.count(), 0);
    }

    void latestQuerySuppressesPreviousResult()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY2(createRepository(directory.path(), 250, QStringLiteral("query"),
                                  nullptr, &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        historySpy.clear();
        errorSpy.clear();

        Git::CommitHistoryQuery queryA;
        queryA.searchText = QStringLiteral("query commit 0017");
        Git::CommitHistoryQuery queryB;
        queryB.searchText = QStringLiteral("query commit 0233");

        bool replaced = false;
        const QMetaObject::Connection connection = connect(
            &manager, &Git::GitManager::sigCommandStarted, &manager,
            [&](const QString& command) {
                if (replaced || command != QStringLiteral("libgit2: history"))
                    return;
                replaced = true;
                manager.setCommitHistoryQuery(queryB);
            });

        manager.setCommitHistoryQuery(queryA);
        QVERIFY(replaced);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        disconnect(connection);

        QCOMPARE(historySpy.count(), 1);
        const Git::CommitHistoryPage page = pageAt(historySpy, 0);
        QCOMPARE(page.offset, 0);
        QCOMPARE(page.commits.size(), 1);
        QCOMPARE(page.commits.first().subject,
                 QStringLiteral("query commit 0233"));
        QVERIFY(!page.hasMore);
        QCOMPARE(errorSpy.count(), 0);
    }

    void latestDiffRequestWinsWithoutRefreshingState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> hashes;
        QVERIFY2(createRepository(directory.path(), 250, QStringLiteral("diff"),
                                  &hashes, &error), qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        QSignalSpy diffSpy(&manager, &Git::GitManager::sigDiffReady);
        QSignalSpy stateSpy(&manager, &Git::GitManager::sigStateReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        bool replaced = false;
        connect(&manager, &Git::GitManager::sigCommandStarted, &manager,
                [&](const QString& command) {
            if (replaced || command != QStringLiteral("libgit2: diff-commit"))
                return;
            replaced = true;
            manager.fetchCommitDiff(hashes.last());
        });

        manager.fetchCommitDiff(hashes.first());
        QVERIFY(replaced);
        QTRY_COMPARE_WITH_TIMEOUT(diffSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        QTest::qWait(100);
        QCOMPARE(diffSpy.first().at(1).toString(), hashes.last().left(8));
        QCOMPARE(stateSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 0);
    }

    void refreshReloadsHistoryOnlyWhenReferencesChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> hashes;
        QVERIFY2(createRepository(directory.path(), 3, QStringLiteral("refresh"),
                                  &hashes, &error), qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy stateSpy(&manager, &Git::GitManager::sigStateReady);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        historySpy.clear();
        stateSpy.clear();

        manager.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        QCOMPARE(historySpy.count(), 0);

        git_repository* repository = nullptr;
        const QByteArray path = QDir::toNativeSeparators(directory.path()).toUtf8();
        QCOMPARE(git_repository_open(&repository, path.constData()), 0);
        git_oid target;
        QCOMPARE(git_oid_fromstr(&target, hashes.first().toLatin1().constData()), 0);
        git_reference* side = nullptr;
        QCOMPARE(git_reference_create(&side, repository, "refs/heads/side",
                                      &target, 0, "refresh test"), 0);
        git_reference_free(side);
        git_repository_free(repository);

        stateSpy.clear();
        manager.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        bool foundSide = false;
        for (const Git::Commit& commit : pageAt(historySpy, 0).commits)
            foundSide = foundSide || commit.refs.contains(QStringLiteral("side"));
        QVERIFY(foundSide);
    }

    void repositorySwitchSuppressesPreviousHistory()
    {
        QTemporaryDir directoryA;
        QTemporaryDir directoryB;
        QVERIFY(directoryA.isValid());
        QVERIFY(directoryB.isValid());
        QString error;
        QVERIFY2(createRepository(directoryA.path(), 250,
                                  QStringLiteral("repository-a"), nullptr, &error),
                 qPrintable(error));
        error.clear();
        QVERIFY2(createRepository(directoryB.path(), 3,
                                  QStringLiteral("repository-b"), nullptr, &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy openedSpy(&manager, &Git::GitManager::sigRepositoryOpened);
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);

        bool switched = false;
        const QMetaObject::Connection connection = connect(
            &manager, &Git::GitManager::sigCommandStarted, &manager,
            [&](const QString& command) {
                if (switched || command != QStringLiteral("libgit2: history"))
                    return;
                switched = true;
                manager.openRepository(directoryB.path());
            });

        manager.openRepository(directoryA.path());
        QTRY_VERIFY_WITH_TIMEOUT(switched, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        disconnect(connection);

        QVERIFY(openedSpy.at(0).at(1).toBool());
        QVERIFY(openedSpy.at(1).at(1).toBool());
        QCOMPARE(QDir::cleanPath(manager.repositoryPath()),
                 QDir::cleanPath(directoryB.path()));
        QCOMPARE(historySpy.count(), 1);
        const Git::CommitHistoryPage page = pageAt(historySpy, 0);
        QCOMPARE(page.commits.size(), 3);
        for (const Git::Commit& commit : page.commits)
            QVERIFY2(commit.subject.startsWith(QStringLiteral("repository-b commit")),
                     qPrintable(commit.subject));
        QVERIFY(!page.hasMore);
        QCOMPARE(errorSpy.count(), 0);
    }

    void historyOpenFailureClearsLoadingState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY2(createRepository(directory.path(), 1, QStringLiteral("failure"),
                                  nullptr, &error), qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy loadingSpy(&manager, &Git::GitManager::sigCommitHistoryLoading);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        bool repositoryHidden = false;
        connect(&manager, &Git::GitManager::sigCommandStarted, &manager,
                [&](const QString& command) {
            if (repositoryHidden || command != QStringLiteral("libgit2: history"))
                return;
            repositoryHidden = QDir().rename(directory.filePath(QStringLiteral(".git")),
                                              directory.filePath(QStringLiteral(".git-hidden")));
        });

        manager.openRepository(directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(repositoryHidden, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!errorSpy.isEmpty(), 15000);
        QVERIFY(historySpy.isEmpty());
        QVERIFY(loadingSpy.size() >= 2);
        QCOMPARE(loadingSpy.last().at(0).toBool(), false);
    }

    void cancellingHistoryDoesNotRestartIt()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY2(createRepository(directory.path(), 250, QStringLiteral("cancel"),
                                  nullptr, &error), qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy loadingSpy(&manager, &Git::GitManager::sigCommitHistoryLoading);
        int historyStarts = 0;
        connect(&manager, &Git::GitManager::sigCommandStarted, &manager,
                [&](const QString& command) {
            if (command != QStringLiteral("libgit2: history"))
                return;
            ++historyStarts;
            if (historyStarts == 1)
                manager.cancelAll();
        });

        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historyStarts, 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        QTest::qWait(200);
        QCOMPARE(historyStarts, 1);
        QVERIFY(historySpy.isEmpty());
        QVERIFY(!loadingSpy.isEmpty());
        QCOMPARE(loadingSpy.last().at(0).toBool(), false);
    }

    void cancelledRefreshRetriesAndPublishesState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> hashes;
        QVERIFY2(createRepository(directory.path(), 3,
                                  QStringLiteral("refresh-cancel"), &hashes,
                                  &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager,
                              &Git::GitManager::sigCommitHistoryReady);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        QSignalSpy stateSpy(&manager, &Git::GitManager::sigStateReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        int refreshStarts = 0;
        const QMetaObject::Connection connection = connect(
            &manager, &Git::GitManager::sigCommandStarted, &manager,
            [&](const QString& command) {
                if (command != QStringLiteral("libgit2: refresh"))
                    return;
                ++refreshStarts;
                if (refreshStarts == 1)
                    manager.cancelAll();
            });

        manager.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(refreshStarts, 2, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        disconnect(connection);

        QCOMPARE(refreshStarts, 2);
        QCOMPARE(qvariant_cast<Git::RepositoryState>(stateSpy.first().at(0)).headHash,
                 hashes.last());
        QCOMPARE(errorSpy.count(), 0);
    }

    void latestPreviewRequestWinsAcrossPreviewTypes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> hashes;
        QVERIFY2(createRepository(directory.path(), 4,
                                  QStringLiteral("preview-latest"), &hashes,
                                  &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager,
                              &Git::GitManager::sigCommitHistoryReady);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        QSignalSpy resetPreviewSpy(&manager,
                                   &Git::GitManager::sigResetPreviewReady);
        QSignalSpy rebasePlanSpy(&manager,
                                 &Git::GitManager::sigRebasePlanReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        int previewStarts = 0;
        bool replaced = false;
        const QMetaObject::Connection connection = connect(
            &manager, &Git::GitManager::sigCommandStarted, &manager,
            [&](const QString& command) {
                if (!command.startsWith(QStringLiteral("libgit2: preview-")))
                    return;
                ++previewStarts;
                if (replaced
                    || command != QStringLiteral("libgit2: preview-reset")) {
                    return;
                }
                replaced = true;
                manager.requestRebasePlan(hashes.at(1));
            });

        manager.requestResetPreview(hashes.first());
        QVERIFY(replaced);
        QTRY_COMPARE_WITH_TIMEOUT(rebasePlanSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        disconnect(connection);

        QCOMPARE(previewStarts, 2);
        QCOMPARE(resetPreviewSpy.count(), 0);
        const Git::RebasePlan plan =
            qvariant_cast<Git::RebasePlan>(rebasePlanSpy.first().at(0));
        QCOMPARE(plan.preview.expectedHead, hashes.last());
        QCOMPARE(plan.preview.targetHash, hashes.at(1));
        QCOMPARE(plan.items.size(), 2);
        QCOMPARE(errorSpy.count(), 0);
    }

    void repositorySwitchSuppressesPreviousPreview()
    {
        QTemporaryDir directoryA;
        QTemporaryDir directoryB;
        QVERIFY(directoryA.isValid());
        QVERIFY(directoryB.isValid());
        QString error;
        QVector<QString> hashesA;
        QVector<QString> hashesB;
        QVERIFY2(createRepository(directoryA.path(), 4,
                                  QStringLiteral("preview-repository-a"),
                                  &hashesA, &error),
                 qPrintable(error));
        error.clear();
        QVERIFY2(createRepository(directoryB.path(), 3,
                                  QStringLiteral("preview-repository-b"),
                                  &hashesB, &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager,
                              &Git::GitManager::sigCommitHistoryReady);
        manager.openRepository(directoryA.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        historySpy.clear();

        QSignalSpy openedSpy(&manager, &Git::GitManager::sigRepositoryOpened);
        QSignalSpy previewSpy(&manager,
                              &Git::GitManager::sigResetPreviewReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        bool switched = false;
        const QMetaObject::Connection connection = connect(
            &manager, &Git::GitManager::sigCommandStarted, &manager,
            [&](const QString& command) {
                if (switched
                    || command != QStringLiteral("libgit2: preview-reset")) {
                    return;
                }
                switched = true;
                manager.openRepository(directoryB.path());
            });

        manager.requestResetPreview(hashesA.first());
        QVERIFY(switched);
        QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        disconnect(connection);

        QCOMPARE(QDir::cleanPath(manager.repositoryPath()),
                 QDir::cleanPath(directoryB.path()));
        QCOMPARE(previewSpy.count(), 0);

        manager.requestResetPreview(hashesB.first());
        QTRY_COMPARE_WITH_TIMEOUT(previewSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        const Git::HistoryRewritePreview preview =
            qvariant_cast<Git::HistoryRewritePreview>(previewSpy.first().at(0));
        QCOMPARE(preview.expectedHead, hashesB.last());
        QCOMPARE(preview.targetHash, hashesB.first());
        QCOMPARE(errorSpy.count(), 0);
    }

    void previewsAndExecutesValidatedReset()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVector<QString> hashes;
        QVERIFY2(createRepository(directory.path(), 3, QStringLiteral("rewrite"),
                                  &hashes, &error), qPrintable(error));

        Git::GitManager manager;
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy resetPreviewSpy(&manager,
                                   &Git::GitManager::sigResetPreviewReady);
        QSignalSpy rebasePlanSpy(&manager, &Git::GitManager::sigRebasePlanReady);
        QSignalSpy operationSpy(&manager,
                                &Git::GitManager::sigHistoryOperationFinished);
        QSignalSpy stateSpy(&manager, &Git::GitManager::sigStateReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);

        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        manager.requestRebasePlan(hashes.first());
        QTRY_COMPARE_WITH_TIMEOUT(rebasePlanSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        const Git::RebasePlan plan =
            qvariant_cast<Git::RebasePlan>(rebasePlanSpy.first().at(0));
        QCOMPARE(plan.preview.expectedHead, hashes.last());
        QCOMPARE(plan.preview.targetHash, hashes.first());
        QCOMPARE(plan.items.size(), 2);

        manager.requestResetPreview(hashes.first());
        QTRY_COMPARE_WITH_TIMEOUT(resetPreviewSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        const Git::HistoryRewritePreview preview =
            qvariant_cast<Git::HistoryRewritePreview>(resetPreviewSpy.first().at(0));
        QCOMPARE(preview.expectedHead, hashes.last());
        QCOMPARE(preview.targetHash, hashes.first());
        QCOMPARE(preview.affectedCount, 2);

        stateSpy.clear();
        manager.resetToCommit(preview, Git::ResetMode::Mixed);
        QTRY_COMPARE_WITH_TIMEOUT(operationSpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!stateSpy.isEmpty(), 15000);
        QCOMPARE(operationSpy.first().at(0).toString(), QStringLiteral("reset"));
        QCOMPARE(qvariant_cast<Git::HistoryOperationStatus>(
                     operationSpy.first().at(1)),
                 Git::HistoryOperationStatus::Completed);
        QCOMPARE(qvariant_cast<Git::RepositoryState>(stateSpy.last().at(0)).headHash,
                 hashes.first());
        QCOMPARE(errorSpy.count(), 0);
    }

    void emptyRepositoryPublishesEmptyPage()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY2(createRepository(directory.path(), 0, QString(), nullptr, &error),
                 qPrintable(error));

        Git::GitManager manager;
        QSignalSpy openedSpy(&manager, &Git::GitManager::sigRepositoryOpened);
        QSignalSpy historySpy(&manager, &Git::GitManager::sigCommitHistoryReady);
        QSignalSpy errorSpy(&manager, &Git::GitManager::sigError);
        manager.openRepository(directory.path());
        QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 15000);
        QVERIFY(openedSpy.at(0).at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.isBusy(), 15000);

        const Git::CommitHistoryPage page = pageAt(historySpy, 0);
        QCOMPARE(page.offset, 0);
        QVERIFY(page.commits.isEmpty());
        QVERIFY(!page.hasMore);
        QVERIFY(!page.resetRequired);
        QCOMPARE(errorSpy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestGitManager)
#include "TestGitManager.moc"
