#include "core/LibGit2Backend.h"
#include <git2.h>
#include <git2/transaction.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    static bool writeFiles(const QString& root,
                           const QMap<QString, QByteArray>& files,
                           QString* error)
    {
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QFile file(QDir(root).filePath(it.key()));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (error)
                    *error = file.errorString();
                return false;
            }
            if (file.write(it.value()) != it.value().size()) {
                if (error)
                    *error = file.errorString();
                return false;
            }
        }
        return true;
    }

    static bool commitFiles(Git::LibGit2Backend& backend, const QString& root,
                            const QMap<QString, QByteArray>& files,
                            const QString& message, QString* hash,
                            QString* error)
    {
        if (!writeFiles(root, files, error)
            || !backend.stageAll(error)
            || !backend.commit(message, false, false, error)) {
            return false;
        }
        const Git::RepositoryState state = backend.snapshot(error);
        if (error && !error->isEmpty())
            return false;
        if (hash)
            *hash = state.headHash;
        return !state.headHash.isEmpty();
    }

    static bool initializeConfigured(Git::LibGit2Backend& backend,
                                     const QString& path, QString* error)
    {
        if (!backend.initialize(path, error))
            return false;
        git_repository* repo = nullptr;
        if (!checkGit(git_repository_open(&repo, path.toUtf8().constData()),
                      QStringLiteral("Cannot open configured repository"), error)) {
            return false;
        }
        git_config* config = nullptr;
        bool ok = checkGit(git_repository_config(&config, repo),
                           QStringLiteral("Cannot open repository config"), error)
            && checkGit(git_config_set_string(config, "user.name", "Test User"),
                        QStringLiteral("Cannot set user.name"), error)
            && checkGit(git_config_set_string(config, "user.email", "test@example.com"),
                        QStringLiteral("Cannot set user.email"), error);
        git_config_free(config);
        git_repository_free(repo);
        return ok;
    }

    static int commitParentCount(const QString& path, const QString& revision,
                                 QString* error)
    {
        git_repository* repo = nullptr;
        git_object* object = nullptr;
        git_object* peeled = nullptr;
        int result = -1;
        if (checkGit(git_repository_open(&repo, path.toUtf8().constData()),
                     QStringLiteral("Cannot open repository"), error)
            && checkGit(git_revparse_single(&object, repo,
                                            revision.toUtf8().constData()),
                        QStringLiteral("Cannot resolve commit"), error)
            && checkGit(git_object_peel(&peeled, object, GIT_OBJECT_COMMIT),
                        QStringLiteral("Cannot peel commit"), error)) {
            result = static_cast<int>(git_commit_parentcount(
                reinterpret_cast<git_commit*>(peeled)));
        }
        git_object_free(peeled);
        git_object_free(object);
        git_repository_free(repo);
        return result;
    }

    static QString commitMessage(const QString& path, const QString& revision,
                                 QString* error)
    {
        git_repository* repo = nullptr;
        git_object* object = nullptr;
        git_object* peeled = nullptr;
        QString result;
        if (checkGit(git_repository_open(&repo, path.toUtf8().constData()),
                     QStringLiteral("Cannot open repository"), error)
            && checkGit(git_revparse_single(&object, repo,
                                            revision.toUtf8().constData()),
                        QStringLiteral("Cannot resolve commit"), error)
            && checkGit(git_object_peel(&peeled, object, GIT_OBJECT_COMMIT),
                        QStringLiteral("Cannot peel commit"), error)) {
            result = QString::fromUtf8(git_commit_message(
                reinterpret_cast<git_commit*>(peeled)));
        }
        git_object_free(peeled);
        git_object_free(object);
        git_repository_free(repo);
        return result;
    }

    static bool setConfigString(const QString& path, const char* name,
                                const char* value, QString* error)
    {
        git_repository* repo = nullptr;
        git_config* config = nullptr;
        const bool opened = checkGit(
            git_repository_open(&repo, path.toUtf8().constData()),
            QStringLiteral("Cannot open repository"), error)
            && checkGit(git_repository_config(&config, repo),
                        QStringLiteral("Cannot open repository config"), error);
        const bool result = opened
            && checkGit(git_config_set_string(config, name, value),
                        QStringLiteral("Cannot update repository config"), error);
        git_config_free(config);
        git_repository_free(repo);
        return result;
    }

    static bool createNote(const QString& path, const QString& target,
                           const QString& message, QString* error)
    {
        git_repository* repo = nullptr;
        git_signature* signature = nullptr;
        git_oid targetOid;
        git_oid noteOid;
        bool result = checkGit(
            git_repository_open(&repo, path.toUtf8().constData()),
            QStringLiteral("Cannot open repository"), error)
            && checkGit(git_oid_fromstr(&targetOid,
                                        target.toLatin1().constData()),
                        QStringLiteral("Cannot parse note target"), error)
            && checkGit(git_signature_default(&signature, repo),
                        QStringLiteral("Cannot create note signature"), error);
        if (result) {
            const QByteArray note = message.toUtf8();
            result = checkGit(git_note_create(
                                  &noteOid, repo, "refs/notes/commits",
                                  signature, signature, &targetOid,
                                  note.constData(), 0),
                              QStringLiteral("Cannot create note"), error);
        }
        git_signature_free(signature);
        git_repository_free(repo);
        return result;
    }

    static QString noteMessage(const QString& path, const QString& target,
                               QString* error)
    {
        git_repository* repo = nullptr;
        git_note* note = nullptr;
        git_oid targetOid;
        QString result;
        if (checkGit(git_repository_open(&repo, path.toUtf8().constData()),
                     QStringLiteral("Cannot open repository"), error)
            && checkGit(git_oid_fromstr(&targetOid,
                                        target.toLatin1().constData()),
                        QStringLiteral("Cannot parse note target"), error)
            && checkGit(git_note_read(&note, repo, "refs/notes/commits",
                                      &targetOid),
                        QStringLiteral("Cannot read note"), error)) {
            result = QString::fromUtf8(git_note_message(note));
        }
        git_note_free(note);
        git_repository_free(repo);
        return result;
    }

    static bool createEmptyCommit(const QString& path, const QString& message,
                                  QString* hash, QString* error)
    {
        git_repository* repo = nullptr;
        git_reference* head = nullptr;
        git_commit* parent = nullptr;
        git_tree* tree = nullptr;
        git_signature* signature = nullptr;
        git_oid commitOid;
        bool result = checkGit(
            git_repository_open(&repo, path.toUtf8().constData()),
            QStringLiteral("Cannot open repository"), error)
            && checkGit(git_repository_head(&head, repo),
                        QStringLiteral("Cannot resolve HEAD"), error)
            && checkGit(git_reference_peel(
                            reinterpret_cast<git_object**>(&parent), head,
                            GIT_OBJECT_COMMIT),
                        QStringLiteral("Cannot resolve HEAD commit"), error)
            && checkGit(git_commit_tree(&tree, parent),
                        QStringLiteral("Cannot resolve HEAD tree"), error)
            && checkGit(git_signature_default(&signature, repo),
                        QStringLiteral("Cannot create commit signature"), error);
        if (result) {
            const QByteArray commitMessage = message.toUtf8();
            const git_commit* parents[] = {parent};
            result = checkGit(git_commit_create(
                                  &commitOid, repo, "HEAD", signature,
                                  signature, nullptr,
                                  commitMessage.constData(), tree, 1, parents),
                              QStringLiteral("Cannot create empty commit"), error);
        }
        if (result && hash)
            *hash = oidText(&commitOid);
        git_signature_free(signature);
        git_tree_free(tree);
        git_commit_free(parent);
        git_reference_free(head);
        git_repository_free(repo);
        return result;
    }

    static bool createDirectReference(const QString& path, const char* name,
                                      const QString& target, QString* error)
    {
        git_repository* repo = nullptr;
        git_reference* reference = nullptr;
        git_oid oid;
        bool result = checkGit(
            git_repository_open(&repo, path.toUtf8().constData()),
            QStringLiteral("Cannot open repository"), error);
        if (result && git_oid_fromstr(&oid, target.toLatin1().constData()) < 0)
            result = checkGit(-1, QStringLiteral("Cannot parse reference target"), error);
        if (result) {
            result = checkGit(git_reference_create(
                                  &reference, repo, name, &oid, 1,
                                  "published history test"),
                              QStringLiteral("Cannot create reference"), error);
        }
        git_reference_free(reference);
        git_repository_free(repo);
        return result;
    }

    static bool descendantOf(const QString& path, const QString& descendant,
                             const QString& ancestor, QString* error)
    {
        git_repository* repo = nullptr;
        git_oid descendantOid;
        git_oid ancestorOid;
        bool result = false;
        if (checkGit(git_repository_open(&repo, path.toUtf8().constData()),
                     QStringLiteral("Cannot open repository"), error)
            && checkGit(git_oid_fromstr(&descendantOid,
                                        descendant.toLatin1().constData()),
                        QStringLiteral("Cannot parse descendant"), error)
            && checkGit(git_oid_fromstr(&ancestorOid,
                                        ancestor.toLatin1().constData()),
                        QStringLiteral("Cannot parse ancestor"), error)) {
            const int rc = git_graph_descendant_of(repo, &descendantOid,
                                                   &ancestorOid);
            if (rc < 0)
                checkGit(rc, QStringLiteral("Cannot inspect graph"), error);
            else
                result = rc == 1 || git_oid_equal(&descendantOid, &ancestorOid);
        }
        git_repository_free(repo);
        return result;
    }

    static QByteArray fileContents(const QString& root, const QString& path)
    {
        QFile file(QDir(root).filePath(path));
        return file.open(QIODevice::ReadOnly)
            ? file.readAll().replace("\r\n", "\n") : QByteArray();
    }

    static bool setHeadRef(const QString& path, const char* name, QString* error)
    {
        git_repository* repo = nullptr;
        const bool result = checkGit(
            git_repository_open(&repo, path.toUtf8().constData()),
            QStringLiteral("Cannot open repository"), error)
            && checkGit(git_repository_set_head(repo, name),
                        QStringLiteral("Cannot update HEAD"), error);
        git_repository_free(repo);
        return result;
    }

    static bool writeRepositoryStateFile(const QString& root,
                                         const QString& relativePath,
                                         const QByteArray& data,
                                         QString* error)
    {
        const QString path = QDir(root).filePath(relativePath);
        QFileInfo info(path);
        if (!QDir().mkpath(info.path())) {
            if (error)
                *error = QStringLiteral("Cannot create state directory.");
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error)
                *error = file.errorString();
            return false;
        }
        if (file.write(data) != data.size()) {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
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

    void providesExternalDiffAndMergeInputs()
    {
        QTemporaryDir diffDir;
        QVERIFY(diffDir.isValid());
        Git::LibGit2Backend diffBackend;
        QString error;
        QVERIFY2(initializeConfigured(diffBackend, diffDir.path(), &error),
                 qPrintable(error));
        QVERIFY2(commitFiles(diffBackend, diffDir.path(),
                             {{QStringLiteral("compare.txt"), "base\n"}},
                             QStringLiteral("base"), nullptr, &error),
                 qPrintable(error));
        QFile comparison(diffDir.filePath(QStringLiteral("compare.txt")));
        QVERIFY(comparison.open(QIODevice::WriteOnly | QIODevice::Truncate));
        comparison.write("working\n");
        comparison.close();

        auto input = diffBackend.externalDiffInput(
            QStringLiteral("compare.txt"), false, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(input.left, QByteArray("base\n"));
        QCOMPARE(input.right, QByteArray("working\n"));
        QVERIFY(diffBackend.stage(QStringLiteral("compare.txt"), &error));
        input = diffBackend.externalDiffInput(
            QStringLiteral("compare.txt"), true, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(input.left, QByteArray("base\n"));
        QCOMPARE(input.right, QByteArray("working\n"));

        QTemporaryDir mergeDir;
        QVERIFY(mergeDir.isValid());
        Git::LibGit2Backend mergeBackend;
        QVERIFY2(initializeConfigured(mergeBackend, mergeDir.path(), &error),
                 qPrintable(error));
        QVERIFY(commitFiles(mergeBackend, mergeDir.path(),
                            {{QStringLiteral("conflict.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = mergeBackend.snapshot(&error).headName;
        QVERIFY(mergeBackend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(commitFiles(mergeBackend, mergeDir.path(),
                            {{QStringLiteral("conflict.txt"), "remote\n"}},
                            QStringLiteral("remote"), nullptr, &error));
        QVERIFY(mergeBackend.checkoutBranch(mainBranch, &error));
        QVERIFY(commitFiles(mergeBackend, mergeDir.path(),
                            {{QStringLiteral("conflict.txt"), "local\n"}},
                            QStringLiteral("local"), nullptr, &error));
        const auto mergeResult = mergeBackend.mergeRevision(
            QStringLiteral("feature"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(mergeResult.status, Git::HistoryOperationStatus::Conflicts);
        const auto mergeInput = mergeBackend.externalMergeInput(
            QStringLiteral("conflict.txt"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(mergeInput.base, QByteArray("base\n"));
        QCOMPARE(mergeInput.local, QByteArray("local\n"));
        QCOMPARE(mergeInput.remote, QByteArray("remote\n"));
    }

    void renamesBranchAndRemovesUpstream()
    {
        QTemporaryDir localDir;
        QTemporaryDir remoteDir;
        QVERIFY(localDir.isValid());
        QVERIFY(remoteDir.isValid());

        git_repository* bare = nullptr;
        QCOMPARE(git_repository_init(&bare,
                                     remoteDir.path().toUtf8().constData(), 1), 0);
        git_repository_free(bare);

        Git::LibGit2Backend backend;
        QString error;
        QVERIFY2(backend.initialize(localDir.path(), &error), qPrintable(error));
        git_repository* local = nullptr;
        QCOMPARE(git_repository_open(&local,
                                     localDir.path().toUtf8().constData()), 0);
        configure(local);
        git_repository_free(local);

        QFile file(localDir.filePath(QStringLiteral("branch.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY(backend.stage(QStringLiteral("branch.txt"), &error));
        QVERIFY(backend.commit(QStringLiteral("base"), false, false, &error));

        const QString original = backend.snapshot(&error).headName;
        QVERIFY(!original.isEmpty());
        QVERIFY(backend.addRemote(QStringLiteral("origin"), remoteDir.path(), &error));
        QVERIFY2(backend.push(QStringLiteral("origin"), original, true, &error),
                 qPrintable(error));

        const QString renamed = QStringLiteral("feature/renamed");
        QVERIFY2(backend.renameBranch(original, renamed, &error), qPrintable(error));
        auto state = backend.snapshot(&error);
        QCOMPARE(state.headName, renamed);
        QCOMPARE(state.upstream, QStringLiteral("origin/") + original);
        QVERIFY2(backend.unsetBranchUpstream(renamed, &error), qPrintable(error));
        state = backend.snapshot(&error);
        QCOMPARE(state.headName, renamed);
        QVERIFY(state.upstream.isEmpty());

        QCOMPARE(git_repository_open(&bare,
                                     remoteDir.path().toUtf8().constData()), 0);
        git_oid remoteTarget;
        const QByteArray remoteRef = QStringLiteral("refs/heads/%1")
            .arg(original).toUtf8();
        QCOMPARE(git_reference_name_to_id(&remoteTarget, bare,
                                          remoteRef.constData()), 0);
        git_repository_free(bare);
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

    void revisionDiffResolvesBranchesAndCommits()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        git_repository* repo = nullptr;
        git_repository_open(&repo, dir.path().toUtf8().constData());
        configure(repo);
        git_repository_free(repo);

        QFile file(dir.filePath(QStringLiteral("compare.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("compare.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("base"), false, false, &error), qPrintable(error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY2(!mainBranch.isEmpty(), qPrintable(error));
        QVERIFY2(backend.createBranch(QStringLiteral("feature"), mainBranch, &error), qPrintable(error));

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("main\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("compare.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("main change"), false, false, &error), qPrintable(error));
        const QString mainTip = backend.snapshot(&error).headHash;

        QVERIFY2(backend.checkoutBranch(QStringLiteral("feature"), &error), qPrintable(error));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("feature\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("compare.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("feature change"), false, false, &error), qPrintable(error));
        const QString featureTip = backend.snapshot(&error).headHash;

        const QString branchDiff = backend.revisionDiff(mainBranch, QStringLiteral("feature"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(branchDiff.contains(QStringLiteral("compare %1..feature").arg(mainBranch)));
        QVERIFY(branchDiff.contains(QStringLiteral("diff --git")));
        QVERIFY(branchDiff.contains(QStringLiteral("main"))
                || branchDiff.contains(QStringLiteral("feature")));

        const QString hashDiff = backend.revisionDiff(mainTip, featureTip, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(hashDiff.contains(QStringLiteral("compare %1..%2").arg(mainTip, featureTip)));
        QVERIFY(hashDiff.contains(QStringLiteral("diff --git")));
        QVERIFY(hashDiff.contains(QStringLiteral("main"))
                || hashDiff.contains(QStringLiteral("feature")));
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

    void forcePushWithLeaseRejectsStaleRemoteState()
    {
        QTemporaryDir publisherDir;
        QTemporaryDir contenderParent;
        QTemporaryDir remoteDir;
        QVERIFY(publisherDir.isValid());
        QVERIFY(contenderParent.isValid());
        QVERIFY(remoteDir.isValid());

        git_repository* repository = nullptr;
        QCOMPARE(git_repository_init(&repository,
                                     remoteDir.path().toUtf8().constData(), 1), 0);
        git_repository_free(repository);

        Git::LibGit2Backend publisher;
        QString error;
        QVERIFY2(publisher.initialize(publisherDir.path(), &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repository,
                                     publisherDir.path().toUtf8().constData()), 0);
        configure(repository);
        git_repository_free(repository);

        QFile published(publisherDir.filePath(QStringLiteral("lease.txt")));
        QVERIFY(published.open(QIODevice::WriteOnly));
        published.write("base\n");
        published.close();
        QVERIFY(publisher.stage(QStringLiteral("lease.txt"), &error));
        QVERIFY(publisher.commit(QStringLiteral("base"), false, false, &error));
        const QString branch = publisher.snapshot(&error).headName;
        QVERIFY(publisher.addRemote(QStringLiteral("origin"), remoteDir.path(), &error));
        QVERIFY2(publisher.push(QStringLiteral("origin"), branch, true, &error),
                 qPrintable(error));

        const QString contenderPath = contenderParent.filePath(QStringLiteral("clone"));
        Git::LibGit2Backend contender;
        QVERIFY2(contender.clone(remoteDir.path(), contenderPath, &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repository, contenderPath.toUtf8().constData()), 0);
        configure(repository);
        git_repository_free(repository);

        QVERIFY(published.open(QIODevice::WriteOnly | QIODevice::Append));
        published.write("publisher\n");
        published.close();
        QVERIFY(publisher.stage(QStringLiteral("lease.txt"), &error));
        QVERIFY(publisher.commit(QStringLiteral("publisher update"), false, false, &error));
        QVERIFY2(publisher.push({}, {}, false, &error), qPrintable(error));
        const QString publishedTip = publisher.snapshot(&error).headHash;

        QFile contenderFile(QDir(contenderPath).filePath(QStringLiteral("lease.txt")));
        QVERIFY(contenderFile.open(QIODevice::WriteOnly | QIODevice::Append));
        contenderFile.write("contender\n");
        contenderFile.close();
        QVERIFY(contender.stage(QStringLiteral("lease.txt"), &error));
        QVERIFY(contender.commit(QStringLiteral("contender update"), false, false, &error));
        const QString contenderTip = contender.snapshot(&error).headHash;

        error.clear();
        QVERIFY(!contender.push({}, {}, false, &error, true));
        QVERIFY2(error.contains(QStringLiteral("remote branch changed")),
                 qPrintable(error));
        QCOMPARE(git_repository_open(&repository,
                                     remoteDir.path().toUtf8().constData()), 0);
        git_oid remoteOid;
        const QByteArray remoteRef = QStringLiteral("refs/heads/%1").arg(branch).toUtf8();
        QCOMPARE(git_reference_name_to_id(&remoteOid, repository,
                                          remoteRef.constData()), 0);
        QCOMPARE(oidText(&remoteOid), publishedTip);
        git_repository_free(repository);

        error.clear();
        QVERIFY2(contender.fetchAll(false, &error), qPrintable(error));
        QVERIFY2(contender.push({}, {}, false, &error, true), qPrintable(error));
        QCOMPARE(git_repository_open(&repository,
                                     remoteDir.path().toUtf8().constData()), 0);
        QCOMPARE(git_reference_name_to_id(&remoteOid, repository,
                                          remoteRef.constData()), 0);
        QCOMPARE(oidText(&remoteOid), contenderTip);
        git_repository_free(repository);
    }

    void pushesDeletesRemoteTagsAndPrunesRemoteTrackingRefs()
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

        QFile file(localDir.filePath(QStringLiteral("remote.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("remote.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("base"), false, false, &error), qPrintable(error));

        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY2(!mainBranch.isEmpty(), qPrintable(error));
        QVERIFY2(backend.addRemote(QStringLiteral("origin"), remoteDir.path(), &error),
                 qPrintable(error));
        QVERIFY2(backend.push(QStringLiteral("origin"), mainBranch, true, &error),
                 qPrintable(error));

        QVERIFY2(backend.createTag(QStringLiteral("v1.0.0"), {}, {}, &error),
                 qPrintable(error));
        QVERIFY2(backend.pushTag(QStringLiteral("origin"), QStringLiteral("v1.0.0"), &error),
                 qPrintable(error));

        QCOMPARE(git_repository_open(&bare,
                                     remoteDir.path().toUtf8().constData()), 0);
        git_oid remoteTagOid;
        QCOMPARE(git_reference_name_to_id(&remoteTagOid, bare,
                                          "refs/tags/v1.0.0"), 0);
        git_repository_free(bare);

        QVERIFY2(backend.deleteRemoteTag(QStringLiteral("origin"),
                                         QStringLiteral("v1.0.0"), &error),
                 qPrintable(error));
        QCOMPARE(git_repository_open(&bare,
                                     remoteDir.path().toUtf8().constData()), 0);
        QCOMPARE(git_reference_name_to_id(&remoteTagOid, bare,
                                          "refs/tags/v1.0.0"), GIT_ENOTFOUND);
        git_repository_free(bare);

        QVERIFY2(backend.createBranch(QStringLiteral("stale"), mainBranch, &error),
                 qPrintable(error));
        QVERIFY2(backend.push(QStringLiteral("origin"), QStringLiteral("stale"), true, &error),
                 qPrintable(error));

        QCOMPARE(git_repository_open(&bare,
                                     remoteDir.path().toUtf8().constData()), 0);
        git_reference* staleRef = nullptr;
        QCOMPARE(git_reference_lookup(&staleRef, bare, "refs/heads/stale"), 0);
        QCOMPARE(git_reference_delete(staleRef), 0);
        git_reference_free(staleRef);
        git_repository_free(bare);

        QVERIFY2(backend.pruneRemote(QStringLiteral("origin"), &error), qPrintable(error));

        QCOMPARE(git_repository_open(&local,
                                     localDir.path().toUtf8().constData()), 0);
        git_reference* remoteTracking = nullptr;
        QCOMPARE(git_reference_lookup(&remoteTracking, local,
                                      "refs/remotes/origin/stale"), GIT_ENOTFOUND);
        git_reference_free(remoteTracking);
        git_repository_free(local);
    }

    void createsListsAndRemovesWorktree()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Git::LibGit2Backend backend;
        QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));

        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo);
        git_repository_free(repo);

        QFile file(dir.filePath(QStringLiteral("worktree.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("worktree.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("base"), false, false, &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        git_oid targetOid;
        const QByteArray headHash = backend.snapshot(&error).headHash.toUtf8();
        QCOMPARE(git_oid_fromstr(&targetOid, headHash.constData()), 0);
        QVERIFY2(updateBranch(repo, "refs/heads/feature-tree", targetOid, false, &error),
                 qPrintable(error));
        git_repository_free(repo);

        const QString worktreePath = QDir(dir.path()).filePath(QStringLiteral("linked-tree"));
        QVERIFY2(backend.addWorktree(QStringLiteral("feature-tree"), worktreePath,
                                     QStringLiteral("feature-tree"), &error),
                 qPrintable(error));

        const QVector<Git::WorktreeInfo> worktrees = backend.worktrees(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(worktrees.size() >= 2);
        bool foundLinked = false;
        for (const Git::WorktreeInfo& worktree : worktrees) {
            if (worktree.name != QStringLiteral("feature-tree"))
                continue;
            foundLinked = true;
            QCOMPARE(QDir::cleanPath(worktree.path), QDir::cleanPath(worktreePath));
            QCOMPARE(worktree.headBranch, QStringLiteral("feature-tree"));
            QVERIFY(worktree.valid);
        }
        QVERIFY(foundLinked);

        QVERIFY2(backend.removeWorktree(QStringLiteral("feature-tree"), true, &error),
                 qPrintable(error));
        QVERIFY(!QDir(worktreePath).exists());
        const QVector<Git::WorktreeInfo> after = backend.worktrees(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (const Git::WorktreeInfo& worktree : after)
            QVERIFY(worktree.name != QStringLiteral("feature-tree"));
    }

    void locksAndUnlocksWorktree()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Git::LibGit2Backend backend;
        QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));

        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo);
        git_repository_free(repo);

        QFile file(dir.filePath(QStringLiteral("lock-tree.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("lock-tree.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("base"), false, false, &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        git_oid targetOid;
        const QByteArray headHash = backend.snapshot(&error).headHash.toUtf8();
        QCOMPARE(git_oid_fromstr(&targetOid, headHash.constData()), 0);
        QVERIFY2(updateBranch(repo, "refs/heads/lock-tree", targetOid, false, &error),
                 qPrintable(error));
        git_repository_free(repo);

        const QString worktreePath = QDir(dir.path()).filePath(QStringLiteral("locked-tree"));
        QVERIFY2(backend.addWorktree(QStringLiteral("lock-tree"), worktreePath,
                                     QStringLiteral("lock-tree"), &error),
                 qPrintable(error));

        QVERIFY2(backend.lockWorktree(QStringLiteral("lock-tree"),
                                      QStringLiteral("portable drive"), &error),
                 qPrintable(error));
        QVector<Git::WorktreeInfo> worktrees = backend.worktrees(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        bool foundLocked = false;
        for (const Git::WorktreeInfo& worktree : worktrees) {
            if (worktree.name != QStringLiteral("lock-tree"))
                continue;
            foundLocked = true;
            QVERIFY(worktree.locked);
            QCOMPARE(worktree.lockReason, QStringLiteral("portable drive"));
        }
        QVERIFY(foundLocked);

        QVERIFY2(backend.unlockWorktree(QStringLiteral("lock-tree"), &error),
                 qPrintable(error));
        worktrees = backend.worktrees(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (const Git::WorktreeInfo& worktree : worktrees) {
            if (worktree.name != QStringLiteral("lock-tree"))
                continue;
            QVERIFY(!worktree.locked);
            QVERIFY(worktree.lockReason.isEmpty());
        }

        QVERIFY2(backend.removeWorktree(QStringLiteral("lock-tree"), true, &error),
                 qPrintable(error));
    }

    void movesWorktree()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Git::LibGit2Backend backend;
        QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));

        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        configure(repo);
        git_repository_free(repo);

        QFile file(dir.filePath(QStringLiteral("move-tree.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY2(backend.stage(QStringLiteral("move-tree.txt"), &error), qPrintable(error));
        QVERIFY2(backend.commit(QStringLiteral("base"), false, false, &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repo, dir.path().toUtf8().constData()), 0);
        git_oid targetOid;
        const QByteArray headHash = backend.snapshot(&error).headHash.toUtf8();
        QCOMPARE(git_oid_fromstr(&targetOid, headHash.constData()), 0);
        QVERIFY2(updateBranch(repo, "refs/heads/move-tree", targetOid, false, &error),
                 qPrintable(error));
        git_repository_free(repo);

        const QString originalPath = QDir(dir.path()).filePath(QStringLiteral("move-tree-original"));
        const QString movedPath = QDir(dir.path()).filePath(QStringLiteral("move-tree-moved"));
        QVERIFY2(backend.addWorktree(QStringLiteral("move-tree"), originalPath,
                                     QStringLiteral("move-tree"), &error),
                 qPrintable(error));

        QVERIFY2(backend.moveWorktree(QStringLiteral("move-tree"), movedPath, &error),
                 qPrintable(error));
        QVERIFY(!QDir(originalPath).exists());
        QVERIFY(QDir(movedPath).exists());

        const QVector<Git::WorktreeInfo> worktrees = backend.worktrees(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        bool foundMoved = false;
        for (const Git::WorktreeInfo& worktree : worktrees) {
            if (worktree.name != QStringLiteral("move-tree"))
                continue;
            foundMoved = true;
            QCOMPARE(QDir::cleanPath(worktree.path), QDir::cleanPath(movedPath));
            QCOMPARE(worktree.headBranch, QStringLiteral("move-tree"));
            QVERIFY(worktree.valid);
        }
        QVERIFY(foundMoved);

        Git::LibGit2Backend movedBackend;
        QVERIFY2(movedBackend.open(movedPath, &error), qPrintable(error));
        const auto movedState = movedBackend.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(movedState.headName, QStringLiteral("move-tree"));

        QVERIFY2(backend.removeWorktree(QStringLiteral("move-tree"), true, &error),
                 qPrintable(error));
    }

    void addsListsUpdatesAndSyncsSubmodule()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sourcePath = dir.filePath(QStringLiteral("source"));
        const QString parentPath = dir.filePath(QStringLiteral("parent"));

        Git::LibGit2Backend source;
        Git::LibGit2Backend parent;
        QString error;
        QVERIFY2(source.initialize(sourcePath, &error), qPrintable(error));
        git_repository* repo = nullptr;
        QCOMPARE(git_repository_open(&repo, sourcePath.toUtf8().constData()), 0);
        configure(repo);
        git_repository_free(repo);
        QFile sourceFile(QDir(sourcePath).filePath(QStringLiteral("library.txt")));
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        sourceFile.write("library\n");
        sourceFile.close();
        QVERIFY2(source.stage(QStringLiteral("library.txt"), &error), qPrintable(error));
        QVERIFY2(source.commit(QStringLiteral("library base"), false, false, &error),
                 qPrintable(error));

        QVERIFY2(parent.initialize(parentPath, &error), qPrintable(error));
        QCOMPARE(git_repository_open(&repo, parentPath.toUtf8().constData()), 0);
        configure(repo);
        git_repository_free(repo);
        QFile parentFile(QDir(parentPath).filePath(QStringLiteral("README.md")));
        QVERIFY(parentFile.open(QIODevice::WriteOnly));
        parentFile.write("parent\n");
        parentFile.close();
        QVERIFY2(parent.stage(QStringLiteral("README.md"), &error), qPrintable(error));
        QVERIFY2(parent.commit(QStringLiteral("parent base"), false, false, &error),
                 qPrintable(error));

        QVERIFY2(parent.addSubmodule(sourcePath, QStringLiteral("deps/library"), &error),
                 qPrintable(error));
        QVector<Git::SubmoduleInfo> submodules = parent.submodules(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(submodules.size(), 1);
        QCOMPARE(submodules.first().name, QStringLiteral("deps/library"));
        QCOMPARE(submodules.first().path, QStringLiteral("deps/library"));
        QCOMPARE(QDir::cleanPath(submodules.first().url), QDir::cleanPath(sourcePath));
        QVERIFY(submodules.first().initialized);
        QVERIFY(!submodules.first().workdirHash.isEmpty());

        QVERIFY2(parent.syncSubmodule(QStringLiteral("deps/library"), &error),
                 qPrintable(error));
        QVERIFY2(parent.updateSubmodule(QStringLiteral("deps/library"), &error),
                 qPrintable(error));
        QVERIFY2(parent.commit(QStringLiteral("add submodule"), false, false, &error),
                 qPrintable(error));
        QVERIFY2(parent.setSubmoduleBranch(QStringLiteral("deps/library"),
                                           QStringLiteral("main"), &error),
                 qPrintable(error));
        submodules = parent.submodules(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(submodules.first().branch, QStringLiteral("main"));
        QVERIFY2(parent.commit(QStringLiteral("track submodule branch"),
                               false, false, &error), qPrintable(error));
        const Git::RepositoryState state = parent.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(state.submodules.size(), 1);
        QVERIFY(QFileInfo::exists(QDir(parentPath).filePath(
            QStringLiteral("deps/library/library.txt"))));

        QVERIFY2(parent.removeSubmodule(QStringLiteral("deps/library"), false, &error),
                 qPrintable(error));
        QVERIFY(!QFileInfo::exists(QDir(parentPath).filePath(
            QStringLiteral("deps/library"))));
        QVERIFY(parent.submodules(&error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));
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

    void historyMergeSupportsFastForwardNormalAndConflict()
    {
        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QString base;
            QVERIFY2(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                                 QStringLiteral("base"), &base, &error), qPrintable(error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY2(backend.createBranch(QStringLiteral("feature"), {}, &error), qPrintable(error));
            QString feature;
            QVERIFY2(commitFiles(backend, dir.path(), {{QStringLiteral("feature.txt"), "feature\n"}},
                                 QStringLiteral("feature"), &feature, &error), qPrintable(error));
            QVERIFY2(backend.checkoutBranch(mainBranch, &error), qPrintable(error));

            QFile untracked(dir.filePath(QStringLiteral("untracked.txt")));
            QVERIFY(untracked.open(QIODevice::WriteOnly));
            untracked.write("keep\n"); untracked.close();
            auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY(!error.isEmpty());
            QCOMPARE(backend.snapshot(nullptr).headHash, base);
            QVERIFY(QFile::remove(untracked.fileName()));

            error.clear();
            result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(backend.snapshot(&error).headHash, feature);
            result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::UpToDate);
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, &error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("feature.txt"), "feature\n"}},
                                QStringLiteral("feature"), nullptr, &error));
            QVERIFY(backend.checkoutBranch(mainBranch, &error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("main.txt"), "main\n"}},
                                QStringLiteral("main"), nullptr, &error));
            const auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(commitParentCount(dir.path(), backend.snapshot(&error).headHash, &error), 2);
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("base.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, &error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("feature.txt"), "feature\n"}},
                                QStringLiteral("feature"), nullptr, &error));
            QVERIFY(backend.checkoutBranch(mainBranch, &error));
            QVERIFY2(setConfigString(dir.path(), "merge.ff", "false", &error),
                     qPrintable(error));

            const auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(commitParentCount(dir.path(), backend.snapshot(&error).headHash,
                                       &error), 2);
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("base.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, &error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("feature.txt"), "feature\n"}},
                                QStringLiteral("feature"), nullptr, &error));
            QVERIFY(backend.checkoutBranch(mainBranch, &error));
            QString mainHead;
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("main.txt"), "main\n"}},
                                QStringLiteral("main"), &mainHead, &error));
            QVERIFY2(setConfigString(dir.path(), "merge.ff", "only", &error),
                     qPrintable(error));

            const auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
            Q_UNUSED(result)
            QVERIFY(!error.isEmpty());
            const Git::RepositoryState state = backend.snapshot(nullptr);
            QCOMPARE(state.headHash, mainHead);
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("conflict.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, &error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("conflict.txt"), "feature\n"}},
                                QStringLiteral("feature"), nullptr, &error));
            QVERIFY(backend.checkoutBranch(mainBranch, &error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("conflict.txt"), "main\n"}},
                                QStringLiteral("main"), nullptr, &error));

            const auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            QCOMPARE(backend.snapshot(&error).activeOperation, Git::RepositoryOperation::Merge);
            QVERIFY2(backend.resolveConflict(QStringLiteral("conflict.txt"), false, &error),
                     qPrintable(error));
            const auto continued = backend.continueHistoryOperation(QStringLiteral("merge"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(continued.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(backend.snapshot(&error).activeOperation, Git::RepositoryOperation::None);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")), QByteArray("feature\n"));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QString base;
            QVERIFY2(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                                 QStringLiteral("base"), &base, &error), qPrintable(error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY2(backend.createBranch(QStringLiteral("feature"), {}, &error), qPrintable(error));
            QString feature;
            QVERIFY2(commitFiles(backend, dir.path(), {{QStringLiteral("feature.txt"), "feature\n"}},
                                 QStringLiteral("feature"), &feature, &error), qPrintable(error));
            QVERIFY2(backend.checkoutBranch(mainBranch, &error), qPrintable(error));

            git_repository* repo = nullptr;
            git_transaction* blocker = nullptr;
            git_reference* head = nullptr;
            QVERIFY2(checkGit(git_repository_open(&repo, dir.path().toUtf8().constData()),
                              QStringLiteral("Cannot open repository"), &error),
                     qPrintable(error));
            QVERIFY2(checkGit(git_repository_head(&head, repo),
                              QStringLiteral("Cannot resolve HEAD"), &error),
                     qPrintable(error));
            QVERIFY2(checkGit(git_transaction_new(&blocker, repo),
                              QStringLiteral("Cannot create blocking transaction"), &error),
                     qPrintable(error));
            QVERIFY2(checkGit(git_transaction_lock_ref(blocker, git_reference_name(head)),
                              QStringLiteral("Cannot lock current branch"), &error),
                     qPrintable(error));

            const auto blocked = backend.mergeRevision(QStringLiteral("feature"), &error);
            Q_UNUSED(blocked)
            QVERIFY(!error.isEmpty());
            const Git::RepositoryState blockedState = backend.snapshot(nullptr);
            QCOMPARE(blockedState.headHash, base);
            QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("feature.txt"))));

            git_reference_free(head);
            git_transaction_free(blocker);
            git_repository_free(repo);
            error.clear();

            const auto merged = backend.mergeRevision(QStringLiteral("feature"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(merged.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(backend.snapshot(&error).headHash, feature);
            QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("feature.txt"))));
        }
    }

    void rebasePlanExecutesAndRejectsStaleHead()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("feature.txt"), "feature\n"}},
                            QStringLiteral("feature"), nullptr, &error));
        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("main.txt"), "main\n"}},
                            QStringLiteral("main"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));

        Git::RebasePlan stale = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(stale.items.size(), 1);
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("later.txt"), "later\n"}},
                            QStringLiteral("later"), nullptr, &error));
        auto result = backend.rebaseOnto(stale, false, &error);
        QVERIFY(!error.isEmpty());

        error.clear();
        Git::RebasePlan current = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(current.items.size(), 2);
        result = backend.rebaseOnto(current, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY2(descendantOf(dir.path(), backend.snapshot(&error).headHash, mainHead, &error),
                 qPrintable(error));
    }

    void normalRebaseRemovesLegacyInteractiveState()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("feature.txt"), "feature\n"}},
                            QStringLiteral("feature"), nullptr, &error));
        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("main.txt"), "main\n"}},
                            QStringLiteral("main"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));
        const Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const QString legacySidecar =
            dir.filePath(QStringLiteral(".git/gitmanager-rebase.json"));
        QFile stale(legacySidecar);
        QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(stale.write("{\"version\":1,\"stale\":true}"), qint64(26));
        stale.close();

        const auto result = backend.rebaseOnto(plan, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(!QFileInfo::exists(legacySidecar));
        QVERIFY2(descendantOf(dir.path(), backend.snapshot(&error).headHash,
                              mainHead, &error), qPrintable(error));
    }

    void cherryPickAndRevertSupportMainline()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QString featureHead;
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("feature.txt"), "feature\n"}},
                            QStringLiteral("feature"), &featureHead, &error));
        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainBeforeMerge;
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("main.txt"), "main\n"}},
                            QStringLiteral("main"), &mainBeforeMerge, &error));
        auto result = backend.mergeRevision(QStringLiteral("feature"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QString mergeCommit = backend.snapshot(&error).headHash;
        QCOMPARE(commitParentCount(dir.path(), mergeCommit, &error), 2);

        QVERIFY2(backend.createBranch(QStringLiteral("target"), mainBeforeMerge, &error),
                 qPrintable(error));
        result = backend.cherryPickCommit(mergeCommit, 0, &error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(backend.snapshot(nullptr).activeOperation, Git::RepositoryOperation::None);

        error.clear();
        result = backend.cherryPickCommit(mergeCommit, 1, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("feature.txt"))));
        result = backend.revertCommit(mergeCommit, 1, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("feature.txt"))));

        QVERIFY2(backend.createBranch(QStringLiteral("target-mainline-2"),
                                      featureHead, &error), qPrintable(error));
        result = backend.cherryPickCommit(mergeCommit, 2, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("main.txt"))));
        result = backend.revertCommit(mergeCommit, 2, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("main.txt"))));
    }

    void cherryPickAndRevertConflictsCanContinueOrAbort()
    {
        const auto prepareConflict = [](QTemporaryDir& dir,
                                        Git::LibGit2Backend& backend,
                                        QString* sourceHead,
                                        QString* targetHead,
                                        QString* error) {
            if (!initializeConfigured(backend, dir.path(), error)
                || !commitFiles(backend, dir.path(),
                                {{QStringLiteral("conflict.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, error)) {
                return false;
            }
            const QString mainBranch = backend.snapshot(error).headName;
            return backend.createBranch(QStringLiteral("source"), {}, error)
                && commitFiles(backend, dir.path(),
                               {{QStringLiteral("conflict.txt"), "source\n"}},
                               QStringLiteral("source"), sourceHead, error)
                && backend.checkoutBranch(mainBranch, error)
                && commitFiles(backend, dir.path(),
                               {{QStringLiteral("conflict.txt"), "target\n"}},
                               QStringLiteral("target"), targetHead, error);
        };

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sourceHead, targetHead;
            QVERIFY2(prepareConflict(dir, backend, &sourceHead, &targetHead,
                                     &error), qPrintable(error));

            auto result = backend.cherryPickCommit(sourceHead, 0, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            QCOMPARE(backend.snapshot(&error).activeOperation,
                     Git::RepositoryOperation::CherryPick);
            QVERIFY2(backend.resolveConflict(QStringLiteral("conflict.txt"),
                                             false, &error), qPrintable(error));
            result = backend.continueHistoryOperation(
                QStringLiteral("cherry-pick"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            const Git::RepositoryState state = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
            QVERIFY(state.headHash != targetHead);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")),
                     QByteArray("source\n"));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sourceHead, targetHead;
            QVERIFY2(prepareConflict(dir, backend, &sourceHead, &targetHead,
                                     &error), qPrintable(error));

            const auto result = backend.cherryPickCommit(sourceHead, 0, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            QVERIFY2(backend.abortOperation(QStringLiteral("cherry-pick"),
                                            &error), qPrintable(error));
            const Git::RepositoryState state = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
            QCOMPARE(state.headHash, targetHead);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")),
                     QByteArray("target\n"));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sourceHead, targetHead;
            QVERIFY2(prepareConflict(dir, backend, &sourceHead, &targetHead,
                                     &error), qPrintable(error));

            auto result = backend.revertCommit(sourceHead, 0, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            QCOMPARE(backend.snapshot(&error).activeOperation,
                     Git::RepositoryOperation::Revert);
            QVERIFY2(backend.resolveConflict(QStringLiteral("conflict.txt"),
                                             false, &error), qPrintable(error));
            result = backend.continueHistoryOperation(QStringLiteral("revert"),
                                                      &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            const Git::RepositoryState state = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
            QVERIFY(state.headHash != targetHead);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")),
                     QByteArray("base\n"));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sourceHead, targetHead;
            QVERIFY2(prepareConflict(dir, backend, &sourceHead, &targetHead,
                                     &error), qPrintable(error));

            const auto result = backend.revertCommit(sourceHead, 0, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            QVERIFY2(backend.abortOperation(QStringLiteral("revert"), &error),
                     qPrintable(error));
            const Git::RepositoryState state = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
            QCOMPARE(state.headHash, targetHead);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")),
                     QByteArray("target\n"));
        }
    }

    void resetModesPreserveTheirDocumentedLayers()
    {
        const auto runReset = [](Git::ResetMode mode) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
            QString base;
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("value.txt"), "base\n"}},
                                QStringLiteral("base"), &base, &error));
            QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("value.txt"), "second\n"}},
                                QStringLiteral("second"), nullptr, &error));
            const Git::HistoryRewritePreview preview = backend.resetPreview(base, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(preview.affectedCount, 1);
            QCOMPARE(preview.publishedCount, 0);
            const auto result = backend.resetToCommit(preview, mode, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            const Git::RepositoryState state = backend.snapshot(&error);
            QCOMPARE(state.headHash, base);
            if (mode == Git::ResetMode::Soft) {
                QCOMPARE(fileContents(dir.path(), QStringLiteral("value.txt")), QByteArray("second\n"));
                QCOMPARE(state.files.size(), 1);
                QVERIFY(state.files.first().isStaged());
            } else if (mode == Git::ResetMode::Mixed) {
                QCOMPARE(fileContents(dir.path(), QStringLiteral("value.txt")), QByteArray("second\n"));
                QCOMPARE(state.files.size(), 1);
                QVERIFY(!state.files.first().isStaged());
                QVERIFY(state.files.first().isUnstaged());
            } else {
                QCOMPARE(fileContents(dir.path(), QStringLiteral("value.txt")), QByteArray("base\n"));
                QVERIFY(state.files.isEmpty());
            }
        };

        runReset(Git::ResetMode::Soft);
        runReset(Git::ResetMode::Mixed);
        runReset(Git::ResetMode::Hard);
    }

    void resetPreviewDetectsPublishedCommits()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QString base;
        QString published;
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("value.txt"), "base\n"}},
                             QStringLiteral("base"), &base, &error),
                 qPrintable(error));
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("value.txt"), "published\n"}},
                             QStringLiteral("published"), &published, &error),
                 qPrintable(error));

        QVERIFY2(createDirectReference(dir.path(), "refs/remotes/origin/main",
                                       published, &error), qPrintable(error));

        const Git::HistoryRewritePreview preview = backend.resetPreview(base, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(preview.affectedCount, 1);
        QCOMPARE(preview.publishedCount, 1);
    }

    void resetPreviewRejectsBranchIdentityChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        const QString initialBranch = backend.snapshot(&error).headName;
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QString sharedHead;
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("value.txt"), "base\n"}},
                             QStringLiteral("base"), &sharedHead, &error),
                 qPrintable(error));
        QVERIFY2(createDirectReference(dir.path(), "refs/heads/other", sharedHead, &error),
                 qPrintable(error));

        const Git::HistoryRewritePreview preview =
            backend.resetPreview(QStringLiteral("HEAD~0"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(preview.currentBranch, initialBranch);

        QVERIFY2(setHeadRef(dir.path(), "refs/heads/other", &error), qPrintable(error));
        const Git::RepositoryState before = backend.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto result = backend.resetToCommit(preview, Git::ResetMode::Soft, &error);
        Q_UNUSED(result)
        QVERIFY(!error.isEmpty());
        const Git::RepositoryState after = backend.snapshot(nullptr);
        QCOMPARE(after.headHash, before.headHash);
        QCOMPARE(after.headName, QStringLiteral("other"));
        QCOMPARE(fileContents(dir.path(), QStringLiteral("value.txt")),
                 QByteArray("base\n"));
    }

    void rebasePlanRejectsBranchIdentityChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("base.txt"), "base\n"}},
                             QStringLiteral("base"), nullptr, &error),
                 qPrintable(error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY2(backend.createBranch(QStringLiteral("feature"), {}, &error),
                 qPrintable(error));
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("feature.txt"), "feature\n"}},
                             QStringLiteral("feature"), nullptr, &error),
                 qPrintable(error));
        const QString featureHead = backend.snapshot(&error).headHash;
        QVERIFY2(createDirectReference(dir.path(), "refs/heads/other", featureHead, &error),
                 qPrintable(error));
        QVERIFY2(backend.checkoutBranch(mainBranch, &error), qPrintable(error));
        QString mainHead;
        QVERIFY2(commitFiles(backend, dir.path(),
                             {{QStringLiteral("main.txt"), "main\n"}},
                             QStringLiteral("main"), &mainHead, &error),
                 qPrintable(error));
        QVERIFY2(backend.checkoutBranch(QStringLiteral("feature"), &error),
                 qPrintable(error));

        const Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan.preview.currentBranch, QStringLiteral("feature"));

        QVERIFY2(setHeadRef(dir.path(), "refs/heads/other", &error), qPrintable(error));
        const auto result = backend.rebaseOnto(plan, false, &error);
        Q_UNUSED(result)
        QVERIFY(!error.isEmpty());
        const Git::RepositoryState state = backend.snapshot(nullptr);
        QCOMPARE(state.headName, QStringLiteral("other"));
        QCOMPARE(state.headHash, featureHead);
        QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
    }

    void multiCommitSequencerStateIsRejectedSafely()
    {
        const auto run = [](const QString& headFile,
                            const QString& operationName) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error),
                     qPrintable(error));
            QString headHash;
            QVERIFY2(commitFiles(backend, dir.path(),
                                 {{QStringLiteral("value.txt"), "base\n"}},
                                 QStringLiteral("base"), &headHash, &error),
                     qPrintable(error));
            QVERIFY2(writeRepositoryStateFile(
                         dir.path(),
                         QStringLiteral(".git/%1").arg(headFile),
                         headHash.toUtf8() + '\n', &error),
                     qPrintable(error));
            QVERIFY2(writeRepositoryStateFile(
                         dir.path(),
                         QStringLiteral(".git/sequencer/todo"),
                         QByteArray("pick ") + headHash.toUtf8()
                             + QByteArray(" sample\n"),
                         &error),
                     qPrintable(error));

            const Git::RepositoryState snapshot = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(snapshot.activeOperation, Git::RepositoryOperation::Unknown);
            QCOMPARE(snapshot.headHash, headHash);

            QByteArray todoBefore = fileContents(
                dir.path(), QStringLiteral(".git/sequencer/todo"));
            QVERIFY(!backend.continueOperation(operationName, &error));
            QVERIFY(!error.isEmpty());
            error.clear();
            QVERIFY(!backend.abortOperation(operationName, &error));
            QVERIFY(!error.isEmpty());

            const Git::RepositoryState after = backend.snapshot(nullptr);
            QCOMPARE(after.activeOperation, Git::RepositoryOperation::Unknown);
            QCOMPARE(after.headHash, headHash);
            QCOMPARE(fileContents(dir.path(), QStringLiteral(".git/sequencer/todo")),
                     todoBefore);
        };

        run(QStringLiteral("CHERRY_PICK_HEAD"), QStringLiteral("cherry-pick"));
        run(QStringLiteral("REVERT_HEAD"), QStringLiteral("revert"));
    }

    void interactiveRebaseSupportsAllActionsAndSidecarRecovery()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        for (int index = 1; index <= 6; ++index) {
            const QString path = QStringLiteral("item%1.txt").arg(index);
            const QString subject = QStringLiteral("item-%1").arg(index);
            QVERIFY2(commitFiles(backend, dir.path(), {{path, subject.toUtf8() + '\n'}},
                                 subject, nullptr, &error), qPrintable(error));
        }
        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(), {{QStringLiteral("upstream.txt"), "upstream\n"}},
                            QStringLiteral("upstream"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));
        QVERIFY2(setConfigString(dir.path(), "notes.rewrite.rebase", "true", &error),
                 qPrintable(error));
        QVERIFY2(setConfigString(dir.path(), "notes.rewriteref",
                                 "refs/notes/commits", &error),
                 qPrintable(error));

        Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan.items.size(), 6);
        for (Git::RebasePlanItem& item : plan.items) {
            if (item.subject == QStringLiteral("item-1"))
                item.action = Git::RebaseAction::Pick;
            else if (item.subject == QStringLiteral("item-2")) {
                item.action = Git::RebaseAction::Reword;
                item.rewrittenMessage = QStringLiteral("item-two-reworded\n");
            } else if (item.subject == QStringLiteral("item-3"))
                item.action = Git::RebaseAction::Edit;
            else if (item.subject == QStringLiteral("item-4"))
                item.action = Git::RebaseAction::Squash;
            else if (item.subject == QStringLiteral("item-5"))
                item.action = Git::RebaseAction::Fixup;
            else if (item.subject == QStringLiteral("item-6"))
                item.action = Git::RebaseAction::Drop;
        }

        auto result = backend.rebaseOnto(plan, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::PausedForEdit);
        QCOMPARE(backend.snapshot(&error).activeOperation, Git::RepositoryOperation::Rebase);
        const QString sidecar = dir.filePath(
            QStringLiteral(".git/rebase-merge/gitmanager.json"));
        QVERIFY(QFileInfo::exists(sidecar));

        backend.close();
        Git::LibGit2Backend reopened;
        QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
        result = reopened.continueHistoryOperation(QStringLiteral("rebase"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QVERIFY(!QFileInfo::exists(sidecar));
        QCOMPARE(reopened.snapshot(&error).activeOperation, Git::RepositoryOperation::None);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("item4.txt"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("item5.txt"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("item6.txt"))));

        Git::CommitHistoryQuery query;
        query.limit = 20;
        const auto history = reopened.commitHistory(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(history.commits.size(), 5);
        QStringList subjects;
        for (const Git::Commit& commit : history.commits)
            subjects.append(commit.subject);
        QVERIFY(subjects.contains(QStringLiteral("item-1")));
        QVERIFY(subjects.contains(QStringLiteral("item-two-reworded")));
        QVERIFY(subjects.contains(QStringLiteral("item-3")));
        QVERIFY(!subjects.contains(QStringLiteral("item-4")));
        QVERIFY(!subjects.contains(QStringLiteral("item-5")));
        QVERIFY(!subjects.contains(QStringLiteral("item-6")));
    }

    void interactiveRebaseRejectsForeignAndInvalidSidecars()
    {
        const auto startPaused = [](QTemporaryDir& dir,
                                    Git::LibGit2Backend& backend,
                                    QString* sidecar, QString* error) {
            if (!initializeConfigured(backend, dir.path(), error)
                || !commitFiles(backend, dir.path(),
                                {{QStringLiteral("base.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, error)) {
                return false;
            }
            const QString mainBranch = backend.snapshot(error).headName;
            if (!backend.createBranch(QStringLiteral("feature"), {}, error)
                || !commitFiles(backend, dir.path(),
                                {{QStringLiteral("feature.txt"), "feature\n"}},
                                QStringLiteral("feature"), nullptr, error)
                || !backend.checkoutBranch(mainBranch, error)) {
                return false;
            }
            QString mainHead;
            if (!commitFiles(backend, dir.path(),
                             {{QStringLiteral("main.txt"), "main\n"}},
                             QStringLiteral("main"), &mainHead, error)
                || !backend.checkoutBranch(QStringLiteral("feature"), error)) {
                return false;
            }
            Git::RebasePlan plan = backend.rebasePlan(mainHead, error);
            if ((error && !error->isEmpty()) || plan.items.size() != 1)
                return false;
            plan.items[0].action = Git::RebaseAction::Edit;
            const auto result = backend.rebaseOnto(plan, true, error);
            if ((error && !error->isEmpty())
                || result.status != Git::HistoryOperationStatus::PausedForEdit) {
                return false;
            }
            *sidecar = dir.filePath(
                QStringLiteral(".git/rebase-merge/gitmanager.json"));
            return QFileInfo::exists(*sidecar);
        };

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sidecar;
            QVERIFY2(startPaused(dir, backend, &sidecar, &error), qPrintable(error));
            QFile input(sidecar);
            QVERIFY(input.open(QIODevice::ReadOnly));
            QJsonDocument document = QJsonDocument::fromJson(input.readAll());
            input.close();
            QJsonObject root = document.object();
            QJsonObject preview = root.value(QStringLiteral("preview")).toObject();
            preview.insert(QStringLiteral("targetHash"),
                           preview.value(QStringLiteral("expectedHead")));
            root.insert(QStringLiteral("preview"), preview);
            QFile output(sidecar);
            QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(output.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
            output.close();

            backend.close();
            Git::LibGit2Backend reopened;
            QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
            error.clear();
            const auto result = reopened.continueHistoryOperation(
                QStringLiteral("rebase"), &error);
            Q_UNUSED(result)
            QVERIFY(error.contains(QStringLiteral("different rebase session")));
            QCOMPARE(reopened.snapshot(nullptr).activeOperation,
                     Git::RepositoryOperation::Rebase);
            error.clear();
            QVERIFY2(reopened.abortOperation(QStringLiteral("rebase"), &error),
                     qPrintable(error));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, sidecar;
            QVERIFY2(startPaused(dir, backend, &sidecar, &error), qPrintable(error));
            QFile input(sidecar);
            QVERIFY(input.open(QIODevice::ReadOnly));
            QJsonDocument document = QJsonDocument::fromJson(input.readAll());
            input.close();
            QJsonObject root = document.object();
            QJsonArray items = root.value(QStringLiteral("items")).toArray();
            QJsonObject first = items.at(0).toObject();
            first.insert(QStringLiteral("action"),
                         static_cast<int>(Git::RebaseAction::Squash));
            items.replace(0, first);
            root.insert(QStringLiteral("items"), items);
            QFile output(sidecar);
            QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(output.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
            output.close();

            backend.close();
            Git::LibGit2Backend reopened;
            QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
            error.clear();
            const auto result = reopened.continueHistoryOperation(
                QStringLiteral("rebase"), &error);
            Q_UNUSED(result)
            QVERIFY(error.contains(QStringLiteral("invalid action"))
                    || error.contains(QStringLiteral("edit state")));
            QCOMPARE(reopened.snapshot(nullptr).activeOperation,
                     Git::RepositoryOperation::Rebase);
            error.clear();
            QVERIFY2(reopened.abortOperation(QStringLiteral("rebase"), &error),
                     qPrintable(error));
        }
    }

    void interactiveRebaseRecoversPendingSquashWithoutDuplicatingMessage()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("first.txt"), "first\n"}},
                            QStringLiteral("first-edit"), nullptr, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("second.txt"), "second\n"}},
                            QStringLiteral("second-body"), nullptr, &error));
        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("upstream.txt"), "upstream\n"}},
                            QStringLiteral("upstream"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));

        Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan.items.size(), 2);
        plan.items[0].action = Git::RebaseAction::Edit;
        plan.items[1].action = Git::RebaseAction::Squash;
        plan.items[1].rewrittenMessage = QStringLiteral("second-body");
        auto result = backend.rebaseOnto(plan, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::PausedForEdit);

        const QString backupPath = dir.filePath(
            QStringLiteral(".git/rebase-merge/gitmanager-rewritten"));
        QVERIFY(QDir().mkpath(backupPath));
        result = backend.continueHistoryOperation(QStringLiteral("rebase"), &error);
        Q_UNUSED(result)
        QVERIFY(error.contains(QStringLiteral("rewritten commit mapping")));
        QCOMPARE(backend.snapshot(nullptr).activeOperation,
                 Git::RepositoryOperation::Rebase);
        const QString sidecar = dir.filePath(
            QStringLiteral(".git/rebase-merge/gitmanager.json"));
        QVERIFY(QFileInfo::exists(sidecar));

        backend.close();
        QVERIFY(QDir().rmdir(backupPath));
        Git::LibGit2Backend reopened;
        error.clear();
        QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
        result = reopened.continueHistoryOperation(QStringLiteral("rebase"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        QCOMPARE(reopened.snapshot(nullptr).activeOperation,
                 Git::RepositoryOperation::None);
        QVERIFY(!QFileInfo::exists(sidecar));

        const QString message = commitMessage(
            dir.path(), reopened.snapshot(&error).headHash, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(message.startsWith(QStringLiteral("first-edit")));
        QCOMPARE(message.count(QStringLiteral("second-body")), 1);
    }

    void interactiveRebasePrefersCompleteBackupOverValidPrimaryPrefix()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QString firstHash;
        QString notedHash;
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("first.txt"), "first\n"}},
                            QStringLiteral("first"), &firstHash, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("noted.txt"), "noted\n"}},
                            QStringLiteral("noted"), &notedHash, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("squash.txt"), "squash\n"}},
                            QStringLiteral("squash"), nullptr, &error));
        QVERIFY2(createNote(dir.path(), notedHash,
                            QStringLiteral("note survives interrupted squash"),
                            &error), qPrintable(error));

        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("upstream.txt"), "upstream\n"}},
                            QStringLiteral("upstream"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));
        QVERIFY2(setConfigString(dir.path(), "notes.rewrite.rebase", "true", &error),
                 qPrintable(error));
        QVERIFY2(setConfigString(dir.path(), "notes.rewriteref",
                                 "refs/notes/commits", &error),
                 qPrintable(error));

        Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan.items.size(), 3);
        plan.items[1].action = Git::RebaseAction::Edit;
        plan.items[2].action = Git::RebaseAction::Squash;
        auto result = backend.rebaseOnto(plan, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::PausedForEdit);

        const QString rebaseDir = dir.filePath(QStringLiteral(".git/rebase-merge"));
        const QString sidecarPath = QDir(rebaseDir).filePath(
            QStringLiteral("gitmanager.json"));
        const QString primaryPath = QDir(rebaseDir).filePath(
            QStringLiteral("rewritten"));
        const QString backupPath = QDir(rebaseDir).filePath(
            QStringLiteral("gitmanager-rewritten"));
        QVERIFY(QDir().mkpath(backupPath));
        result = backend.continueHistoryOperation(QStringLiteral("rebase"), &error);
        Q_UNUSED(result)
        QVERIFY(error.contains(QStringLiteral("rewritten commit mapping")));
        QCOMPARE(backend.snapshot(nullptr).activeOperation,
                 Git::RepositoryOperation::Rebase);
        const QString rewrittenHead = backend.snapshot(nullptr).headHash;
        QVERIFY(!rewrittenHead.isEmpty());

        QFile sidecarFile(sidecarPath);
        QVERIFY(sidecarFile.open(QIODevice::ReadOnly));
        const QJsonObject sidecar = QJsonDocument::fromJson(
            sidecarFile.readAll()).object();
        sidecarFile.close();
        const int pendingIndex = sidecar.value(QStringLiteral("pendingIndex")).toInt(-1);
        QCOMPARE(pendingIndex, 2);
        const QString preActionHead =
            sidecar.value(QStringLiteral("preActionHead")).toString();
        const QJsonArray items = sidecar.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 3);
        const QString squashSource = items.at(pendingIndex).toObject()
            .value(QStringLiteral("hash")).toString();
        QVERIFY(!preActionHead.isEmpty());
        QVERIFY(!squashSource.isEmpty());

        QFile primaryInput(primaryPath);
        QVERIFY(primaryInput.open(QIODevice::ReadOnly));
        const QStringList primaryLines = QString::fromUtf8(primaryInput.readAll())
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        primaryInput.close();
        QCOMPARE(primaryLines.size(), 2);

        QStringList completeLines;
        QString prefixLine;
        for (const QString& line : primaryLines) {
            const int separator = line.indexOf(QLatin1Char(' '));
            QVERIFY(separator > 0);
            const QString source = line.left(separator);
            QString target = line.mid(separator + 1).trimmed();
            if (target.compare(preActionHead, Qt::CaseInsensitive) == 0)
                target = rewrittenHead;
            completeLines.append(source + QLatin1Char(' ') + target);
            if (source.compare(firstHash, Qt::CaseInsensitive) == 0)
                prefixLine = completeLines.constLast();
        }
        completeLines.append(squashSource + QLatin1Char(' ') + rewrittenHead);
        QVERIFY(!prefixLine.isEmpty());
        const QByteArray completeData =
            (completeLines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
        const QByteArray prefixData = (prefixLine + QLatin1Char('\n')).toUtf8();

        backend.close();
        QVERIFY(QDir().rmdir(backupPath));
        QFile backupOutput(backupPath);
        QVERIFY(backupOutput.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(backupOutput.write(completeData), qint64(completeData.size()));
        backupOutput.close();
        QFile primaryOutput(primaryPath);
        QVERIFY(primaryOutput.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(primaryOutput.write(prefixData), qint64(prefixData.size()));
        primaryOutput.close();

        Git::LibGit2Backend reopened;
        error.clear();
        QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
        result = reopened.continueHistoryOperation(QStringLiteral("rebase"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        const Git::RepositoryState finalState = reopened.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(finalState.activeOperation, Git::RepositoryOperation::None);
        QVERIFY2(descendantOf(dir.path(), finalState.headHash, mainHead, &error),
                 qPrintable(error));
        error.clear();
        QCOMPARE(noteMessage(dir.path(), finalState.headHash, &error),
                 QStringLiteral("note survives interrupted squash"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    void interactiveRebaseAllowsEmptyFixupWithUnchangedHead()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(initializeConfigured(backend, dir.path(), &error), qPrintable(error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("base.txt"), "base\n"}},
                            QStringLiteral("base"), nullptr, &error));
        const QString mainBranch = backend.snapshot(&error).headName;
        QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("pause.txt"), "pause\n"}},
                            QStringLiteral("pause"), nullptr, &error));
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("first.txt"), "first\n"}},
                            QStringLiteral("first"), nullptr, &error));
        QString emptyHash;
        QVERIFY2(createEmptyCommit(dir.path(), QStringLiteral("empty-fixup"),
                                   &emptyHash, &error), qPrintable(error));
        QCOMPARE(backend.snapshot(&error).headHash, emptyHash);

        QVERIFY(backend.checkoutBranch(mainBranch, &error));
        QString mainHead;
        QVERIFY(commitFiles(backend, dir.path(),
                            {{QStringLiteral("upstream.txt"), "upstream\n"}},
                            QStringLiteral("upstream"), &mainHead, &error));
        QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));

        Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(plan.items.size(), 3);
        QCOMPARE(plan.items.at(2).hash, emptyHash);
        plan.items[0].action = Git::RebaseAction::Edit;
        plan.items[2].action = Git::RebaseAction::Fixup;
        auto result = backend.rebaseOnto(plan, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::PausedForEdit);

        const QString rebaseDir = dir.filePath(QStringLiteral(".git/rebase-merge"));
        const QString sidecarPath = QDir(rebaseDir).filePath(
            QStringLiteral("gitmanager.json"));
        const QString backupPath = QDir(rebaseDir).filePath(
            QStringLiteral("gitmanager-rewritten"));
        QVERIFY(QDir().mkpath(backupPath));
        result = backend.continueHistoryOperation(QStringLiteral("rebase"), &error);
        Q_UNUSED(result)
        QVERIFY(error.contains(QStringLiteral("rewritten commit mapping")));
        const QString unchangedHead = backend.snapshot(nullptr).headHash;
        QFile sidecarFile(sidecarPath);
        QVERIFY(sidecarFile.open(QIODevice::ReadOnly));
        const QJsonObject sidecar = QJsonDocument::fromJson(
            sidecarFile.readAll()).object();
        sidecarFile.close();
        QCOMPARE(sidecar.value(QStringLiteral("pendingIndex")).toInt(-1), 2);
        QCOMPARE(sidecar.value(QStringLiteral("preActionHead")).toString(),
                 unchangedHead);

        backend.close();
        QVERIFY(QDir().rmdir(backupPath));
        Git::LibGit2Backend reopened;
        error.clear();
        QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
        result = reopened.continueHistoryOperation(QStringLiteral("rebase"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
        const Git::RepositoryState state = reopened.snapshot(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
        QVERIFY2(descendantOf(dir.path(), state.headHash, mainHead, &error),
                 qPrintable(error));

        Git::CommitHistoryQuery query;
        query.limit = 10;
        const Git::CommitHistoryPage history = reopened.commitHistory(query, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(history.commits.size(), 4);
        for (const Git::Commit& commit : history.commits)
            QVERIFY(commit.hash != emptyHash);
    }

    void interactiveRebaseRejectsSquashAfterAlreadyAppliedPick()
    {
        const auto run = [](Git::RebaseAction action) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error;
            QVERIFY2(initializeConfigured(backend, dir.path(), &error),
                     qPrintable(error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("base.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, &error));
            const QString mainBranch = backend.snapshot(&error).headName;
            QVERIFY(backend.createBranch(QStringLiteral("feature"), {}, &error));
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("shared.txt"), "shared\n"}},
                                QStringLiteral("feature-shared"), nullptr, &error));
            QString featureHead;
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("second.txt"), "second\n"}},
                                QStringLiteral("second"), &featureHead, &error));
            QVERIFY(backend.checkoutBranch(mainBranch, &error));
            QString mainHead;
            QVERIFY(commitFiles(backend, dir.path(),
                                {{QStringLiteral("shared.txt"), "shared\n"}},
                                QStringLiteral("upstream-shared"), &mainHead, &error));
            QVERIFY(backend.checkoutBranch(QStringLiteral("feature"), &error));

            Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(plan.items.size(), 2);
            plan.items[1].action = action;
            const auto result = backend.rebaseOnto(plan, true, &error);
            Q_UNUSED(result)
            QVERIFY(error.contains(QStringLiteral("previous rewritten commit")));
            QCOMPARE(backend.snapshot(nullptr).activeOperation,
                     Git::RepositoryOperation::Rebase);
            error.clear();
            QVERIFY2(backend.abortOperation(QStringLiteral("rebase"), &error),
                     qPrintable(error));
            QCOMPARE(backend.snapshot(&error).headHash, featureHead);
        };

        run(Git::RebaseAction::Squash);
        run(Git::RebaseAction::Fixup);
    }

    void interactiveRebaseConflictCanContinueOrAbortAfterReopen()
    {
        const auto prepareConflict = [](QTemporaryDir& dir,
                                        Git::LibGit2Backend& backend,
                                        QString* mainHead,
                                        QString* featureHead,
                                        QString* error) {
            if (!initializeConfigured(backend, dir.path(), error)
                || !commitFiles(backend, dir.path(),
                                {{QStringLiteral("conflict.txt"), "base\n"}},
                                QStringLiteral("base"), nullptr, error))
                return false;
            const QString mainBranch = backend.snapshot(error).headName;
            return backend.createBranch(QStringLiteral("feature"), {}, error)
                && commitFiles(backend, dir.path(),
                               {{QStringLiteral("conflict.txt"), "feature\n"}},
                               QStringLiteral("feature"), featureHead, error)
                && backend.checkoutBranch(mainBranch, error)
                && commitFiles(backend, dir.path(),
                               {{QStringLiteral("conflict.txt"), "main\n"}},
                               QStringLiteral("main"), mainHead, error)
                && backend.checkoutBranch(QStringLiteral("feature"), error);
        };

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, mainHead, featureHead;
            QVERIFY2(prepareConflict(dir, backend, &mainHead, &featureHead, &error),
                     qPrintable(error));
            Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            auto result = backend.rebaseOnto(plan, true, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            const Git::RepositoryState pausedState = backend.snapshot(&error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            error.clear();
            QVERIFY(!backend.checkoutBranch(QStringLiteral("feature"), &error));
            QVERIFY(!error.isEmpty());
            error.clear();
            QVERIFY(!backend.createBranch(QStringLiteral("forbidden"), mainHead,
                                          &error));
            QVERIFY(!error.isEmpty());
            error.clear();
            QVERIFY(!backend.deleteBranch(QStringLiteral("feature"), true,
                                          &error));
            QVERIFY(!error.isEmpty());
            error.clear();
            QVERIFY(!backend.createTag(QStringLiteral("forbidden-tag"), {}, {},
                                       &error));
            QVERIFY(!error.isEmpty());
            const Git::RepositoryState guardedState = backend.snapshot(nullptr);
            QCOMPARE(guardedState.headHash, pausedState.headHash);
            QCOMPARE(guardedState.activeOperation, Git::RepositoryOperation::Rebase);
            error.clear();
            QVERIFY2(backend.resolveConflict(QStringLiteral("conflict.txt"), false, &error),
                     qPrintable(error));
            backend.close();

            Git::LibGit2Backend reopened;
            QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
            result = reopened.continueHistoryOperation(QStringLiteral("rebase"), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Completed);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")), QByteArray("feature\n"));
            QVERIFY2(descendantOf(dir.path(), reopened.snapshot(&error).headHash,
                                  mainHead, &error), qPrintable(error));
        }

        {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            Git::LibGit2Backend backend; QString error, mainHead, featureHead;
            QVERIFY2(prepareConflict(dir, backend, &mainHead, &featureHead, &error),
                     qPrintable(error));
            const Git::RebasePlan plan = backend.rebasePlan(mainHead, &error);
            const auto result = backend.rebaseOnto(plan, true, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(result.status, Git::HistoryOperationStatus::Conflicts);
            backend.close();

            Git::LibGit2Backend reopened;
            QVERIFY2(reopened.open(dir.path(), &error), qPrintable(error));
            QVERIFY2(reopened.abortOperation(QStringLiteral("rebase"), &error), qPrintable(error));
            const Git::RepositoryState state = reopened.snapshot(&error);
            QCOMPARE(state.headHash, featureHead);
            QCOMPARE(state.activeOperation, Git::RepositoryOperation::None);
            QCOMPARE(fileContents(dir.path(), QStringLiteral("conflict.txt")), QByteArray("feature\n"));
            QVERIFY(!QFileInfo::exists(
                dir.filePath(QStringLiteral(".git/rebase-merge/gitmanager.json"))));
        }
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
