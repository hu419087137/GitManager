#include "LibGit2Backend.h"

#include <git2.h>
#include <git2/sys/errors.h>
#include <git2/transaction.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace Git {

struct InteractiveRebaseState {
    RebasePlan plan;
    bool pausedForEdit {false};
    int completedIndex {-1};
    int pendingIndex {-1};
    QString preActionHead;
    QString pendingTree;
};

namespace {

template <typename T, void (*Free)(T*)>
class GitHandle {
public:
    GitHandle() = default;
    explicit GitHandle(T* value) : _value(value) {}
    ~GitHandle() { reset(); }

    GitHandle(const GitHandle&) = delete;
    GitHandle& operator=(const GitHandle&) = delete;

    T* get() const { return _value; }
    T** out()
    {
        reset();
        return &_value;
    }
    void reset(T* value = nullptr)
    {
        if (_value)
            Free(_value);
        _value = value;
    }
    explicit operator bool() const { return _value != nullptr; }

private:
    T* _value {nullptr};
};

using AnnotatedHandle = GitHandle<git_annotated_commit, git_annotated_commit_free>;
using CommitHandle = GitHandle<git_commit, git_commit_free>;
using ConfigHandle = GitHandle<git_config, git_config_free>;
using ConfigIteratorHandle = GitHandle<git_config_iterator, git_config_iterator_free>;
using DiffHandle = GitHandle<git_diff, git_diff_free>;
using IndexHandle = GitHandle<git_index, git_index_free>;
using ObjectHandle = GitHandle<git_object, git_object_free>;
using RebaseHandle = GitHandle<git_rebase, git_rebase_free>;
using ReferenceHandle = GitHandle<git_reference, git_reference_free>;
using RemoteHandle = GitHandle<git_remote, git_remote_free>;
using RevwalkHandle = GitHandle<git_revwalk, git_revwalk_free>;
using SignatureHandle = GitHandle<git_signature, git_signature_free>;
using StatusListHandle = GitHandle<git_status_list, git_status_list_free>;
using TreeHandle = GitHandle<git_tree, git_tree_free>;
using TreeEntryHandle = GitHandle<git_tree_entry, git_tree_entry_free>;
using TransactionHandle = GitHandle<git_transaction, git_transaction_free>;
using WorktreeHandle = GitHandle<git_worktree, git_worktree_free>;
using SubmoduleHandle = GitHandle<git_submodule, git_submodule_free>;
using RepositoryHandle = GitHandle<git_repository, git_repository_free>;

QString oidText(const git_oid* oid)
{
    if (!oid)
        return {};
    char value[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(value, sizeof(value), oid);
    return QString::fromLatin1(value);
}

QString text(const char* value)
{
    return value ? QString::fromUtf8(value) : QString();
}

bool fail(QString* error, const QString& fallback)
{
    if (error)
        *error = LibGit2Backend::lastError(fallback);
    return false;
}

bool ensureRepository(git_repository* repository, QString* error)
{
    if (repository)
        return true;
    if (error)
        *error = QStringLiteral("No repository is open.");
    return false;
}

bool ensureNoActiveOperation(git_repository* repository, QString* error)
{
    if (git_repository_state(repository) == GIT_REPOSITORY_STATE_NONE)
        return true;
    if (error) {
        *error = QStringLiteral(
            "Finish or abort the active repository operation first.");
    }
    return false;
}

File::Status statusValue(unsigned int flags, bool index)
{
    if (!index && (flags & GIT_STATUS_WT_NEW))
        return File::Status::E_Untracked;
    if (flags & (index ? GIT_STATUS_INDEX_NEW : GIT_STATUS_WT_NEW))
        return File::Status::E_Added;
    if (flags & (index ? GIT_STATUS_INDEX_MODIFIED : GIT_STATUS_WT_MODIFIED))
        return File::Status::E_Modified;
    if (flags & (index ? GIT_STATUS_INDEX_DELETED : GIT_STATUS_WT_DELETED))
        return File::Status::E_Deleted;
    if (flags & (index ? GIT_STATUS_INDEX_RENAMED : GIT_STATUS_WT_RENAMED))
        return File::Status::E_Renamed;
    if (flags & (index ? GIT_STATUS_INDEX_TYPECHANGE : GIT_STATUS_WT_TYPECHANGE))
        return File::Status::E_TypeChanged;
    return File::Status::E_Unmodified;
}

bool lookupHeadCommit(git_repository* repository, CommitHandle& commit)
{
    ReferenceHandle head;
    if (git_repository_head(head.out(), repository) < 0)
        return false;
    const git_oid* oid = git_reference_target(head.get());
    return oid && git_commit_lookup(commit.out(), repository, oid) == 0;
}

bool lookupHeadTree(git_repository* repository, TreeHandle& tree)
{
    CommitHandle commit;
    return lookupHeadCommit(repository, commit)
        && git_commit_tree(tree.out(), commit.get()) == 0;
}

bool lookupCommit(git_repository* repository, const QString& revision,
                  CommitHandle& commit, QString* error,
                  const QString& fallback)
{
    const QByteArray spec = revision.toUtf8();
    ObjectHandle object;
    ObjectHandle peeled;
    if (git_revparse_single(object.out(), repository, spec.constData()) < 0
        || git_object_peel(peeled.out(), object.get(), GIT_OBJECT_COMMIT) < 0
        || git_commit_lookup(commit.out(), repository,
                             git_object_id(peeled.get())) < 0) {
        fail(error, fallback);
        return false;
    }
    return true;
}

QString resolveWorktreePath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool writeTextFile(const QString& path, const QString& content, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(content.toUtf8()) < 0) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool deleteConfigSection(git_config* config, const QString& section,
                         QString* error)
{
    const QString expression = QStringLiteral("^submodule\\.%1\\..*$")
        .arg(QRegularExpression::escape(section));
    ConfigIteratorHandle iterator;
    if (git_config_iterator_glob_new(iterator.out(), config,
                                     expression.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot inspect submodule configuration."));

    QStringList keys;
    git_config_entry* entry = nullptr;
    int rc = 0;
    while ((rc = git_config_next(&entry, iterator.get())) == 0) {
        if (entry && entry->name)
            keys.append(QString::fromUtf8(entry->name));
    }
    if (rc != GIT_ITEROVER)
        return fail(error, QStringLiteral("Cannot inspect submodule configuration."));
    git_error_clear();
    iterator.reset();
    for (const QString& key : keys) {
        if (git_config_delete_entry(config, key.toUtf8().constData()) < 0)
            return fail(error, QStringLiteral("Cannot remove submodule configuration."));
    }
    return true;
}

bool copyDirectoryRecursively(const QString& sourcePath, const QString& targetPath,
                              QString* error)
{
    QDir source(sourcePath);
    if (!source.exists()) {
        if (error)
            *error = QStringLiteral("Source worktree directory does not exist.");
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (error)
            *error = QStringLiteral("Cannot create target worktree directory.");
        return false;
    }

    QDirIterator it(sourcePath,
                    QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden
                        | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = source.relativeFilePath(info.filePath());
        const QString target = QDir(targetPath).filePath(relative);

        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                if (error)
                    *error = QStringLiteral("Cannot create target subdirectory.");
                return false;
            }
            continue;
        }

        const QFileInfo targetInfo(target);
        if (!QDir().mkpath(targetInfo.dir().absolutePath())) {
            if (error)
                *error = QStringLiteral("Cannot create target parent directory.");
            return false;
        }
        if (QFile::exists(target) && !QFile::remove(target)) {
            if (error)
                *error = QStringLiteral("Cannot replace target file.");
            return false;
        }

        bool copied = false;
        if (info.isSymLink()) {
            copied = QFile::link(info.symLinkTarget(), target);
        } else {
            copied = QFile::copy(info.filePath(), target);
            if (copied)
                QFile::setPermissions(target, info.permissions());
        }
        if (!copied) {
            if (error)
                *error = QStringLiteral("Cannot copy worktree contents.");
            return false;
        }
    }

    return true;
}

bool moveDirectoryRecursively(const QString& sourcePath, const QString& targetPath,
                              QString* error)
{
    const QString source = QDir::cleanPath(sourcePath);
    const QString target = QDir::cleanPath(targetPath);
    if (source.compare(target, Qt::CaseInsensitive) == 0)
        return true;

    const QString sourcePrefix = source.endsWith(QDir::separator())
        ? source : source + QDir::separator();
    if (target.startsWith(sourcePrefix, Qt::CaseInsensitive)) {
        if (error)
            *error = QStringLiteral("Cannot move a worktree into itself.");
        return false;
    }

    const QFileInfo targetInfo(target);
    if (targetInfo.exists()) {
        if (error)
            *error = QStringLiteral("Target worktree path already exists.");
        return false;
    }
    if (!QDir().mkpath(targetInfo.dir().absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create target parent directory.");
        return false;
    }

    const QFileInfo sourceInfo(source);
    QDir sourceParent(sourceInfo.dir());
    if (sourceParent.rename(sourceInfo.fileName(), target))
        return true;

    if (!copyDirectoryRecursively(source, target, error))
        return false;

    QDir sourceDir(source);
    if (!sourceDir.removeRecursively()) {
        if (error)
            *error = QStringLiteral("Cannot remove original worktree directory.");
        return false;
    }
    return true;
}

QString branchNameFromRef(const char* refName)
{
    const QString full = text(refName);
    if (full.startsWith(QStringLiteral("refs/heads/")))
        return full.mid(11);
    if (full.startsWith(QStringLiteral("refs/remotes/")))
        return full.mid(13);
    return full;
}

HistoryOperationResult historyResult(HistoryOperationStatus status,
                                     const QString& message)
{
    HistoryOperationResult result;
    result.status = status;
    result.message = message;
    return result;
}

HistoryOperationResult historyFailure(QString* error, const QString& fallback)
{
    const QString message = LibGit2Backend::lastError(fallback);
    if (error)
        *error = message;
    return historyResult(HistoryOperationStatus::Completed, message);
}

RepositoryOperation repositoryOperation(int state)
{
    switch (state) {
    case GIT_REPOSITORY_STATE_NONE:
        return RepositoryOperation::None;
    case GIT_REPOSITORY_STATE_MERGE:
        return RepositoryOperation::Merge;
    case GIT_REPOSITORY_STATE_REBASE:
    case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
    case GIT_REPOSITORY_STATE_REBASE_MERGE:
        return RepositoryOperation::Rebase;
    case GIT_REPOSITORY_STATE_CHERRYPICK:
        return RepositoryOperation::CherryPick;
    case GIT_REPOSITORY_STATE_REVERT:
        return RepositoryOperation::Revert;
    default:
        return RepositoryOperation::Unknown;
    }
}

bool repositoryDirty(git_repository* repository, bool* dirty, QString* error)
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
                  | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                  | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
                  | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
    StatusListHandle statuses;
    if (git_status_list_new(statuses.out(), repository, &options) < 0)
        return fail(error, QStringLiteral("Cannot inspect repository changes."));
    if (dirty)
        *dirty = git_status_list_entrycount(statuses.get()) != 0;
    return true;
}

bool headDetails(git_repository* repository, QString* hash, QString* branch,
                 QString* upstream, QString* error)
{
    ReferenceHandle head;
    if (git_repository_head(head.out(), repository) < 0)
        return fail(error, QStringLiteral("Cannot resolve HEAD."));
    const git_oid* oid = git_reference_target(head.get());
    if (!oid)
        return fail(error, QStringLiteral("Cannot resolve HEAD commit."));
    if (hash)
        *hash = oidText(oid);
    if (branch) {
        *branch = git_repository_head_detached(repository) == 1
            ? QString() : text(git_reference_shorthand(head.get()));
    }
    if (upstream) {
        upstream->clear();
        if (git_repository_head_detached(repository) != 1) {
            ReferenceHandle upstreamRef;
            if (git_branch_upstream(upstreamRef.out(), head.get()) == 0)
                *upstream = text(git_reference_shorthand(upstreamRef.get()));
            else
                git_error_clear();
        }
    }
    return true;
}

bool ensureHistoryOperationStart(git_repository* repository,
                                 const QString& expectedHead,
                                 QString* error)
{
    if (git_repository_state(repository) != GIT_REPOSITORY_STATE_NONE) {
        if (error)
            *error = QStringLiteral("Another repository operation is already in progress.");
        return false;
    }
    bool dirty = false;
    if (!repositoryDirty(repository, &dirty, error))
        return false;
    if (dirty) {
        if (error) {
            *error = QStringLiteral(
                "Commit, stash, or discard all staged, unstaged, and untracked changes first.");
        }
        return false;
    }
    QString currentHead;
    if (!headDetails(repository, &currentHead, nullptr, nullptr, error))
        return false;
    if (!expectedHead.isEmpty()
        && currentHead.compare(expectedHead, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("HEAD changed after the operation was prepared. Refresh and try again.");
        return false;
    }
    return true;
}

QSet<QString> remoteReachableCommits(git_repository* repository,
                                     QString* error)
{
    QSet<QString> result;
    RevwalkHandle walk;
    if (git_revwalk_new(walk.out(), repository) < 0) {
        fail(error, QStringLiteral("Cannot inspect published history."));
        return result;
    }

    git_branch_iterator* iterator = nullptr;
    if (git_branch_iterator_new(&iterator, repository, GIT_BRANCH_REMOTE) < 0) {
        fail(error, QStringLiteral("Cannot enumerate remote branches."));
        return result;
    }
    bool pushed = false;
    git_reference* raw = nullptr;
    git_branch_t type = GIT_BRANCH_REMOTE;
    int next = GIT_ITEROVER;
    while ((next = git_branch_next(&raw, &type, iterator)) == 0) {
        ReferenceHandle reference(raw);
        raw = nullptr;
        ObjectHandle commit;
        if (git_reference_peel(commit.out(), reference.get(), GIT_OBJECT_COMMIT) == 0) {
            if (git_revwalk_push(walk.get(), git_object_id(commit.get())) == 0)
                pushed = true;
            else {
                git_branch_iterator_free(iterator);
                fail(error, QStringLiteral("Cannot walk remote branch history."));
                return {};
            }
        } else {
            git_error_clear();
        }
    }
    git_branch_iterator_free(iterator);
    if (next != GIT_ITEROVER) {
        fail(error, QStringLiteral("Cannot enumerate remote branches."));
        return {};
    }
    if (!pushed)
        return result;

    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_oid oid;
    int rc = 0;
    while ((rc = git_revwalk_next(&oid, walk.get())) == 0)
        result.insert(oidText(&oid));
    if (rc != GIT_ITEROVER) {
        fail(error, QStringLiteral("Cannot inspect published history."));
        return {};
    }
    git_error_clear();
    return result;
}

QVector<QString> commitsBetween(git_repository* repository,
                                const git_oid* head, const git_oid* upstream,
                                QString* error)
{
    QVector<QString> result;
    RevwalkHandle walk;
    if (git_revwalk_new(walk.out(), repository) < 0
        || git_revwalk_push(walk.get(), head) < 0
        || git_revwalk_hide(walk.get(), upstream) < 0) {
        fail(error, QStringLiteral("Cannot inspect affected commits."));
        return result;
    }
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_oid oid;
    int rc = 0;
    while ((rc = git_revwalk_next(&oid, walk.get())) == 0)
        result.append(oidText(&oid));
    if (rc != GIT_ITEROVER) {
        fail(error, QStringLiteral("Cannot inspect affected commits."));
        result.clear();
    }
    git_error_clear();
    return result;
}

QString interactiveRebasePath(git_repository* repository)
{
    const char* gitDir = git_repository_path(repository);
    return gitDir
        ? QDir(QString::fromUtf8(gitDir))
              .filePath(QStringLiteral("rebase-merge/gitmanager.json"))
        : QString();
}

QString legacyInteractiveRebasePath(git_repository* repository)
{
    const char* gitDir = git_repository_path(repository);
    return gitDir
        ? QDir(QString::fromUtf8(gitDir))
              .filePath(QStringLiteral("gitmanager-rebase.json"))
        : QString();
}

bool jsonInteger(const QJsonValue& value, int* result)
{
    if (!value.isDouble())
        return false;
    const int converted = value.toInt(std::numeric_limits<int>::min());
    if (converted == std::numeric_limits<int>::min()
        || value.toDouble() != static_cast<double>(converted)) {
        return false;
    }
    if (result)
        *result = converted;
    return true;
}

bool validOidText(const QString& value)
{
    if (value.size() != GIT_OID_SHA1_HEXSIZE)
        return false;
    git_oid oid;
    const QByteArray bytes = value.toLatin1();
    const bool valid = git_oid_fromstr(&oid, bytes.constData()) == 0;
    git_error_clear();
    return valid;
}

QJsonObject previewJson(const HistoryRewritePreview& preview)
{
    QJsonObject object;
    object.insert(QStringLiteral("revision"), preview.revision);
    object.insert(QStringLiteral("targetHash"), preview.targetHash);
    object.insert(QStringLiteral("expectedHead"), preview.expectedHead);
    object.insert(QStringLiteral("currentBranch"), preview.currentBranch);
    object.insert(QStringLiteral("upstream"), preview.upstream);
    object.insert(QStringLiteral("affectedCount"), preview.affectedCount);
    object.insert(QStringLiteral("publishedCount"), preview.publishedCount);
    object.insert(QStringLiteral("dirty"), preview.dirty);
    object.insert(QStringLiteral("activeOperation"),
                  static_cast<int>(preview.activeOperation));
    return object;
}

bool previewFromJson(const QJsonObject& object,
                     HistoryRewritePreview* preview)
{
    const QJsonValue revision = object.value(QStringLiteral("revision"));
    const QJsonValue targetHash = object.value(QStringLiteral("targetHash"));
    const QJsonValue expectedHead = object.value(QStringLiteral("expectedHead"));
    const QJsonValue currentBranch = object.value(QStringLiteral("currentBranch"));
    const QJsonValue upstream = object.value(QStringLiteral("upstream"));
    const QJsonValue dirty = object.value(QStringLiteral("dirty"));
    if (!revision.isString() || !targetHash.isString()
        || !expectedHead.isString() || !currentBranch.isString()
        || !upstream.isString() || !dirty.isBool()) {
        return false;
    }

    int affectedCount = -1;
    int publishedCount = -1;
    int activeOperation = -1;
    if (!jsonInteger(object.value(QStringLiteral("affectedCount")),
                     &affectedCount)
        || !jsonInteger(object.value(QStringLiteral("publishedCount")),
                        &publishedCount)
        || !jsonInteger(object.value(QStringLiteral("activeOperation")),
                        &activeOperation)
        || affectedCount < 0 || publishedCount < 0
        || activeOperation < static_cast<int>(RepositoryOperation::None)
        || activeOperation > static_cast<int>(RepositoryOperation::Unknown)
        || !validOidText(targetHash.toString())
        || !validOidText(expectedHead.toString())) {
        return false;
    }

    if (preview) {
        preview->revision = revision.toString();
        preview->targetHash = targetHash.toString();
        preview->expectedHead = expectedHead.toString();
        preview->currentBranch = currentBranch.toString();
        preview->upstream = upstream.toString();
        preview->affectedCount = affectedCount;
        preview->publishedCount = publishedCount;
        preview->dirty = dirty.toBool();
        preview->activeOperation =
            static_cast<RepositoryOperation>(activeOperation);
    }
    return true;
}

QJsonObject rebaseStateJson(const InteractiveRebaseState& state)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("pausedForEdit"), state.pausedForEdit);
    root.insert(QStringLiteral("completedIndex"), state.completedIndex);
    root.insert(QStringLiteral("pendingIndex"), state.pendingIndex);
    root.insert(QStringLiteral("preActionHead"), state.preActionHead);
    root.insert(QStringLiteral("pendingTree"), state.pendingTree);
    root.insert(QStringLiteral("preview"), previewJson(state.plan.preview));
    QJsonArray items;
    for (const RebasePlanItem& item : state.plan.items) {
        QJsonObject value;
        value.insert(QStringLiteral("hash"), item.hash);
        value.insert(QStringLiteral("subject"), item.subject);
        value.insert(QStringLiteral("message"), item.message);
        value.insert(QStringLiteral("action"), static_cast<int>(item.action));
        value.insert(QStringLiteral("rewrittenMessage"), item.rewrittenMessage);
        value.insert(QStringLiteral("published"), item.published);
        value.insert(QStringLiteral("parentCount"), item.parentCount);
        items.append(value);
    }
    root.insert(QStringLiteral("items"), items);
    return root;
}

bool saveInteractiveRebase(git_repository* repository,
                           const InteractiveRebaseState& state,
                           QString* error)
{
    QSaveFile file(interactiveRebasePath(repository));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot save interactive rebase state: %1")
                         .arg(file.errorString());
        return false;
    }
    const QByteArray data = QJsonDocument(rebaseStateJson(state))
                                .toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) {
        if (error) {
            *error = QStringLiteral("Cannot save interactive rebase state: %1")
                         .arg(file.errorString());
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot save interactive rebase state: %1")
                         .arg(file.errorString());
        return false;
    }
    return true;
}

bool loadInteractiveRebase(git_repository* repository,
                           InteractiveRebaseState* state,
                           QString* error)
{
    QFile file(interactiveRebasePath(repository));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open interactive rebase state.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    int version = -1;
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !jsonInteger(root.value(QStringLiteral("version")), &version)
        || version != 2
        || !root.value(QStringLiteral("preview")).isObject()
        || !root.value(QStringLiteral("items")).isArray()
        || !root.value(QStringLiteral("pausedForEdit")).isBool()
        || !root.value(QStringLiteral("preActionHead")).isString()
        || !root.value(QStringLiteral("pendingTree")).isString()) {
        if (error)
            *error = QStringLiteral("Interactive rebase state is invalid.");
        return false;
    }

    InteractiveRebaseState loaded;
    if (!previewFromJson(root.value(QStringLiteral("preview")).toObject(),
                         &loaded.plan.preview)
        || loaded.plan.preview.dirty
        || loaded.plan.preview.activeOperation != RepositoryOperation::None) {
        if (error)
            *error = QStringLiteral("Interactive rebase state is invalid.");
        return false;
    }

    bool hasEffectiveCommit = false;
    for (const QJsonValue& raw : root.value(QStringLiteral("items")).toArray()) {
        if (!raw.isObject()) {
            if (error)
                *error = QStringLiteral("Interactive rebase state is invalid.");
            return false;
        }
        const QJsonObject value = raw.toObject();
        const QJsonValue hash = value.value(QStringLiteral("hash"));
        const QJsonValue subject = value.value(QStringLiteral("subject"));
        const QJsonValue message = value.value(QStringLiteral("message"));
        const QJsonValue rewritten = value.value(QStringLiteral("rewrittenMessage"));
        const QJsonValue published = value.value(QStringLiteral("published"));
        if (!hash.isString() || !subject.isString() || !message.isString()
            || !rewritten.isString() || !published.isBool()) {
            if (error)
                *error = QStringLiteral("Interactive rebase state is invalid.");
            return false;
        }
        RebasePlanItem item;
        item.hash = hash.toString();
        item.subject = subject.toString();
        item.message = message.toString();
        const QJsonValue actionValue = value.value(QStringLiteral("action"));
        int action = -1;
        if (!jsonInteger(actionValue, &action)
            || action < static_cast<int>(RebaseAction::Pick)
            || action > static_cast<int>(RebaseAction::Drop)) {
            if (error)
                *error = QStringLiteral("Interactive rebase state contains an invalid action.");
            return false;
        }
        item.action = static_cast<RebaseAction>(action);
        item.rewrittenMessage = rewritten.toString();
        item.published = published.toBool();
        if (!jsonInteger(value.value(QStringLiteral("parentCount")),
                         &item.parentCount)
            || item.parentCount < 0 || item.parentCount > 1
            || !validOidText(item.hash)) {
            if (error)
                *error = QStringLiteral("Interactive rebase state is invalid.");
            return false;
        }

        if (!hasEffectiveCommit && item.action != RebaseAction::Drop) {
            if (item.action == RebaseAction::Squash
                || item.action == RebaseAction::Fixup) {
                if (error) {
                    *error = QStringLiteral(
                        "Interactive rebase state starts with an invalid action.");
                }
                return false;
            }
            hasEffectiveCommit = true;
        }
        const QString effectiveMessage = item.rewrittenMessage.isEmpty()
            ? item.message : item.rewrittenMessage;
        if ((item.action == RebaseAction::Reword
             || item.action == RebaseAction::Squash)
            && effectiveMessage.trimmed().isEmpty()) {
            if (error) {
                *error = QStringLiteral(
                    "Interactive rebase state contains an empty commit message.");
            }
            return false;
        }
        loaded.plan.items.append(item);
    }

    loaded.pausedForEdit =
        root.value(QStringLiteral("pausedForEdit")).toBool();
    loaded.preActionHead =
        root.value(QStringLiteral("preActionHead")).toString();
    loaded.pendingTree = root.value(QStringLiteral("pendingTree")).toString();
    if (loaded.plan.items.isEmpty()
        || !jsonInteger(root.value(QStringLiteral("completedIndex")),
                        &loaded.completedIndex)
        || !jsonInteger(root.value(QStringLiteral("pendingIndex")),
                        &loaded.pendingIndex)
        || loaded.completedIndex < -1
        || loaded.completedIndex >= loaded.plan.items.size()
        || loaded.pendingIndex < -1
        || loaded.pendingIndex >= loaded.plan.items.size()) {
        if (error)
            *error = QStringLiteral("Interactive rebase state contains an invalid position.");
        return false;
    }

    if (loaded.pendingIndex >= 0) {
        const RebaseAction pendingAction =
            loaded.plan.items.at(loaded.pendingIndex).action;
        if (loaded.pausedForEdit
            || (pendingAction != RebaseAction::Squash
                && pendingAction != RebaseAction::Fixup)
            || loaded.completedIndex != loaded.pendingIndex - 1
            || !validOidText(loaded.preActionHead)
            || !validOidText(loaded.pendingTree)) {
            if (error)
                *error = QStringLiteral("Interactive rebase pending state is invalid.");
            return false;
        }
    } else if (!loaded.preActionHead.isEmpty()
               || !loaded.pendingTree.isEmpty()) {
        if (error)
            *error = QStringLiteral("Interactive rebase pending state is invalid.");
        return false;
    }

    if (loaded.pausedForEdit) {
        const int editIndex = loaded.completedIndex + 1;
        if (editIndex < 0 || editIndex >= loaded.plan.items.size()
            || loaded.plan.items.at(editIndex).action != RebaseAction::Edit) {
            if (error)
                *error = QStringLiteral("Interactive rebase edit state is invalid.");
            return false;
        }
    }

    if (state)
        *state = std::move(loaded);
    return true;
}

bool removeInteractiveRebase(git_repository* repository, QString* error = nullptr)
{
    const QStringList paths = {
        interactiveRebasePath(repository),
        legacyInteractiveRebasePath(repository)
    };
    for (const QString& path : paths) {
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (!QFile::remove(path)) {
            if (error) {
                *error = QStringLiteral(
                    "Cannot remove stale interactive rebase state: %1")
                             .arg(QDir::toNativeSeparators(path));
            }
            return false;
        }
    }
    return true;
}

struct NetworkContext {
    QByteArray username;
    QByteArray secret;
    LibGit2Backend::ProgressCallback progress;
    const std::atomic_bool* cancelFlag {nullptr};
    QString pushError;
    QByteArray leaseDestination;
    git_oid leaseExpected {};
    bool forceWithLease {false};

    bool cancelled() const
    {
        return cancelFlag && cancelFlag->load(std::memory_order_relaxed);
    }
};

int cancelNetwork(NetworkContext* context)
{
    if (!context || !context->cancelled())
        return 0;
    git_error_set_str(GIT_ERROR_CALLBACK, "Operation cancelled.");
    return GIT_EUSER;
}

int credentialCallback(git_credential** out, const char*,
                       const char* usernameFromUrl,
                       unsigned int allowedTypes, void* payload)
{
    auto* context = static_cast<NetworkContext*>(payload);
    if (cancelNetwork(context) < 0)
        return GIT_EUSER;

    const QByteArray username = context && !context->username.isEmpty()
        ? context->username
        : QByteArray(usernameFromUrl ? usernameFromUrl : "git");

    if ((allowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT)
        && context && !context->secret.isEmpty()) {
        return git_credential_userpass_plaintext_new(
            out, username.constData(), context->secret.constData());
    }
    if (allowedTypes & GIT_CREDENTIAL_SSH_KEY)
        return git_credential_ssh_key_from_agent(out, username.constData());
    if (allowedTypes & GIT_CREDENTIAL_USERNAME)
        return git_credential_username_new(out, username.constData());
    if (allowedTypes & GIT_CREDENTIAL_DEFAULT)
        return git_credential_default_new(out);
    return GIT_PASSTHROUGH;
}

int sidebandCallback(const char* value, int length, void* payload)
{
    auto* context = static_cast<NetworkContext*>(payload);
    if (cancelNetwork(context) < 0)
        return GIT_EUSER;
    if (context && context->progress && value && length > 0) {
        const QString message = QString::fromUtf8(value, length).trimmed();
        if (!message.isEmpty())
            context->progress(message, -1);
    }
    return 0;
}

int transferCallback(const git_indexer_progress* stats, void* payload)
{
    auto* context = static_cast<NetworkContext*>(payload);
    if (cancelNetwork(context) < 0)
        return GIT_EUSER;
    if (context && context->progress && stats) {
        int percent = -1;
        if (stats->total_objects > 0)
            percent = static_cast<int>(stats->received_objects * 100
                                       / stats->total_objects);
        context->progress(QStringLiteral("Receiving objects %1/%2")
                              .arg(stats->received_objects)
                              .arg(stats->total_objects),
                          percent);
    }
    return 0;
}

int pushTransferCallback(unsigned int current, unsigned int total,
                         size_t, void* payload)
{
    auto* context = static_cast<NetworkContext*>(payload);
    if (cancelNetwork(context) < 0)
        return GIT_EUSER;
    if (context && context->progress) {
        const int percent = total > 0 ? static_cast<int>(current * 100 / total) : -1;
        context->progress(QStringLiteral("Pushing objects %1/%2")
                              .arg(current).arg(total), percent);
    }
    return 0;
}

int pushUpdateCallback(const char*, const char* status, void* payload)
{
    if (!status)
        return 0;
    auto* context = static_cast<NetworkContext*>(payload);
    if (context)
        context->pushError = QString::fromUtf8(status);
    git_error_set_str(GIT_ERROR_NET, status);
    return GIT_EUSER;
}

int pushNegotiationCallback(const git_push_update** updates, size_t length,
                            void* payload)
{
    auto* context = static_cast<NetworkContext*>(payload);
    if (!context || !context->forceWithLease)
        return 0;
    for (size_t index = 0; index < length; ++index) {
        const git_push_update* update = updates[index];
        if (!update || !update->dst_refname
            || context->leaseDestination != update->dst_refname) {
            continue;
        }
        if (git_oid_cmp(&update->src, &context->leaseExpected) == 0)
            return 0;
        context->pushError = QStringLiteral(
            "Force-with-lease rejected: the remote branch changed since the last fetch.");
        git_error_set_str(GIT_ERROR_NET, context->pushError.toUtf8().constData());
        return GIT_EUSER;
    }
    context->pushError = QStringLiteral(
        "Force-with-lease rejected: the remote branch was not advertised.");
    git_error_set_str(GIT_ERROR_NET, context->pushError.toUtf8().constData());
    return GIT_EUSER;
}

NetworkContext networkContext(const RemoteCredentials& credentials,
                              const LibGit2Backend::ProgressCallback& callback,
                              const std::atomic_bool* cancelFlag)
{
    NetworkContext context;
    context.username = credentials.username.toUtf8();
    context.secret = credentials.secret.toUtf8();
    context.progress = callback;
    context.cancelFlag = cancelFlag;
    return context;
}

void configureCallbacks(git_remote_callbacks& callbacks,
                        NetworkContext* context)
{
    callbacks.credentials = credentialCallback;
    callbacks.sideband_progress = sidebandCallback;
    callbacks.transfer_progress = transferCallback;
    callbacks.push_transfer_progress = pushTransferCallback;
    callbacks.push_update_reference = pushUpdateCallback;
    callbacks.push_negotiation = pushNegotiationCallback;
    callbacks.payload = context;
}

QString submoduleState(git_repository* repository, const QString& path,
                       const git_diff_delta* delta)
{
    const bool isSubmodule = delta
        && (delta->old_file.mode == GIT_FILEMODE_COMMIT
            || delta->new_file.mode == GIT_FILEMODE_COMMIT);
    if (!isSubmodule)
        return QStringLiteral("N...");

    unsigned int status = 0;
    const QByteArray name = path.toUtf8();
    if (git_submodule_status(&status, repository, name.constData(),
                             GIT_SUBMODULE_IGNORE_NONE) < 0)
        return QStringLiteral("S...");

    const QChar commit = status & (GIT_SUBMODULE_STATUS_INDEX_ADDED
                                  | GIT_SUBMODULE_STATUS_INDEX_DELETED
                                  | GIT_SUBMODULE_STATUS_INDEX_MODIFIED
                                  | GIT_SUBMODULE_STATUS_WD_ADDED
                                  | GIT_SUBMODULE_STATUS_WD_DELETED)
        ? QLatin1Char('C') : QLatin1Char('.');
    const QChar modified = status & (GIT_SUBMODULE_STATUS_WD_MODIFIED
                                    | GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED
                                    | GIT_SUBMODULE_STATUS_WD_WD_MODIFIED)
        ? QLatin1Char('M') : QLatin1Char('.');
    const QChar untracked = status & GIT_SUBMODULE_STATUS_WD_UNTRACKED
        ? QLatin1Char('U') : QLatin1Char('.');
    return QStringLiteral("S%1%2%3").arg(commit).arg(modified).arg(untracked);
}

QString renamedFrom(git_repository* repository, const QString& path,
                    bool staged)
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
                  | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                  | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
                  | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
                  | GIT_STATUS_OPT_RENAMES_FROM_REWRITES;
    StatusListHandle status;
    if (git_status_list_new(status.out(), repository, &options) < 0) {
        git_error_clear();
        return {};
    }

    const size_t count = git_status_list_entrycount(status.get());
    for (size_t index = 0; index < count; ++index) {
        const git_status_entry* entry = git_status_byindex(status.get(), index);
        const git_diff_delta* delta = entry
            ? (staged ? entry->head_to_index : entry->index_to_workdir)
            : nullptr;
        if (delta && delta->status == GIT_DELTA_RENAMED
            && text(delta->new_file.path) == path)
            return text(delta->old_file.path);
    }
    return {};
}

int restoreIndexPath(git_index* index, git_tree* headTree,
                     const QByteArray& path)
{
    TreeEntryHandle entry;
    const int lookup = headTree
        ? git_tree_entry_bypath(entry.out(), headTree, path.constData())
        : GIT_ENOTFOUND;
    if (lookup == GIT_ENOTFOUND) {
        git_error_clear();
        const int remove = git_index_remove_bypath(index, path.constData());
        if (remove == GIT_ENOTFOUND) {
            git_error_clear();
            return 0;
        }
        return remove;
    }
    if (lookup < 0)
        return lookup;

    git_index_entry restored {};
    restored.id = *git_tree_entry_id(entry.get());
    restored.mode = static_cast<uint32_t>(git_tree_entry_filemode(entry.get()));
    restored.path = path.constData();
    return git_index_add(index, &restored);
}

void addRef(QHash<QString, QStringList>& refs, const git_oid* oid,
            const QString& label)
{
    if (!oid || label.isEmpty())
        return;
    QStringList& labels = refs[oidText(oid)];
    if (!labels.contains(label))
        labels.append(label);
}

struct ReferenceSnapshot {
    QHash<QString, QStringList> labelsByCommit;
    QHash<QString, QStringList> remoteLabelsByCommit;
    QStringList branchRefs;
    QString version;
    bool valid {true};
};

ReferenceSnapshot referenceSnapshot(git_repository* repository, QString* error)
{
    ReferenceSnapshot result;
    QString currentHeadRef;
    ReferenceHandle head;
    if (git_repository_head(head.out(), repository) == 0)
        currentHeadRef = text(git_reference_name(head.get()));
    else
        git_error_clear();

    QStringList signatures;
    if (currentHeadRef.startsWith(QStringLiteral("refs/heads/")))
        signatures.append(QStringLiteral("HEAD->%1").arg(currentHeadRef));
    git_reference_iterator* iterator = nullptr;
    if (git_reference_iterator_new(&iterator, repository) < 0) {
        fail(error, QStringLiteral("Cannot enumerate repository references."));
        result.valid = false;
        git_error_clear();
    } else {
        git_reference* rawReference = nullptr;
        int nextResult = GIT_ITEROVER;
        while ((nextResult = git_reference_next(&rawReference, iterator)) == 0) {
            ReferenceHandle reference(rawReference);
            rawReference = nullptr;
            const QString fullName = text(git_reference_name(reference.get()));
            const bool local = fullName.startsWith(QStringLiteral("refs/heads/"));
            const bool remote = fullName.startsWith(QStringLiteral("refs/remotes/"));
            const bool tag = fullName.startsWith(QStringLiteral("refs/tags/"));
            if (!local && !remote && !tag)
                continue;

            ObjectHandle commitObject;
            if (git_reference_peel(commitObject.out(), reference.get(),
                                   GIT_OBJECT_COMMIT) < 0) {
                if (local || remote) {
                    fail(error, QStringLiteral("Cannot resolve branch reference."));
                    result.valid = false;
                    git_error_clear();
                    break;
                }
                git_error_clear();
                continue;
            }

            const git_oid* oid = git_object_id(commitObject.get());
            const QString hash = oidText(oid);
            signatures.append(fullName + QLatin1Char(':') + hash);
            if (local || remote)
                result.branchRefs.append(fullName);

            QString label;
            if (local) {
                const QString name = fullName.mid(11);
                label = fullName == currentHeadRef
                    ? QStringLiteral("HEAD -> %1").arg(name) : name;
            } else if (remote) {
                label = fullName.mid(13);
            } else {
                label = QStringLiteral("tag: %1").arg(fullName.mid(10));
            }
            addRef(result.labelsByCommit, oid, label);
            if (remote)
                addRef(result.remoteLabelsByCommit, oid, label);
        }
        git_reference_iterator_free(iterator);
        if (result.valid && nextResult != GIT_ITEROVER) {
            fail(error, QStringLiteral("Cannot enumerate repository references."));
            result.valid = false;
        }
        git_error_clear();
    }

    if (!result.valid)
        return result;

    if (git_repository_head_detached(repository) == 1) {
        ReferenceHandle detachedHead;
        if (git_repository_head(detachedHead.out(), repository) == 0) {
            const git_oid* oid = git_reference_target(detachedHead.get());
            addRef(result.labelsByCommit, oid, QStringLiteral("HEAD"));
            signatures.append(QStringLiteral("HEAD:%1").arg(oidText(oid)));
        } else {
            git_error_clear();
        }
    }

    std::sort(result.branchRefs.begin(), result.branchRefs.end());
    for (auto it = result.labelsByCommit.begin(); it != result.labelsByCommit.end(); ++it)
        std::sort(it.value().begin(), it.value().end());
    for (auto it = result.remoteLabelsByCommit.begin();
         it != result.remoteLabelsByCommit.end(); ++it) {
        std::sort(it.value().begin(), it.value().end());
    }
    std::sort(signatures.begin(), signatures.end());
    result.version = QString::fromLatin1(QCryptographicHash::hash(
        signatures.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
    return result;
}

void assignLane(Commit& commit, QVector<QString>& lanes)
{
    int lane = lanes.indexOf(commit.hash);
    if (lane < 0) {
        lane = lanes.indexOf(QString());
        if (lane < 0) {
            lane = lanes.size();
            lanes.append(QString());
        }
    }
    commit.lane = lane;
    for (int duplicate = lane + 1; duplicate < lanes.size(); ++duplicate) {
        if (lanes[duplicate] == commit.hash)
            lanes[duplicate].clear();
    }
    lanes[lane] = commit.parents.isEmpty() ? QString() : commit.parents.first();
    for (int parent = 1; parent < commit.parents.size(); ++parent) {
        if (lanes.contains(commit.parents[parent]))
            continue;
        int freeLane = lanes.indexOf(QString());
        if (freeLane < 0) {
            freeLane = lanes.size();
            lanes.append(QString());
        }
        lanes[freeLane] = commit.parents[parent];
    }
    commit.activeLanes = lanes;
    while (!commit.activeLanes.isEmpty() && commit.activeLanes.last().isEmpty())
        commit.activeLanes.removeLast();
}

bool commitTouchesPath(git_repository* repository, git_commit* commit,
                       const QString& path, bool* matches, QString* error)
{
    *matches = false;
    TreeHandle tree;
    if (git_commit_tree(tree.out(), commit) < 0)
        return fail(error, QStringLiteral("Cannot read commit tree."));

    const QByteArray pathBytes = path.toUtf8();
    char* pathValue = const_cast<char*>(pathBytes.constData());
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.pathspec.count = 1;
    options.pathspec.strings = &pathValue;
    options.flags = GIT_DIFF_INCLUDE_TYPECHANGE | GIT_DIFF_DISABLE_PATHSPEC_MATCH;

    const unsigned int parentCount = git_commit_parentcount(commit);
    const unsigned int comparisonCount = qMax(1U, parentCount);
    for (unsigned int parentIndex = 0; parentIndex < comparisonCount; ++parentIndex) {
        TreeHandle parentTree;
        if (parentCount > 0) {
            CommitHandle parent;
            if (git_commit_parent(parent.out(), commit, parentIndex) < 0
                || git_commit_tree(parentTree.out(), parent.get()) < 0)
                return fail(error, QStringLiteral("Cannot read parent commit tree."));
        }
        DiffHandle diff;
        if (git_diff_tree_to_tree(diff.out(), repository, parentTree.get(),
                                  tree.get(), &options) < 0)
            return fail(error, QStringLiteral("Cannot filter commit history by path."));
        if (git_diff_num_deltas(diff.get()) > 0) {
            *matches = true;
            return true;
        }
    }
    return true;
}

QString diffText(git_diff* diff, QString* error, const QString& fallback)
{
    git_buf output = GIT_BUF_INIT;
    const int rc = git_diff_to_buf(&output, diff, GIT_DIFF_FORMAT_PATCH);
    const QString result = rc == 0
        ? QString::fromUtf8(output.ptr ? output.ptr : "",
                            static_cast<int>(output.size))
        : QString();
    git_buf_dispose(&output);
    if (rc < 0)
        fail(error, fallback);
    return result;
}

void configureBundledOpenSslCertificates()
{
#ifdef GITMANAGER_BUNDLED_OPENSSL
    const QString certificatePath = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("certs/cacert.pem"));
    if (!QFileInfo(certificatePath).isFile()) {
        qWarning().noquote() << "Bundled OpenSSL CA certificate bundle is missing:"
                             << certificatePath;
        return;
    }

    const QByteArray nativePath = QDir::toNativeSeparators(certificatePath).toUtf8();
    if (git_libgit2_opts(GIT_OPT_SET_SSL_CERT_LOCATIONS,
                         nativePath.constData(), nullptr) < 0) {
        const git_error* error = git_error_last();
        qWarning().noquote() << "Cannot configure bundled OpenSSL CA certificate bundle:"
                             << (error && error->message
                                     ? QString::fromUtf8(error->message)
                                     : certificatePath);
        git_error_clear();
    }
#endif
}

} // namespace

LibGit2Backend::LibGit2Backend()
{
    if (git_libgit2_init() > 0)
        configureBundledOpenSslCertificates();
}

LibGit2Backend::~LibGit2Backend()
{
    close();
    git_libgit2_shutdown();
}

void LibGit2Backend::setCredentials(const RemoteCredentials& credentials)
{
    _credentials = credentials;
}

void LibGit2Backend::setProgressCallback(ProgressCallback callback)
{
    _progressCallback = std::move(callback);
}

void LibGit2Backend::setCancelFlag(const std::atomic_bool* cancelFlag)
{
    _cancelFlag = cancelFlag;
}

bool LibGit2Backend::open(const QString& path, QString* error)
{
    close();
    const QByteArray repositoryPath = QDir::toNativeSeparators(path).toUtf8();
    if (git_repository_open_ext(&_repository, repositoryPath.constData(),
                                GIT_REPOSITORY_OPEN_CROSS_FS, nullptr) < 0)
        return fail(error, QStringLiteral("Cannot open repository."));
    return true;
}

bool LibGit2Backend::initialize(const QString& path, QString* error)
{
    close();
    const QByteArray repositoryPath = QDir::toNativeSeparators(path).toUtf8();
    if (git_repository_init(&_repository, repositoryPath.constData(), 0) < 0)
        return fail(error, QStringLiteral("Cannot initialize repository."));
    return true;
}

bool LibGit2Backend::clone(const QString& url, const QString& path, QString* error)
{
    close();
    if (isCancelled()) {
        if (error)
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_clone_options options = GIT_CLONE_OPTIONS_INIT;
    options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE
                                             | GIT_CHECKOUT_RECREATE_MISSING;
    configureCallbacks(options.fetch_opts.callbacks, &context);

    const QByteArray remoteUrl = url.toUtf8();
    const QByteArray localPath = QDir::toNativeSeparators(path).toUtf8();
    progress(QStringLiteral("Cloning repository"), 0);
    if (git_clone(&_repository, remoteUrl.constData(), localPath.constData(),
                  &options) < 0) {
        close();
        return fail(error, isCancelled()
            ? QStringLiteral("Operation cancelled.")
            : QStringLiteral("Cannot clone repository."));
    }
    progress(QStringLiteral("Clone completed"), 100);
    return true;
}

void LibGit2Backend::close()
{
    git_repository_free(_repository);
    _repository = nullptr;
}

QString LibGit2Backend::rootPath() const
{
    if (!_repository)
        return {};
    const char* workdir = git_repository_workdir(_repository);
    return workdir ? QDir::cleanPath(QString::fromUtf8(workdir)) : QString();
}

RepositoryState LibGit2Backend::snapshot(QString* error) const
{
    RepositoryState result;
    if (!ensureRepository(_repository, error))
        return result;

    result.rootPath = rootPath();
    result.detached = git_repository_head_detached(_repository) == 1;
    result.unborn = git_repository_head_unborn(_repository) == 1;
    result.activeOperation = repositoryOperation(git_repository_state(_repository));

    ReferenceHandle head;
    const int headRc = git_repository_head(head.out(), _repository);
    if (headRc == 0) {
        result.headName = result.detached
            ? QStringLiteral("HEAD") : text(git_reference_shorthand(head.get()));
        result.headHash = oidText(git_reference_target(head.get()));
    } else if (result.unborn) {
        ReferenceHandle symbolic;
        if (git_reference_lookup(symbolic.out(), _repository, "HEAD") == 0)
            result.headName = branchNameFromRef(
                git_reference_symbolic_target(symbolic.get()));
        git_error_clear();
    } else if (headRc != GIT_ENOTFOUND) {
        fail(error, QStringLiteral("Cannot read repository HEAD."));
    }

    const ReferenceSnapshot refs = referenceSnapshot(_repository, error);
    if (!refs.valid)
        return result;
    result.refsVersion = refs.version;
    result.worktrees = worktrees(nullptr);
    git_branch_iterator* rawIterator = nullptr;
    if (git_branch_iterator_new(&rawIterator, _repository, GIT_BRANCH_ALL) == 0) {
        git_reference* rawBranch = nullptr;
        git_branch_t type = GIT_BRANCH_LOCAL;
        while (git_branch_next(&rawBranch, &type, rawIterator) == 0) {
            ReferenceHandle branch(rawBranch);
            rawBranch = nullptr;
            Branch value;
            value.fullName = text(git_reference_name(branch.get()));
            value.name = text(git_reference_shorthand(branch.get()));
            value.isRemote = type == GIT_BRANCH_REMOTE;
            value.isCurrent = !value.isRemote && git_branch_is_head(branch.get()) == 1;

            ObjectHandle commitObject;
            if (git_reference_peel(commitObject.out(), branch.get(),
                                   GIT_OBJECT_COMMIT) == 0) {
                value.hash = oidText(git_object_id(commitObject.get()));
            } else {
                git_error_clear();
            }

            if (!value.isRemote) {
                ReferenceHandle upstream;
                if (git_branch_upstream(upstream.out(), branch.get()) == 0) {
                    value.upstream = text(git_reference_shorthand(upstream.get()));
                    const git_oid* localOid = git_reference_target(branch.get());
                    const git_oid* upstreamOid = git_reference_target(upstream.get());
                    size_t ahead = 0;
                    size_t behind = 0;
                    if (localOid && upstreamOid
                        && git_graph_ahead_behind(&ahead, &behind, _repository,
                                                  localOid, upstreamOid) == 0) {
                        value.ahead = static_cast<int>(ahead);
                        value.behind = static_cast<int>(behind);
                    }
                    if (value.isCurrent) {
                        result.upstream = value.upstream;
                        result.ahead = value.ahead;
                        result.behind = value.behind;
                    }
                } else {
                    git_error_clear();
                }
            }
            result.branches.append(value);
        }
        git_branch_iterator_free(rawIterator);
        if (git_error_last() && !isCancelled())
            git_error_clear();
    } else {
        fail(error, QStringLiteral("Cannot enumerate branches."));
        git_error_clear();
    }

    git_status_options statusOptions = GIT_STATUS_OPTIONS_INIT;
    statusOptions.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    statusOptions.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
                        | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                        | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
                        | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
                        | GIT_STATUS_OPT_RENAMES_FROM_REWRITES;
    StatusListHandle statusList;
    if (git_status_list_new(statusList.out(), _repository, &statusOptions) < 0) {
        fail(error, QStringLiteral("Cannot read repository status."));
        return result;
    }

    const size_t statusCount = git_status_list_entrycount(statusList.get());
    result.files.reserve(static_cast<int>(statusCount));
    for (size_t index = 0; index < statusCount; ++index) {
        const git_status_entry* entry = git_status_byindex(statusList.get(), index);
        if (!entry)
            continue;
        const git_diff_delta* primary = entry->index_to_workdir
            ? entry->index_to_workdir : entry->head_to_index;
        if (!primary)
            continue;

        const char* currentPath = primary->new_file.path
            ? primary->new_file.path : primary->old_file.path;
        if (!currentPath)
            continue;

        File file;
        file.path = text(currentPath);
        file.indexStatus = statusValue(entry->status, true);
        file.workStatus = statusValue(entry->status, false);
        file.tracked = (entry->status & GIT_STATUS_WT_NEW) == 0;
        file.conflicted = (entry->status & GIT_STATUS_CONFLICTED) != 0;
        if (!file.tracked)
            file.workStatus = File::Status::E_Untracked;
        if (file.conflicted) {
            file.indexStatus = File::Status::E_Unmerged;
            file.workStatus = File::Status::E_Unmerged;
        }

        const git_diff_delta* renameDelta = nullptr;
        if (entry->index_to_workdir
            && entry->index_to_workdir->status == GIT_DELTA_RENAMED)
            renameDelta = entry->index_to_workdir;
        else if (entry->head_to_index
                 && entry->head_to_index->status == GIT_DELTA_RENAMED)
            renameDelta = entry->head_to_index;
        if (renameDelta && renameDelta->old_file.path)
            file.originalPath = text(renameDelta->old_file.path);
        file.submoduleState = submoduleState(_repository, file.path, primary);
        result.files.append(file);
    }
    result.submodules = submodules(nullptr);
    return result;
}

CommitHistoryPage LibGit2Backend::commitHistory(const CommitHistoryQuery& query,
                                                QString* error) const
{
    CommitHistoryPage result;
    if (!ensureRepository(_repository, error))
        return result;

    const ReferenceSnapshot refs = referenceSnapshot(_repository, error);
    if (!refs.valid)
        return result;
    result.refsVersion = refs.version;
    int offset = qMax(0, query.offset);
    if (offset > 0 && !query.expectedRefsVersion.isEmpty()
        && query.expectedRefsVersion != result.refsVersion) {
        offset = 0;
        result.resetRequired = true;
    }
    result.offset = offset;
    const int limit = qBound(1, query.limit, 1000);

    RevwalkHandle walk;
    if (git_revwalk_new(walk.out(), _repository) < 0) {
        fail(error, QStringLiteral("Cannot create commit history walker."));
        return result;
    }
    unsigned int sorting = GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME;
    if (query.oldestFirst)
        sorting |= GIT_SORT_REVERSE;
    git_revwalk_sorting(walk.get(), sorting);

    int pushResult = 0;
    if (query.branch == QLatin1String("*")) {
        bool pushed = false;
        for (const QString& branchRef : refs.branchRefs) {
            const QByteArray name = branchRef.toUtf8();
            const int rc = git_revwalk_push_ref(walk.get(), name.constData());
            if (rc == 0)
                pushed = true;
            else if (rc != GIT_ENOTFOUND) {
                pushResult = rc;
                break;
            }
            git_error_clear();
        }
        if (!pushed && pushResult == 0)
            return result;
    } else if (query.branch.isEmpty()) {
        if (git_repository_head_unborn(_repository) == 1) {
            git_error_clear();
            return result;
        }
        pushResult = git_revwalk_push_head(walk.get());
    } else {
        const QByteArray revision = query.branch.toUtf8();
        pushResult = git_revwalk_push_ref(walk.get(), revision.constData());
        if (pushResult < 0) {
            git_error_clear();
            ObjectHandle object;
            ObjectHandle commitObject;
            if (git_revparse_single(object.out(), _repository,
                                    revision.constData()) == 0
                && git_object_peel(commitObject.out(), object.get(),
                                   GIT_OBJECT_COMMIT) == 0) {
                pushResult = git_revwalk_push(walk.get(),
                                             git_object_id(commitObject.get()));
            }
        }
    }
    if ((pushResult == GIT_ENOTFOUND || pushResult == GIT_EUNBORNBRANCH)
        && query.branch.isEmpty()) {
        git_error_clear();
        return result;
    }
    if (pushResult < 0) {
        fail(error, QStringLiteral("Cannot resolve commit history branch."));
        return result;
    }

    const QString search = query.searchText.trimmed();
    const QString authorFilter = query.author.trimmed();
    QString path = QDir::fromNativeSeparators(query.path.trimmed());
    if (!path.isEmpty()) {
        if (QFileInfo(path).isAbsolute())
            path = QDir(rootPath()).relativeFilePath(path);
        path = QDir::cleanPath(path);
        if (path == QLatin1String("."))
            path.clear();
    }
    QVector<QString> lanes;
    int matchingIndex = 0;
    git_oid oid;
    int walkResult = GIT_ITEROVER;
    while ((walkResult = git_revwalk_next(&oid, walk.get())) == 0) {
        if (isCancelled()) {
            if (error)
                *error = QStringLiteral("Operation cancelled.");
            return result;
        }

        CommitHandle commit;
        if (git_commit_lookup(commit.out(), _repository, &oid) < 0) {
            fail(error, QStringLiteral("Cannot read commit history entry."));
            return result;
        }

        Commit value;
        value.hash = oidText(&oid);
        value.shortHash = value.hash.left(8);
        const unsigned int parentCount = git_commit_parentcount(commit.get());
        for (unsigned int parent = 0; parent < parentCount; ++parent)
            value.parents.append(oidText(git_commit_parent_id(commit.get(), parent)));
        const git_signature* author = git_commit_author(commit.get());
        value.authorName = author ? text(author->name) : QString();
        value.date = QDateTime::fromSecsSinceEpoch(git_commit_time(commit.get()), Qt::UTC);
        value.subject = text(git_commit_summary(commit.get()));
        value.refs = refs.labelsByCommit.value(value.hash);
        value.remoteRefs = refs.remoteLabelsByCommit.value(value.hash);
        if (!query.oldestFirst)
            assignLane(value, lanes);

        bool matches = true;
        if (query.fromDate.isValid() && value.date < query.fromDate)
            matches = false;
        if (query.toDate.isValid() && value.date > query.toDate)
            matches = false;
        if (matches && !authorFilter.isEmpty()) {
            const QString authorEmail = author ? text(author->email) : QString();
            matches = value.authorName.contains(authorFilter, Qt::CaseInsensitive)
                || authorEmail.contains(authorFilter, Qt::CaseInsensitive);
        }
        if (matches && !search.isEmpty()) {
            const QString message = text(git_commit_message(commit.get()));
            matches = value.hash.contains(search, Qt::CaseInsensitive)
                || message.contains(search, Qt::CaseInsensitive)
                || value.authorName.contains(search, Qt::CaseInsensitive)
                || value.refs.join(QLatin1Char(' ')).contains(search,
                                                           Qt::CaseInsensitive);
        }
        if (matches && !path.isEmpty()) {
            if (!commitTouchesPath(_repository, commit.get(), path, &matches, error))
                return result;
        }
        if (!matches)
            continue;

        if (matchingIndex++ < offset)
            continue;
        if (result.commits.size() < limit) {
            result.commits.append(value);
            continue;
        }
        result.hasMore = true;
        break;
    }

    if (!result.hasMore && walkResult != GIT_ITEROVER) {
        fail(error, QStringLiteral("Cannot walk commit history."));
        return result;
    }
    git_error_clear();
    return result;
}

QString LibGit2Backend::fileDiff(const QString& path, bool staged,
                                 bool untracked, QString* error) const
{
    if (!ensureRepository(_repository, error))
        return {};

    const QByteArray pathBytes = path.toUtf8();
    char* pathValue = const_cast<char*>(pathBytes.constData());
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.flags = GIT_DIFF_DISABLE_PATHSPEC_MATCH
                  | GIT_DIFF_INCLUDE_TYPECHANGE;
    options.pathspec.count = 1;
    options.pathspec.strings = &pathValue;
    options.context_lines = 3;
    if (untracked)
        options.flags |= GIT_DIFF_INCLUDE_UNTRACKED
                       | GIT_DIFF_RECURSE_UNTRACKED_DIRS
                       | GIT_DIFF_SHOW_UNTRACKED_CONTENT;

    DiffHandle diff;
    int rc = 0;
    if (staged) {
        TreeHandle headTree;
        lookupHeadTree(_repository, headTree);
        git_error_clear();
        rc = git_diff_tree_to_index(diff.out(), _repository, headTree.get(),
                                    nullptr, &options);
    } else {
        rc = git_diff_index_to_workdir(diff.out(), _repository, nullptr,
                                       &options);
    }
    if (rc < 0) {
        fail(error, QStringLiteral("Cannot create file diff."));
        return {};
    }
    return diffText(diff.get(), error, QStringLiteral("Cannot format file diff."));
}

QString LibGit2Backend::commitDiff(const QString& hash, QString* error) const
{
    if (!ensureRepository(_repository, error))
        return {};
    CommitHandle commit;
    if (!lookupCommit(_repository, hash, commit, error,
                      QStringLiteral("Cannot find commit.")))
        return {};

    TreeHandle tree;
    if (git_commit_tree(tree.out(), commit.get()) < 0) {
        fail(error, QStringLiteral("Cannot read commit tree."));
        return {};
    }
    TreeHandle parentTree;
    if (git_commit_parentcount(commit.get()) > 0) {
        CommitHandle parent;
        if (git_commit_parent(parent.out(), commit.get(), 0) < 0
            || git_commit_tree(parentTree.out(), parent.get()) < 0) {
            fail(error, QStringLiteral("Cannot read parent commit."));
            return {};
        }
    }

    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.flags = GIT_DIFF_INCLUDE_TYPECHANGE;
    DiffHandle diff;
    if (git_diff_tree_to_tree(diff.out(), _repository, parentTree.get(),
                              tree.get(), &options) < 0) {
        fail(error, QStringLiteral("Cannot create commit diff."));
        return {};
    }
    git_diff_find_options findOptions = GIT_DIFF_FIND_OPTIONS_INIT;
    findOptions.flags = GIT_DIFF_FIND_RENAMES;
    if (git_diff_find_similar(diff.get(), &findOptions) < 0)
        git_error_clear();

    QString output;
    output += QStringLiteral("commit %1\n").arg(oidText(git_commit_id(commit.get())));
    const git_signature* author = git_commit_author(commit.get());
    if (author) {
        output += QStringLiteral("Author: %1 <%2>\n")
                      .arg(text(author->name), text(author->email));
    }
    output += QStringLiteral("Date:   %1\n\n    %2\n\n")
                  .arg(QDateTime::fromSecsSinceEpoch(git_commit_time(commit.get()),
                                                     Qt::UTC)
                           .toLocalTime().toString(Qt::ISODate),
                       text(git_commit_summary(commit.get())));

    git_diff_stats* stats = nullptr;
    if (git_diff_get_stats(&stats, diff.get()) == 0) {
        git_buf statsBuffer = GIT_BUF_INIT;
        if (git_diff_stats_to_buf(&statsBuffer, stats, GIT_DIFF_STATS_FULL, 80) == 0)
            output += QString::fromUtf8(statsBuffer.ptr ? statsBuffer.ptr : "",
                                        static_cast<int>(statsBuffer.size)) + QLatin1Char('\n');
        git_buf_dispose(&statsBuffer);
        git_diff_stats_free(stats);
    } else {
        git_error_clear();
    }

    QString patchError;
    const QString patch = diffText(diff.get(), &patchError,
                                   QStringLiteral("Cannot format commit diff."));
    if (!patchError.isEmpty()) {
        if (error)
            *error = patchError;
        return {};
    }
    return output + patch;
}

QString LibGit2Backend::revisionDiff(const QString& baseRevision,
                                     const QString& targetRevision,
                                     QString* error) const
{
    if (!ensureRepository(_repository, error))
        return {};

    CommitHandle baseCommit;
    if (!lookupCommit(_repository, baseRevision, baseCommit, error,
                      QStringLiteral("Cannot find base revision.")))
        return {};

    CommitHandle targetCommit;
    if (!lookupCommit(_repository, targetRevision, targetCommit, error,
                      QStringLiteral("Cannot find target revision.")))
        return {};

    TreeHandle baseTree;
    if (git_commit_tree(baseTree.out(), baseCommit.get()) < 0) {
        fail(error, QStringLiteral("Cannot read base revision tree."));
        return {};
    }

    TreeHandle targetTree;
    if (git_commit_tree(targetTree.out(), targetCommit.get()) < 0) {
        fail(error, QStringLiteral("Cannot read target revision tree."));
        return {};
    }

    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.flags = GIT_DIFF_INCLUDE_TYPECHANGE;

    DiffHandle diff;
    if (git_diff_tree_to_tree(diff.out(), _repository, baseTree.get(),
                              targetTree.get(), &options) < 0) {
        fail(error, QStringLiteral("Cannot create revision diff."));
        return {};
    }

    git_diff_find_options findOptions = GIT_DIFF_FIND_OPTIONS_INIT;
    findOptions.flags = GIT_DIFF_FIND_RENAMES;
    if (git_diff_find_similar(diff.get(), &findOptions) < 0)
        git_error_clear();

    const QString resolvedBase = oidText(git_commit_id(baseCommit.get()));
    const QString resolvedTarget = oidText(git_commit_id(targetCommit.get()));

    QString output;
    output += QStringLiteral("compare %1..%2\n")
                  .arg(baseRevision, targetRevision);
    output += QStringLiteral("Base:   %1\n").arg(resolvedBase);
    output += QStringLiteral("Target: %1\n\n").arg(resolvedTarget);

    git_diff_stats* stats = nullptr;
    if (git_diff_get_stats(&stats, diff.get()) == 0) {
        git_buf statsBuffer = GIT_BUF_INIT;
        if (git_diff_stats_to_buf(&statsBuffer, stats, GIT_DIFF_STATS_FULL, 80) == 0)
            output += QString::fromUtf8(statsBuffer.ptr ? statsBuffer.ptr : "",
                                        static_cast<int>(statsBuffer.size))
                + QLatin1Char('\n');
        git_buf_dispose(&statsBuffer);
        git_diff_stats_free(stats);
    } else {
        git_error_clear();
    }

    QString patchError;
    const QString patch = diffText(diff.get(), &patchError,
                                   QStringLiteral("Cannot format revision diff."));
    if (!patchError.isEmpty()) {
        if (error)
            *error = patchError;
        return {};
    }
    return output + patch;
}

bool LibGit2Backend::stage(const QString& path, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return fail(error, QStringLiteral("Cannot open repository index."));

    const QString originalPath = renamedFrom(_repository, path, false);
    const QByteArray pathBytes = path.toUtf8();
    int rc = git_index_add_bypath(index.get(), pathBytes.constData());
    if (rc == GIT_ENOTFOUND) {
        git_error_clear();
        rc = git_index_remove_bypath(index.get(), pathBytes.constData());
        if (rc == GIT_ENOTFOUND) {
            git_error_clear();
            rc = 0;
        }
    }
    if (rc == 0 && !originalPath.isEmpty()) {
        rc = git_index_remove_bypath(index.get(),
                                     originalPath.toUtf8().constData());
        if (rc == GIT_ENOTFOUND) {
            git_error_clear();
            rc = 0;
        }
    }
    if (rc == 0)
        rc = git_index_write(index.get());
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot stage file."));
    return true;
}

bool LibGit2Backend::unstage(const QString& path, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    TreeHandle headTree;
    if (!lookupHeadTree(_repository, headTree)) {
        if (git_repository_head_unborn(_repository) != 1)
            return fail(error, QStringLiteral("Cannot resolve HEAD."));
        git_error_clear();
    }

    const QString originalPath = renamedFrom(_repository, path, true);
    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return fail(error, QStringLiteral("Cannot open repository index."));
    const QByteArray pathBytes = path.toUtf8();
    int rc = restoreIndexPath(index.get(), headTree.get(), pathBytes);
    if (rc == 0 && !originalPath.isEmpty())
        rc = restoreIndexPath(index.get(), headTree.get(), originalPath.toUtf8());
    if (rc == 0)
        rc = git_index_write(index.get());
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot unstage file."));
    return true;
}

bool LibGit2Backend::stageAll(QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return fail(error, QStringLiteral("Cannot open repository index."));
    git_strarray paths {};
    int rc = git_index_add_all(index.get(), &paths, GIT_INDEX_ADD_DEFAULT,
                               nullptr, nullptr);
    if (rc == 0)
        rc = git_index_write(index.get());
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot stage files."));
    return true;
}

bool LibGit2Backend::unstageAll(QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    ObjectHandle head;
    if (git_revparse_single(head.out(), _repository, "HEAD^{commit}") == 0) {
        if (git_reset(_repository, head.get(), GIT_RESET_MIXED, nullptr) < 0)
            return fail(error, QStringLiteral("Cannot unstage files."));
        return true;
    }
    if (git_repository_head_unborn(_repository) != 1)
        return fail(error, QStringLiteral("Cannot resolve HEAD."));
    git_error_clear();

    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0
        || git_index_clear(index.get()) < 0
        || git_index_write(index.get()) < 0)
        return fail(error, QStringLiteral("Cannot clear repository index."));
    return true;
}

bool LibGit2Backend::discard(const QString& path, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    const QString originalPath = renamedFrom(_repository, path, false);
    const QByteArray checkoutPath = originalPath.isEmpty()
        ? path.toUtf8() : originalPath.toUtf8();
    char* pathValue = const_cast<char*>(checkoutPath.constData());
    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = GIT_CHECKOUT_FORCE
                              | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
    options.paths.count = 1;
    options.paths.strings = &pathValue;
    if (git_checkout_index(_repository, nullptr, &options) < 0)
        return fail(error, QStringLiteral("Cannot discard working tree changes."));
    if (!originalPath.isEmpty()) {
        const QString renamedPath = QDir(rootPath()).filePath(path);
        if (QFileInfo::exists(renamedPath) && !QFile::remove(renamedPath)) {
            if (error)
                *error = QStringLiteral("Cannot remove renamed working tree path.");
            return false;
        }
    }
    return true;
}

bool LibGit2Backend::removeUntracked(const QString& path, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;

    unsigned int status = 0;
    const QByteArray pathBytes = path.toUtf8();
    const int statusRc = git_status_file(&status, _repository,
                                         pathBytes.constData());
    if (statusRc < 0 || !(status & GIT_STATUS_WT_NEW)) {
        if (statusRc < 0)
            git_error_clear();
        if (error)
            *error = QStringLiteral("Refusing to remove a tracked or unknown path.");
        return false;
    }

    const QString root = QDir::cleanPath(rootPath());
    const QString absolute = QDir::cleanPath(QDir(root).absoluteFilePath(path));
    const QString rootPrefix = root.endsWith(QDir::separator())
        ? root : root + QDir::separator();
#ifdef Q_OS_WIN
    const bool inside = absolute.startsWith(rootPrefix, Qt::CaseInsensitive);
#else
    const bool inside = absolute.startsWith(rootPrefix);
#endif
    if (!inside) {
        if (error)
            *error = QStringLiteral("Path is outside the repository.");
        return false;
    }

    QFileInfo info(absolute);
    bool removed = false;
    if (info.isDir() && !info.isSymLink())
        removed = QDir(absolute).removeRecursively();
    else
        removed = QFile::remove(absolute);
    if (!removed && QFileInfo::exists(absolute)) {
        if (error)
            *error = QStringLiteral("Cannot remove untracked path: %1").arg(path);
        return false;
    }
    return true;
}

bool LibGit2Backend::applyPatch(const QString& patch, bool cached,
                                bool reverse, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    const QByteArray data = (reverse ? reversePatch(patch) : patch).toUtf8();
    DiffHandle diff;
    if (git_diff_from_buffer(diff.out(), data.constData(),
                             static_cast<size_t>(data.size())) < 0)
        return fail(error, QStringLiteral("Cannot parse patch."));

    git_apply_options options = GIT_APPLY_OPTIONS_INIT;
    if (git_apply(_repository, diff.get(),
                  cached ? GIT_APPLY_LOCATION_INDEX
                         : GIT_APPLY_LOCATION_WORKDIR,
                  &options) < 0)
        return fail(error, QStringLiteral("Cannot apply patch."));
    return true;
}

bool LibGit2Backend::commit(const QString& message, bool amend, bool signoff,
                            QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (message.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Commit message cannot be empty.");
        return false;
    }
    if (git_repository_state(_repository) != GIT_REPOSITORY_STATE_NONE) {
        if (error)
            *error = QStringLiteral("Use Continue to finish the active repository operation.");
        return false;
    }

    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0)
        return fail(error, QStringLiteral("Git user.name and user.email are required."));

    QString finalMessage = message;
    if (signoff) {
        const QString trailer = QStringLiteral("Signed-off-by: %1 <%2>")
            .arg(text(signature.get()->name), text(signature.get()->email));
        if (!finalMessage.contains(trailer, Qt::CaseInsensitive)) {
            while (finalMessage.endsWith(QLatin1Char('\n')))
                finalMessage.chop(1);
            finalMessage += QStringLiteral("\n\n") + trailer;
        }
    }
    const QByteArray messageBytes = finalMessage.toUtf8();
    git_oid oid;

    if (!amend) {
        git_commit_create_options options = GIT_COMMIT_CREATE_OPTIONS_INIT;
        options.author = signature.get();
        options.committer = signature.get();
        if (git_commit_create_from_stage(&oid, _repository,
                                         messageBytes.constData(),
                                         &options) < 0)
            return fail(error, QStringLiteral("Cannot create commit."));
        return true;
    }

    CommitHandle head;
    if (!lookupHeadCommit(_repository, head))
        return fail(error, QStringLiteral("Cannot amend an unborn HEAD."));
    IndexHandle index;
    git_oid treeOid;
    TreeHandle tree;
    if (git_repository_index(index.out(), _repository) < 0
        || git_index_write_tree(&treeOid, index.get()) < 0
        || git_tree_lookup(tree.out(), _repository, &treeOid) < 0)
        return fail(error, QStringLiteral("Cannot create amended tree."));
    if (git_commit_amend(&oid, head.get(), "HEAD", nullptr, signature.get(),
                         nullptr, messageBytes.constData(), tree.get()) < 0)
        return fail(error, QStringLiteral("Cannot amend commit."));
    return true;
}

HistoryRewritePreview LibGit2Backend::resetPreview(const QString& revision,
                                                   QString* error) const
{
    HistoryRewritePreview preview;
    preview.revision = revision.trimmed();
    if (!ensureRepository(_repository, error))
        return preview;

    preview.activeOperation = repositoryOperation(git_repository_state(_repository));
    if (!repositoryDirty(_repository, &preview.dirty, error))
        return preview;

    CommitHandle head;
    CommitHandle target;
    if (!lookupHeadCommit(_repository, head)) {
        fail(error, QStringLiteral("Cannot prepare a history operation on an unborn HEAD."));
        return preview;
    }
    if (!lookupCommit(_repository, preview.revision, target, error,
                      QStringLiteral("Cannot resolve history operation target."))) {
        return preview;
    }

    preview.expectedHead = oidText(git_commit_id(head.get()));
    preview.targetHash = oidText(git_commit_id(target.get()));
    if (!headDetails(_repository, nullptr, &preview.currentBranch,
                     &preview.upstream, error)) {
        return {};
    }

    QString affectedError;
    const QVector<QString> affected = commitsBetween(
        _repository, git_commit_id(head.get()), git_commit_id(target.get()),
        &affectedError);
    if (!affectedError.isEmpty()) {
        if (error)
            *error = affectedError;
        return {};
    }
    preview.affectedCount = affected.size();

    QString publishedError;
    const QSet<QString> published = remoteReachableCommits(_repository,
                                                           &publishedError);
    if (!publishedError.isEmpty()) {
        if (error)
            *error = publishedError;
        return {};
    }
    for (const QString& hash : affected) {
        if (published.contains(hash))
            ++preview.publishedCount;
    }
    return preview;
}

RebasePlan LibGit2Backend::rebasePlan(const QString& revision,
                                      QString* error) const
{
    RebasePlan plan;
    plan.preview = resetPreview(revision, error);
    if (!ensureRepository(_repository, error)
        || plan.preview.expectedHead.isEmpty()
        || plan.preview.targetHash.isEmpty()) {
        return plan;
    }

    CommitHandle head;
    CommitHandle target;
    if (!lookupHeadCommit(_repository, head)
        || !lookupCommit(_repository, plan.preview.targetHash, target, error,
                         QStringLiteral("Cannot resolve rebase target."))) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Cannot resolve rebase revisions.");
        return {};
    }

    QString rangeError;
    const QVector<QString> range = commitsBetween(
        _repository, git_commit_id(head.get()), git_commit_id(target.get()),
        &rangeError);
    if (!rangeError.isEmpty()) {
        if (error)
            *error = rangeError;
        return {};
    }
    for (const QString& hash : range) {
        CommitHandle commit;
        if (!lookupCommit(_repository, hash, commit, error,
                          QStringLiteral("Cannot inspect rebase commit."))) {
            return {};
        }
        if (git_commit_parentcount(commit.get()) > 1) {
            if (error) {
                *error = QStringLiteral(
                    "This rebase range contains merge commit %1; preserving merge topology is not supported yet.")
                             .arg(hash.left(8));
            }
            return {};
        }
    }

    AnnotatedHandle upstream;
    if (git_annotated_commit_lookup(upstream.out(), _repository,
                                    git_commit_id(target.get())) < 0) {
        historyFailure(error, QStringLiteral("Cannot prepare rebase target."));
        return {};
    }
    git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
    options.inmemory = 1;
    RebaseHandle rebase;
    if (git_rebase_init(rebase.out(), _repository, nullptr, upstream.get(),
                        nullptr, &options) < 0) {
        historyFailure(error, QStringLiteral("Cannot prepare rebase plan."));
        return {};
    }

    QString publishedError;
    const QSet<QString> published = remoteReachableCommits(_repository,
                                                           &publishedError);
    if (!publishedError.isEmpty()) {
        if (error)
            *error = publishedError;
        return {};
    }

    const size_t count = git_rebase_operation_entrycount(rebase.get());
    plan.items.reserve(static_cast<int>(count));
    plan.preview.publishedCount = 0;
    for (size_t index = 0; index < count; ++index) {
        const git_rebase_operation* operation =
            git_rebase_operation_byindex(rebase.get(), index);
        if (!operation) {
            if (error)
                *error = QStringLiteral("Cannot inspect rebase operation.");
            return {};
        }
        CommitHandle commit;
        if (git_commit_lookup(commit.out(), _repository, &operation->id) < 0) {
            historyFailure(error, QStringLiteral("Cannot inspect rebase commit."));
            return {};
        }
        RebasePlanItem item;
        item.hash = oidText(git_commit_id(commit.get()));
        item.subject = text(git_commit_summary(commit.get()));
        item.message = text(git_commit_message(commit.get()));
        item.rewrittenMessage = item.message;
        item.parentCount = static_cast<int>(git_commit_parentcount(commit.get()));
        item.published = published.contains(item.hash);
        if (item.published)
            ++plan.preview.publishedCount;
        plan.items.append(item);
    }
    plan.preview.affectedCount = plan.items.size();
    return plan;
}

HistoryOperationResult LibGit2Backend::mergeRevision(const QString& revision,
                                                     QString* error)
{
    if (!ensureRepository(_repository, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    if (!ensureHistoryOperationStart(_repository, {}, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());

    AnnotatedHandle target;
    if (git_annotated_commit_from_revspec(target.out(), _repository,
                                          revision.toUtf8().constData()) < 0) {
        return historyFailure(error, QStringLiteral("Cannot resolve merge target."));
    }
    const git_annotated_commit* targets[] = {target.get()};
    git_merge_analysis_t analysis = GIT_MERGE_ANALYSIS_NONE;
    git_merge_preference_t preference = GIT_MERGE_PREFERENCE_NONE;
    if (git_merge_analysis(&analysis, &preference, _repository, targets, 1) < 0)
        return historyFailure(error, QStringLiteral("Cannot analyze merge."));

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        return historyResult(HistoryOperationStatus::UpToDate,
                             QStringLiteral("Already up to date."));
    }

    if ((analysis & (GIT_MERGE_ANALYSIS_FASTFORWARD
                     | GIT_MERGE_ANALYSIS_UNBORN))
        && !(preference & GIT_MERGE_PREFERENCE_NO_FASTFORWARD)) {
        CommitHandle targetCommit;
        if (git_commit_lookup(targetCommit.out(), _repository,
                              git_annotated_commit_id(target.get())) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot resolve fast-forward target."));
        }
        ReferenceHandle head;
        if (git_repository_head(head.out(), _repository) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot resolve HEAD for fast-forward."));
        }
        const char* headName = git_reference_name(head.get());
        const git_oid* headOid = git_reference_target(head.get());
        if (!headName || !headOid) {
            if (error) {
                *error = QStringLiteral(
                    "Fast-forward merge requires a direct local branch HEAD.");
            }
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }

        TransactionHandle transaction;
        if (git_transaction_new(transaction.out(), _repository) < 0
            || git_transaction_lock_ref(transaction.get(), headName) < 0) {
            return historyFailure(
                error,
                QStringLiteral("Cannot lock the current branch for fast-forward."));
        }

        ReferenceHandle freshHead;
        if (git_repository_head(freshHead.out(), _repository) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot re-check HEAD before fast-forward."));
        }
        const char* freshHeadName = git_reference_name(freshHead.get());
        const git_oid* freshHeadOid = git_reference_target(freshHead.get());
        if (!freshHeadName || !freshHeadOid
            || std::strcmp(freshHeadName, headName) != 0
            || git_oid_cmp(freshHeadOid, headOid) != 0) {
            if (error) {
                *error = QStringLiteral(
                    "HEAD changed while preparing the fast-forward merge. Refresh and try again.");
            }
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        if (git_transaction_set_target(transaction.get(), headName,
                                       git_commit_id(targetCommit.get()),
                                       nullptr, "merge: fast-forward") < 0) {
            return historyFailure(
                error,
                QStringLiteral("Cannot queue the fast-forward branch update."));
        }

        CommitHandle originalCommit;
        if (git_commit_lookup(originalCommit.out(), _repository, freshHeadOid) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot capture the current branch commit."));
        }
        TreeHandle originalTree;
        if (git_commit_tree(originalTree.out(), originalCommit.get()) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot capture the current branch tree."));
        }
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        checkout.checkout_strategy = GIT_CHECKOUT_SAFE
                                   | GIT_CHECKOUT_RECREATE_MISSING;
        if (git_checkout_tree(_repository,
                              reinterpret_cast<git_object*>(targetCommit.get()),
                              &checkout) < 0) {
            return historyFailure(error,
                                  QStringLiteral("Cannot check out fast-forward target."));
        }

        if (git_transaction_commit(transaction.get()) < 0) {
            git_checkout_options rollback = GIT_CHECKOUT_OPTIONS_INIT;
            rollback.checkout_strategy = GIT_CHECKOUT_FORCE
                                       | GIT_CHECKOUT_RECREATE_MISSING;
            git_checkout_tree(_repository,
                              reinterpret_cast<git_object*>(originalTree.get()),
                              &rollback);
            git_reset(_repository,
                      reinterpret_cast<git_object*>(originalCommit.get()),
                      GIT_RESET_MIXED, nullptr);
            return historyFailure(
                error,
                QStringLiteral("Cannot update the current branch after fast-forward."));
        }
        progress(QStringLiteral("Fast-forward merge completed"), 100);
        return historyResult(HistoryOperationStatus::Completed,
                             QStringLiteral("Fast-forward completed."));
    }

    const bool forceMergeCommit =
        (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)
        && (preference & GIT_MERGE_PREFERENCE_NO_FASTFORWARD);
    if (!(analysis & GIT_MERGE_ANALYSIS_NORMAL) && !forceMergeCommit) {
        if (error)
            *error = QStringLiteral("No supported merge is available.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (preference & GIT_MERGE_PREFERENCE_FASTFORWARD_ONLY) {
        if (error)
            *error = QStringLiteral("The repository is configured for fast-forward-only merges.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    git_merge_options mergeOptions = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_SAFE
                               | GIT_CHECKOUT_RECREATE_MISSING
                               | GIT_CHECKOUT_ALLOW_CONFLICTS;
    if (git_merge(_repository, targets, 1, &mergeOptions, &checkout) < 0)
        return historyFailure(error, QStringLiteral("Cannot merge revision."));

    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return historyFailure(error, QStringLiteral("Cannot open merge index."));
    if (git_index_has_conflicts(index.get())) {
        return historyResult(HistoryOperationStatus::Conflicts,
                             QStringLiteral("Merge stopped because of conflicts."));
    }
    if (!createStateCommit(QStringLiteral("merge"), error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    progress(QStringLiteral("Merge completed"), 100);
    return historyResult(HistoryOperationStatus::Completed,
                         QStringLiteral("Merge completed."));
}

HistoryOperationResult LibGit2Backend::rebaseOnto(const RebasePlan& requestedPlan,
                                                  bool interactive,
                                                  QString* error)
{
    if (!ensureRepository(_repository, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    if (requestedPlan.preview.expectedHead.isEmpty()
        || requestedPlan.preview.targetHash.isEmpty()) {
        if (error)
            *error = QStringLiteral("The rebase plan is incomplete.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (!ensureHistoryOperationStart(_repository,
                                     requestedPlan.preview.expectedHead, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    QString currentBranch;
    if (!headDetails(_repository, nullptr, &currentBranch, nullptr, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (currentBranch != requestedPlan.preview.currentBranch) {
        if (error) {
            *error = QStringLiteral(
                "The current branch changed after the rebase plan was prepared. Refresh and try again.");
        }
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    CommitHandle resolvedTarget;
    const QString targetRevision = requestedPlan.preview.revision.isEmpty()
        ? requestedPlan.preview.targetHash : requestedPlan.preview.revision;
    if (!lookupCommit(_repository, targetRevision, resolvedTarget, error,
                      QStringLiteral("Cannot resolve rebase target."))) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (oidText(git_commit_id(resolvedTarget.get())).compare(
            requestedPlan.preview.targetHash, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("The rebase target changed after the plan was prepared.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    QString planError;
    RebasePlan effective = rebasePlan(requestedPlan.preview.targetHash, &planError);
    if (!planError.isEmpty()) {
        if (error)
            *error = planError;
        return historyResult(HistoryOperationStatus::Completed, planError);
    }
    if (effective.preview.expectedHead.compare(
            requestedPlan.preview.expectedHead, Qt::CaseInsensitive) != 0
        || effective.preview.currentBranch != requestedPlan.preview.currentBranch
        || effective.items.size() != requestedPlan.items.size()) {
        if (error)
            *error = QStringLiteral("The rebase plan is stale. Refresh and prepare it again.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    bool hasEffectiveCommit = false;
    for (int index = 0; index < effective.items.size(); ++index) {
        if (effective.items[index].hash.compare(requestedPlan.items[index].hash,
                                                Qt::CaseInsensitive) != 0) {
            if (error)
                *error = QStringLiteral("The rebase plan order is invalid.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        const RebaseAction action = interactive
            ? requestedPlan.items[index].action : RebaseAction::Pick;
        if (static_cast<int>(action) < static_cast<int>(RebaseAction::Pick)
            || static_cast<int>(action) > static_cast<int>(RebaseAction::Drop)) {
            if (error)
                *error = QStringLiteral("The rebase plan contains an invalid action.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        if (!hasEffectiveCommit && action != RebaseAction::Drop) {
            if (action == RebaseAction::Squash || action == RebaseAction::Fixup) {
                if (error) {
                    *error = QStringLiteral(
                        "The first non-dropped rebase item cannot be squash or fixup.");
                }
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            }
            hasEffectiveCommit = true;
        }
        const QString requestedMessage =
            requestedPlan.items[index].rewrittenMessage.isEmpty()
                ? requestedPlan.items[index].message
                : requestedPlan.items[index].rewrittenMessage;
        if ((action == RebaseAction::Reword
             || action == RebaseAction::Squash)
            && requestedMessage.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral(
                    "Reword and squash actions require a commit message.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        effective.items[index].action = action;
        effective.items[index].rewrittenMessage =
            requestedPlan.items[index].rewrittenMessage;
    }
    if (effective.items.isEmpty()) {
        return historyResult(HistoryOperationStatus::UpToDate,
                             QStringLiteral("No commits need to be rebased."));
    }
    if (!removeInteractiveRebase(_repository, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    AnnotatedHandle upstream;
    if (git_annotated_commit_lookup(upstream.out(), _repository,
                                    git_commit_id(resolvedTarget.get())) < 0) {
        return historyFailure(error, QStringLiteral("Cannot prepare rebase target."));
    }
    git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
    options.checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE
                                               | GIT_CHECKOUT_RECREATE_MISSING;
    RebaseHandle rebase;
    if (git_rebase_init(rebase.out(), _repository, nullptr, upstream.get(),
                        nullptr, &options) < 0) {
        return historyFailure(error, QStringLiteral("Cannot start rebase."));
    }

    if (!interactive)
        return finishRebaseResult(rebase.get(), error);
    InteractiveRebaseState state;
    state.plan = effective;
    if (!saveInteractiveRebase(_repository, state, error)) {
        git_rebase_abort(rebase.get());
        QString cleanupError;
        removeInteractiveRebase(_repository, &cleanupError);
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    return finishInteractiveRebase(rebase.get(), std::move(state), error);
}

HistoryOperationResult LibGit2Backend::cherryPickCommit(const QString& revision,
                                                        unsigned int mainline,
                                                        QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureHistoryOperationStart(_repository, {}, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    CommitHandle commit;
    if (!lookupCommit(_repository, revision, commit, error,
                      QStringLiteral("Cannot resolve cherry-pick commit."))) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    git_cherrypick_options options = GIT_CHERRYPICK_OPTIONS_INIT;
    options.mainline = mainline;
    options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE
                                             | GIT_CHECKOUT_RECREATE_MISSING
                                             | GIT_CHECKOUT_ALLOW_CONFLICTS;
    if (git_cherrypick(_repository, commit.get(), &options) < 0)
        return historyFailure(error, QStringLiteral("Cannot cherry-pick commit."));

    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return historyFailure(error, QStringLiteral("Cannot open cherry-pick index."));
    if (git_index_has_conflicts(index.get())) {
        return historyResult(HistoryOperationStatus::Conflicts,
                             QStringLiteral("Cherry-pick stopped because of conflicts."));
    }
    if (!createStateCommit(QStringLiteral("cherry-pick"), error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    return historyResult(HistoryOperationStatus::Completed,
                         QStringLiteral("Cherry-pick completed."));
}

HistoryOperationResult LibGit2Backend::revertCommit(const QString& revision,
                                                    unsigned int mainline,
                                                    QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureHistoryOperationStart(_repository, {}, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    CommitHandle commit;
    if (!lookupCommit(_repository, revision, commit, error,
                      QStringLiteral("Cannot resolve revert commit."))) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    git_revert_options options = GIT_REVERT_OPTIONS_INIT;
    options.mainline = mainline;
    options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE
                                             | GIT_CHECKOUT_RECREATE_MISSING
                                             | GIT_CHECKOUT_ALLOW_CONFLICTS;
    if (git_revert(_repository, commit.get(), &options) < 0)
        return historyFailure(error, QStringLiteral("Cannot revert commit."));

    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return historyFailure(error, QStringLiteral("Cannot open revert index."));
    if (git_index_has_conflicts(index.get())) {
        return historyResult(HistoryOperationStatus::Conflicts,
                             QStringLiteral("Revert stopped because of conflicts."));
    }
    if (!createStateCommit(QStringLiteral("revert"), error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    return historyResult(HistoryOperationStatus::Completed,
                         QStringLiteral("Revert completed."));
}

HistoryOperationResult LibGit2Backend::resetToCommit(
    const HistoryRewritePreview& preview, ResetMode mode, QString* error)
{
    if (!ensureRepository(_repository, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    if (git_repository_state(_repository) != GIT_REPOSITORY_STATE_NONE) {
        if (error)
            *error = QStringLiteral("Abort or finish the active repository operation first.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    QString currentHead;
    QString currentBranch;
    if (!headDetails(_repository, &currentHead, &currentBranch, nullptr, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    if (preview.expectedHead.isEmpty()
        || currentHead.compare(preview.expectedHead, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("HEAD changed after reset was previewed. Refresh and try again.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (currentBranch != preview.currentBranch) {
        if (error) {
            *error = QStringLiteral(
                "The current branch changed after reset was previewed. Refresh and try again.");
        }
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    CommitHandle target;
    if (!lookupCommit(_repository, preview.targetHash, target, error,
                      QStringLiteral("Cannot resolve reset target."))) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    git_reset_t resetType = GIT_RESET_MIXED;
    switch (mode) {
    case ResetMode::Soft:  resetType = GIT_RESET_SOFT; break;
    case ResetMode::Mixed: resetType = GIT_RESET_MIXED; break;
    case ResetMode::Hard:  resetType = GIT_RESET_HARD; break;
    }
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_FORCE
                               | GIT_CHECKOUT_RECREATE_MISSING;
    if (git_reset(_repository, reinterpret_cast<git_object*>(target.get()),
                  resetType, &checkout) < 0) {
        return historyFailure(error, QStringLiteral("Cannot reset HEAD."));
    }
    return historyResult(HistoryOperationStatus::Completed,
                         QStringLiteral("Reset completed."));
}

bool LibGit2Backend::checkoutBranch(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    QByteArray branchName = name.toUtf8();
    ReferenceHandle branch;
    bool createdTrackingBranch = false;
    if (git_branch_lookup(branch.out(), _repository, branchName.constData(),
                          GIT_BRANCH_LOCAL) < 0) {
        git_error_clear();
        ReferenceHandle remote;
        if (git_branch_lookup(remote.out(), _repository, branchName.constData(),
                              GIT_BRANCH_REMOTE) < 0)
            return fail(error, QStringLiteral("Cannot find branch."));

        CommitHandle target;
        ObjectHandle object;
        if (git_reference_peel(object.out(), remote.get(), GIT_OBJECT_COMMIT) < 0
            || git_commit_lookup(target.out(), _repository,
                                 git_object_id(object.get())) < 0)
            return fail(error, QStringLiteral("Cannot resolve remote branch."));

        QString localName = name.section(QLatin1Char('/'), 1, -1);
        if (localName.isEmpty())
            localName = name;
        branchName = localName.toUtf8();
        if (git_branch_create(branch.out(), _repository, branchName.constData(),
                              target.get(), 0) < 0)
            return fail(error, QStringLiteral("Cannot create tracking branch."));
        createdTrackingBranch = true;
        if (git_branch_set_upstream(branch.get(), name.toUtf8().constData()) < 0) {
            git_branch_delete(branch.get());
            return fail(error, QStringLiteral("Cannot configure branch upstream."));
        }
    }

    ObjectHandle target;
    if (git_reference_peel(target.out(), branch.get(), GIT_OBJECT_COMMIT) < 0)
        return fail(error, QStringLiteral("Cannot resolve branch target."));
    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_RECREATE_MISSING;
    if (git_checkout_tree(_repository, target.get(), &options) < 0) {
        if (createdTrackingBranch)
            git_branch_delete(branch.get());
        return fail(error, QStringLiteral("Local changes prevent checkout."));
    }
    if (git_repository_set_head(_repository, git_reference_name(branch.get())) < 0) {
        if (createdTrackingBranch)
            git_branch_delete(branch.get());
        return fail(error, QStringLiteral("Cannot update HEAD."));
    }
    return true;
}

bool LibGit2Backend::createBranch(const QString& name, const QString& from,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    CommitHandle target;
    const QString revision = from.isEmpty() ? QStringLiteral("HEAD") : from;
    if (!lookupCommit(_repository, revision, target, error,
                      QStringLiteral("Cannot resolve branch start point.")))
        return false;

    const QByteArray branchName = name.toUtf8();
    ReferenceHandle branch;
    if (git_branch_create(branch.out(), _repository, branchName.constData(),
                          target.get(), 0) < 0)
        return fail(error, QStringLiteral("Cannot create branch."));

    ObjectHandle targetObject;
    if (git_object_lookup(targetObject.out(), _repository,
                          git_commit_id(target.get()), GIT_OBJECT_COMMIT) < 0)
        return fail(error, QStringLiteral("Cannot resolve branch target."));
    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_RECREATE_MISSING;
    if (git_checkout_tree(_repository, targetObject.get(), &options) < 0) {
        git_branch_delete(branch.get());
        return fail(error, QStringLiteral("Local changes prevent checkout."));
    }
    if (git_repository_set_head(_repository, git_reference_name(branch.get())) < 0)
        return fail(error, QStringLiteral("Cannot update HEAD."));
    return true;
}

bool LibGit2Backend::deleteBranch(const QString& name, bool force,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    const QByteArray branchName = name.toUtf8();
    ReferenceHandle branch;
    if (git_branch_lookup(branch.out(), _repository, branchName.constData(),
                          GIT_BRANCH_LOCAL) < 0)
        return fail(error, QStringLiteral("Cannot find local branch."));
    if (git_branch_is_head(branch.get()) == 1) {
        if (error)
            *error = QStringLiteral("Cannot delete the current branch.");
        return false;
    }

    if (!force) {
        ReferenceHandle head;
        if (git_repository_head(head.out(), _repository) < 0)
            return fail(error, QStringLiteral("Cannot resolve HEAD."));
        const git_oid* headOid = git_reference_target(head.get());
        const git_oid* branchOid = git_reference_target(branch.get());
        const int merged = headOid && branchOid
            ? git_graph_descendant_of(_repository, headOid, branchOid) : 0;
        if (merged != 1 && (!headOid || !branchOid
                            || git_oid_cmp(headOid, branchOid) != 0)) {
            if (merged < 0)
                git_error_clear();
            if (error)
                *error = QStringLiteral("Branch is not fully merged. Use force to delete it.");
            return false;
        }
    }
    if (git_branch_delete(branch.get()) < 0)
        return fail(error, QStringLiteral("Cannot delete branch."));
    return true;
}

bool LibGit2Backend::renameBranch(const QString& name, const QString& newName,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty() || newName.trimmed().isEmpty())
        return fail(error, QStringLiteral("Branch names cannot be empty."));

    ReferenceHandle branch;
    if (git_branch_lookup(branch.out(), _repository, name.toUtf8().constData(),
                          GIT_BRANCH_LOCAL) < 0)
        return fail(error, QStringLiteral("Cannot find local branch."));

    ReferenceHandle renamed;
    if (git_branch_move(renamed.out(), branch.get(),
                        newName.toUtf8().constData(), 0) < 0)
        return fail(error, QStringLiteral("Cannot rename branch."));
    return true;
}

bool LibGit2Backend::unsetBranchUpstream(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;

    ReferenceHandle branch;
    if (git_branch_lookup(branch.out(), _repository, name.toUtf8().constData(),
                          GIT_BRANCH_LOCAL) < 0)
        return fail(error, QStringLiteral("Cannot find local branch."));
    if (git_branch_set_upstream(branch.get(), nullptr) < 0)
        return fail(error, QStringLiteral("Cannot remove branch upstream."));
    return true;
}

bool LibGit2Backend::createTag(const QString& name, const QString& target,
                               const QString& message, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    const QByteArray revision = (target.isEmpty() ? QStringLiteral("HEAD")
                                                   : target).toUtf8();
    ObjectHandle object;
    if (git_revparse_single(object.out(), _repository, revision.constData()) < 0)
        return fail(error, QStringLiteral("Cannot resolve tag target."));

    const QByteArray tagName = name.toUtf8();
    git_oid oid;
    int rc = 0;
    if (message.isEmpty()) {
        rc = git_tag_create_lightweight(&oid, _repository, tagName.constData(),
                                        object.get(), 0);
    } else {
        SignatureHandle signature;
        if (git_signature_default(signature.out(), _repository) < 0)
            return fail(error, QStringLiteral("Git user.name and user.email are required."));
        const QByteArray tagMessage = message.toUtf8();
        rc = git_tag_create(&oid, _repository, tagName.constData(), object.get(),
                            signature.get(), tagMessage.constData(), 0);
    }
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot create tag."));
    return true;
}

bool LibGit2Backend::deleteTag(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (git_tag_delete(_repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot delete tag."));
    return true;
}

bool LibGit2Backend::pushTag(const QString& remoteName, const QString& tagName,
                             QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (isCancelled()) {
        if (error)
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }
    if (remoteName.trimmed().isEmpty() || tagName.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Remote and tag name are required.");
        return false;
    }

    ReferenceHandle tagRef;
    const QByteArray localTagRef = QStringLiteral("refs/tags/%1").arg(tagName).toUtf8();
    if (git_reference_lookup(tagRef.out(), _repository, localTagRef.constData()) < 0)
        return fail(error, QStringLiteral("Cannot find local tag."));

    RemoteHandle remote;
    if (git_remote_lookup(remote.out(), _repository,
                          remoteName.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find push remote."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    configureCallbacks(options.callbacks, &context);

    const QByteArray refspecValue = QStringLiteral("refs/tags/%1:refs/tags/%1")
        .arg(tagName).toUtf8();
    char* refspecPointer = const_cast<char*>(refspecValue.constData());
    git_strarray refspecs {&refspecPointer, 1};
    progress(QStringLiteral("Pushing tag %1").arg(tagName), 0);
    if (git_remote_push(remote.get(), &refspecs, &options) < 0) {
        if (error && !context.pushError.isEmpty())
            *error = context.pushError;
        else
            fail(error, isCancelled()
                ? QStringLiteral("Operation cancelled.")
                : QStringLiteral("Cannot push tag."));
        return false;
    }
    progress(QStringLiteral("Tag push completed"), 100);
    return true;
}

bool LibGit2Backend::deleteRemoteTag(const QString& remoteName,
                                     const QString& tagName,
                                     QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (isCancelled()) {
        if (error)
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }
    if (remoteName.trimmed().isEmpty() || tagName.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Remote and tag name are required.");
        return false;
    }

    RemoteHandle remote;
    if (git_remote_lookup(remote.out(), _repository,
                          remoteName.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find push remote."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    configureCallbacks(options.callbacks, &context);

    const QByteArray refspecValue = QStringLiteral(":refs/tags/%1").arg(tagName).toUtf8();
    char* refspecPointer = const_cast<char*>(refspecValue.constData());
    git_strarray refspecs {&refspecPointer, 1};
    progress(QStringLiteral("Deleting remote tag %1").arg(tagName), 0);
    if (git_remote_push(remote.get(), &refspecs, &options) < 0) {
        if (error && !context.pushError.isEmpty())
            *error = context.pushError;
        else
            fail(error, isCancelled()
                ? QStringLiteral("Operation cancelled.")
                : QStringLiteral("Cannot delete remote tag."));
        return false;
    }
    progress(QStringLiteral("Remote tag deleted"), 100);
    return true;
}

bool LibGit2Backend::pruneRemote(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Remote name is required.");
        return false;
    }
    return fetchRemote(name, true, error);
}

bool LibGit2Backend::addRemote(const QString& name, const QString& url,
                               QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    RemoteHandle remote;
    if (git_remote_create(remote.out(), _repository, name.toUtf8().constData(),
                          url.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot add remote."));
    return true;
}

bool LibGit2Backend::removeRemote(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (git_remote_delete(_repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot remove remote."));
    return true;
}

QVector<RemoteInfo> LibGit2Backend::remotes(QString* error) const
{
    QVector<RemoteInfo> result;
    if (!ensureRepository(_repository, error))
        return result;
    git_strarray names = {};
    if (git_remote_list(&names, _repository) < 0) {
        fail(error, QStringLiteral("Cannot list remotes."));
        return result;
    }
    for (size_t index = 0; index < names.count; ++index) {
        RemoteHandle remote;
        if (git_remote_lookup(remote.out(), _repository, names.strings[index]) < 0) {
            git_error_clear();
            continue;
        }
        RemoteInfo value;
        value.name = text(names.strings[index]);
        value.fetchUrl = text(git_remote_url(remote.get()));
        value.pushUrl = text(git_remote_pushurl(remote.get()));
        result.append(value);
    }
    git_strarray_dispose(&names);
    return result;
}

bool LibGit2Backend::fetchRemote(const QString& name, bool prune,
                                 QString* error)
{
    if (isCancelled()) {
        if (error)
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }
    RemoteHandle remote;
    if (git_remote_lookup(remote.out(), _repository,
                          name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find remote."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
    options.prune = prune ? GIT_FETCH_PRUNE : GIT_FETCH_PRUNE_UNSPECIFIED;
    options.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_AUTO;
    configureCallbacks(options.callbacks, &context);

    progress(QStringLiteral("Fetching %1").arg(name), 0);
    if (git_remote_fetch(remote.get(), nullptr, &options,
                         "fetch from Git Manager") < 0)
        return fail(error, isCancelled()
            ? QStringLiteral("Operation cancelled.")
            : QStringLiteral("Cannot fetch remote."));
    progress(QStringLiteral("Fetched %1").arg(name), 100);
    return true;
}

bool LibGit2Backend::fetchAll(bool prune, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    git_strarray remotes {};
    if (git_remote_list(&remotes, _repository) < 0)
        return fail(error, QStringLiteral("Cannot list remotes."));

    bool ok = true;
    for (size_t index = 0; index < remotes.count; ++index) {
        if (!fetchRemote(text(remotes.strings[index]), prune, error)) {
            ok = false;
            break;
        }
    }
    git_strarray_dispose(&remotes);
    return ok;
}

bool LibGit2Backend::push(const QString& remoteName, const QString& branchName,
                          bool setUpstream, QString* error, bool forceWithLease)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (isCancelled()) {
        if (error)
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }

    QString localBranch = branchName;
    if (localBranch.isEmpty()) {
        ReferenceHandle head;
        if (git_repository_head(head.out(), _repository) < 0
            || git_repository_head_detached(_repository) == 1)
            return fail(error, QStringLiteral("A local branch is required for push."));
        localBranch = text(git_reference_shorthand(head.get()));
    }

    const QByteArray localRef = QStringLiteral("refs/heads/%1")
        .arg(localBranch).toUtf8();
    QString actualRemote = remoteName;
    QString remoteBranch = localBranch;
    if (actualRemote.isEmpty()) {
        git_buf remoteBuffer = GIT_BUF_INIT;
        if (git_branch_upstream_remote(&remoteBuffer, _repository,
                                       localRef.constData()) == 0) {
            actualRemote = QString::fromUtf8(remoteBuffer.ptr,
                                             static_cast<int>(remoteBuffer.size));
        }
        git_buf_dispose(&remoteBuffer);
        if (actualRemote.isEmpty()) {
            git_error_clear();
            actualRemote = QStringLiteral("origin");
        }

        git_buf mergeBuffer = GIT_BUF_INIT;
        if (git_branch_upstream_merge(&mergeBuffer, _repository,
                                      localRef.constData()) == 0) {
            const QString mergeRef = QString::fromUtf8(
                mergeBuffer.ptr, static_cast<int>(mergeBuffer.size));
            if (mergeRef.startsWith(QStringLiteral("refs/heads/")))
                remoteBranch = mergeRef.mid(11);
        } else {
            git_error_clear();
        }
        git_buf_dispose(&mergeBuffer);
    }

    RemoteHandle remote;
    if (git_remote_lookup(remote.out(), _repository,
                          actualRemote.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find push remote."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    configureCallbacks(options.callbacks, &context);

    if (setUpstream)
        remoteBranch = localBranch;
    const QString trackingRefName = QStringLiteral("refs/remotes/%1/%2")
        .arg(actualRemote, remoteBranch);
    if (forceWithLease) {
        ReferenceHandle tracking;
        if (git_reference_lookup(tracking.out(), _repository,
                                 trackingRefName.toUtf8().constData()) < 0
            || !git_reference_target(tracking.get())) {
            return fail(error, QStringLiteral(
                "Force-with-lease requires a fetched remote-tracking branch."));
        }
        context.forceWithLease = true;
        context.leaseDestination = QStringLiteral("refs/heads/%1")
            .arg(remoteBranch).toUtf8();
        git_oid_cpy(&context.leaseExpected, git_reference_target(tracking.get()));
    }
    const QByteArray refspecValue = QStringLiteral("%1refs/heads/%2:refs/heads/%3")
        .arg(forceWithLease ? QStringLiteral("+") : QString(),
             localBranch, remoteBranch).toUtf8();
    char* refspecPointer = const_cast<char*>(refspecValue.constData());
    git_strarray refspecs {&refspecPointer, 1};
    progress(QStringLiteral("Pushing %1").arg(localBranch), 0);
    if (git_remote_push(remote.get(), &refspecs, &options) < 0) {
        if (error && !context.pushError.isEmpty())
            *error = context.pushError;
        else
            fail(error, isCancelled()
                ? QStringLiteral("Operation cancelled.")
                : QStringLiteral("Cannot push branch."));
        return false;
    }

    ReferenceHandle local;
    if (git_branch_lookup(local.out(), _repository, localBranch.toUtf8().constData(),
                          GIT_BRANCH_LOCAL) < 0) {
        return fail(error, QStringLiteral(
            "Push succeeded, but the local branch could not be refreshed."));
    }
    const git_oid* localOid = git_reference_target(local.get());
    ReferenceHandle tracking;
    if (!localOid
        || git_reference_create(tracking.out(), _repository,
                                trackingRefName.toUtf8().constData(), localOid, 1,
                                "update by push") < 0) {
        return fail(error, QStringLiteral(
            "Push succeeded, but the remote-tracking branch could not be refreshed."));
    }

    if (setUpstream) {
        const QString remoteTracking = actualRemote + QLatin1Char('/') + remoteBranch;
        if (git_branch_set_upstream(local.get(),
                                       remoteTracking.toUtf8().constData()) < 0)
            return fail(error, QStringLiteral("Push succeeded, but upstream could not be set."));
    }
    progress(QStringLiteral("Push completed"), 100);
    return true;
}

bool LibGit2Backend::pullRebase(QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (git_repository_state(_repository) != GIT_REPOSITORY_STATE_NONE) {
        if (error)
            *error = QStringLiteral("Another Git operation is already in progress.");
        return false;
    }

    ReferenceHandle head;
    if (git_repository_head(head.out(), _repository) < 0
        || git_repository_head_detached(_repository) == 1)
        return fail(error, QStringLiteral("Pull requires a local branch."));

    ReferenceHandle upstreamBeforeFetch;
    if (git_branch_upstream(upstreamBeforeFetch.out(), head.get()) < 0)
        return fail(error, QStringLiteral("The current branch has no upstream."));
    const QString upstreamFullName = text(git_reference_name(upstreamBeforeFetch.get()));

    git_buf remoteName = GIT_BUF_INIT;
    const int remoteRc = git_branch_remote_name(&remoteName, _repository,
                                                 upstreamFullName.toUtf8().constData());
    const QString remote = remoteRc == 0
        ? QString::fromUtf8(remoteName.ptr, static_cast<int>(remoteName.size))
        : QString();
    git_buf_dispose(&remoteName);
    if (remoteRc < 0 || remote.isEmpty())
        return fail(error, QStringLiteral("Cannot determine upstream remote."));
    if (!fetchRemote(remote, false, error))
        return false;

    ReferenceHandle current;
    ReferenceHandle upstream;
    if (git_repository_head(current.out(), _repository) < 0
        || git_reference_lookup(upstream.out(), _repository,
                                upstreamFullName.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot refresh upstream reference."));
    const git_oid* localOid = git_reference_target(current.get());
    const git_oid* upstreamOid = git_reference_target(upstream.get());
    if (!localOid || !upstreamOid)
        return fail(error, QStringLiteral("Cannot resolve pull revisions."));

    if (git_oid_cmp(localOid, upstreamOid) == 0) {
        progress(QStringLiteral("Already up to date"), 100);
        return true;
    }
    const int localContainsUpstream = git_graph_descendant_of(
        _repository, localOid, upstreamOid);
    if (localContainsUpstream == 1) {
        progress(QStringLiteral("Local branch is already ahead"), 100);
        return true;
    }
    const int upstreamContainsLocal = git_graph_descendant_of(
        _repository, upstreamOid, localOid);
    if (upstreamContainsLocal < 0 || localContainsUpstream < 0)
        return fail(error, QStringLiteral("Cannot analyze branch history."));

    if (upstreamContainsLocal == 1) {
        ObjectHandle upstreamObject;
        if (git_object_lookup(upstreamObject.out(), _repository, upstreamOid,
                              GIT_OBJECT_COMMIT) < 0)
            return fail(error, QStringLiteral("Cannot resolve fast-forward target."));
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        checkout.checkout_strategy = GIT_CHECKOUT_SAFE
                                   | GIT_CHECKOUT_RECREATE_MISSING;
        if (git_checkout_tree(_repository, upstreamObject.get(), &checkout) < 0)
            return fail(error, QStringLiteral("Local changes prevent fast-forward."));
        ReferenceHandle updated;
        if (git_reference_set_target(updated.out(), current.get(), upstreamOid,
                                     "pull: fast-forward") < 0)
            return fail(error, QStringLiteral("Cannot update branch after fast-forward."));
        progress(QStringLiteral("Fast-forward completed"), 100);
        return true;
    }

    AnnotatedHandle upstreamCommit;
    if (git_annotated_commit_from_ref(upstreamCommit.out(), _repository,
                                      upstream.get()) < 0)
        return fail(error, QStringLiteral("Cannot prepare rebase upstream."));
    git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
    options.checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE
                                               | GIT_CHECKOUT_RECREATE_MISSING;
    RebaseHandle rebase;
    if (git_rebase_init(rebase.out(), _repository, nullptr,
                        upstreamCommit.get(), nullptr, &options) < 0)
        return fail(error, QStringLiteral("Cannot start rebase."));
    return finishRebase(rebase.get(), error);
}

QVector<WorktreeInfo> LibGit2Backend::worktrees(QString* error) const
{
    QVector<WorktreeInfo> result;
    if (!ensureRepository(_repository, error))
        return result;

    WorktreeInfo current;
    current.name = QFileInfo(rootPath()).fileName();
    current.path = rootPath();
    current.detached = git_repository_head_detached(_repository) == 1;
    current.current = true;
    current.valid = true;

    ReferenceHandle head;
    if (git_repository_head(head.out(), _repository) == 0) {
        current.headBranch = current.detached
            ? QStringLiteral("HEAD")
            : text(git_reference_shorthand(head.get()));
        current.headHash = oidText(git_reference_target(head.get()));
    } else {
        git_error_clear();
    }
    result.append(current);

    git_strarray names {};
    if (git_worktree_list(&names, _repository) < 0) {
        fail(error, QStringLiteral("Cannot list worktrees."));
        return {};
    }

    for (size_t index = 0; index < names.count; ++index) {
        const QString name = text(names.strings[index]);
        WorktreeHandle worktree;
        if (git_worktree_lookup(worktree.out(), _repository,
                                name.toUtf8().constData()) < 0) {
            git_error_clear();
            continue;
        }
        WorktreeInfo info;
        info.name = name;
        info.path = resolveWorktreePath(text(git_worktree_path(worktree.get())));
        info.valid = git_worktree_validate(worktree.get()) == 0;
        git_error_clear();

        git_buf lockReason = GIT_BUF_INIT;
        const int locked = git_worktree_is_locked(&lockReason, worktree.get());
        info.locked = locked == 1;
        if (info.locked) {
            info.lockReason = QString::fromUtf8(lockReason.ptr ? lockReason.ptr : "",
                                                static_cast<int>(lockReason.size));
        }
        git_buf_dispose(&lockReason);
        git_error_clear();

        git_repository* rawWorktreeRepo = nullptr;
        if (git_repository_open_from_worktree(&rawWorktreeRepo, worktree.get()) == 0) {
            GitHandle<git_repository, git_repository_free> worktreeRepo(rawWorktreeRepo);
            info.detached = git_repository_head_detached(worktreeRepo.get()) == 1;
            ReferenceHandle worktreeHead;
            if (git_repository_head(worktreeHead.out(), worktreeRepo.get()) == 0) {
                info.headBranch = info.detached
                    ? QStringLiteral("HEAD")
                    : text(git_reference_shorthand(worktreeHead.get()));
                info.headHash = oidText(git_reference_target(worktreeHead.get()));
            } else {
                git_error_clear();
            }
        } else {
            git_error_clear();
        }
        result.append(info);
    }
    git_strarray_dispose(&names);
    return result;
}

bool LibGit2Backend::addWorktree(const QString& name, const QString& path,
                                 const QString& branchName, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty() || path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Worktree name and path are required.");
        return false;
    }

    git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
    if (!branchName.trimmed().isEmpty()) {
        ReferenceHandle branch;
        if (git_branch_lookup(branch.out(), _repository, branchName.toUtf8().constData(),
                              GIT_BRANCH_LOCAL) < 0)
            return fail(error, QStringLiteral("Cannot find worktree branch."));
        options.ref = branch.get();
        options.checkout_existing = 1;
        WorktreeHandle worktree;
        if (git_worktree_add(worktree.out(), _repository, name.toUtf8().constData(),
                             path.toUtf8().constData(), &options) < 0)
            return fail(error, QStringLiteral("Cannot create worktree."));
        return true;
    }

    WorktreeHandle worktree;
    if (git_worktree_add(worktree.out(), _repository, name.toUtf8().constData(),
                         path.toUtf8().constData(), nullptr) < 0)
        return fail(error, QStringLiteral("Cannot create worktree."));
    return true;
}

bool LibGit2Backend::moveWorktree(const QString& name, const QString& path,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty() || path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Worktree name and target path are required.");
        return false;
    }

    WorktreeHandle worktree;
    if (git_worktree_lookup(worktree.out(), _repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find worktree."));

    git_buf lockReason = GIT_BUF_INIT;
    const int locked = git_worktree_is_locked(&lockReason, worktree.get());
    git_buf_dispose(&lockReason);
    if (locked == 1) {
        if (error)
            *error = QStringLiteral("Unlock the worktree before moving it.");
        return false;
    }
    if (locked < 0) {
        git_error_clear();
        return fail(error, QStringLiteral("Cannot inspect worktree lock state."));
    }

    const QString sourcePath = resolveWorktreePath(text(git_worktree_path(worktree.get())));
    const QString targetPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (sourcePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Cannot resolve worktree path.");
        return false;
    }
    if (sourcePath.compare(targetPath, Qt::CaseInsensitive) == 0)
        return true;
    if (!QFileInfo::exists(sourcePath)) {
        if (error)
            *error = QStringLiteral("Source worktree directory does not exist.");
        return false;
    }

    const QString commonDir = QDir::cleanPath(text(git_repository_commondir(_repository)));
    const QString gitdirFile = QDir(commonDir).filePath(
        QStringLiteral("worktrees/%1/gitdir").arg(name));
    if (!QFileInfo::exists(gitdirFile)) {
        if (error)
            *error = QStringLiteral("Cannot find worktree metadata.");
        return false;
    }

    if (!moveDirectoryRecursively(sourcePath, targetPath, error))
        return false;

    const QString movedGitFile = QDir(targetPath).filePath(QStringLiteral(".git"));
    if (!writeTextFile(gitdirFile,
                       QDir::fromNativeSeparators(movedGitFile) + QLatin1Char('\n'),
                       error)) {
        return false;
    }

    const QString repositoryRoot = rootPath();
    close();
    if (!open(repositoryRoot, error))
        return false;
    return true;
}

bool LibGit2Backend::lockWorktree(const QString& name, const QString& reason,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Worktree name is required.");
        return false;
    }

    WorktreeHandle worktree;
    if (git_worktree_lookup(worktree.out(), _repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find worktree."));
    if (git_worktree_lock(worktree.get(), reason.trimmed().toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot lock worktree."));
    return true;
}

bool LibGit2Backend::unlockWorktree(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Worktree name is required.");
        return false;
    }

    WorktreeHandle worktree;
    if (git_worktree_lookup(worktree.out(), _repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find worktree."));
    const int rc = git_worktree_unlock(worktree.get());
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot unlock worktree."));
    return true;
}

bool LibGit2Backend::removeWorktree(const QString& name, bool force,
                                    QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Worktree name is required.");
        return false;
    }

    WorktreeHandle worktree;
    if (git_worktree_lookup(worktree.out(), _repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot find worktree."));

    git_worktree_prune_options options = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
    options.flags = force
        ? (GIT_WORKTREE_PRUNE_VALID
           | GIT_WORKTREE_PRUNE_LOCKED
           | GIT_WORKTREE_PRUNE_WORKING_TREE)
        : 0;
    const int prunable = git_worktree_is_prunable(worktree.get(), &options);
    if (prunable == 1) {
        if (git_worktree_prune(worktree.get(), &options) < 0)
            return fail(error, QStringLiteral("Cannot prune worktree metadata."));
        return true;
    }
    git_error_clear();

    const QString worktreePath = resolveWorktreePath(text(git_worktree_path(worktree.get())));
    if (!worktreePath.isEmpty()) {
        QDir dir(worktreePath);
        if (dir.exists() && !dir.removeRecursively()) {
            if (error)
                *error = QStringLiteral("Cannot remove worktree directory.");
            return false;
        }
    }
    if (git_worktree_prune(worktree.get(), &options) < 0)
        return fail(error, QStringLiteral("Cannot remove worktree."));
    return true;
}

QVector<SubmoduleInfo> LibGit2Backend::submodules(QString* error) const
{
    QVector<SubmoduleInfo> result;
    if (!ensureRepository(_repository, error))
        return result;

    // Discovery refreshes libgit2's cached index/config state. Inspect through
    // a separate handle so the backend's status and history views stay stable.
    RepositoryHandle inspectionRepository;
    const QByteArray repositoryPath = rootPath().toUtf8();
    if (git_repository_open(inspectionRepository.out(),
                            repositoryPath.constData()) < 0) {
        fail(error, QStringLiteral("Cannot open repository for submodule inspection."));
        return result;
    }

    struct Context {
        git_repository* repository {nullptr};
        QVector<SubmoduleInfo>* values {nullptr};
    } context {inspectionRepository.get(), &result};

    const int rc = git_submodule_foreach(
        inspectionRepository.get(),
        [](git_submodule* submodule, const char*, void* payload) -> int {
            auto* context = static_cast<Context*>(payload);
            SubmoduleInfo value;
            value.name = text(git_submodule_name(submodule));
            value.path = text(git_submodule_path(submodule));
            value.url = text(git_submodule_url(submodule));
            value.branch = text(git_submodule_branch(submodule));
            value.indexHash = oidText(git_submodule_index_id(submodule));
            value.workdirHash = oidText(git_submodule_wd_id(submodule));

            unsigned int status = 0;
            const QByteArray name = value.name.toUtf8();
            if (git_submodule_status(&status, context->repository,
                                     name.constData(),
                                     GIT_SUBMODULE_IGNORE_NONE) == 0) {
                if ((status & GIT_SUBMODULE_STATUS_INDEX_DELETED)
                    && !(status & (GIT_SUBMODULE_STATUS_IN_INDEX
                                   | GIT_SUBMODULE_STATUS_IN_CONFIG
                                   | GIT_SUBMODULE_STATUS_IN_WD))) {
                    return 0;
                }
                value.initialized = (status & GIT_SUBMODULE_STATUS_IN_WD)
                    && !(status & GIT_SUBMODULE_STATUS_WD_UNINITIALIZED);
                value.dirty = !GIT_SUBMODULE_STATUS_IS_UNMODIFIED(status);
            } else {
                git_error_clear();
            }
            context->values->append(value);
            return 0;
        },
        &context);
    if (rc < 0) {
        fail(error, QStringLiteral("Cannot list submodules."));
        result.clear();
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.path.compare(right.path, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool LibGit2Backend::addSubmodule(const QString& url, const QString& path,
                                  QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    const QString normalizedPath = QDir::cleanPath(
        QDir::fromNativeSeparators(path.trimmed()));
    if (url.trimmed().isEmpty() || normalizedPath.isEmpty()
        || QDir::isAbsolutePath(normalizedPath)
        || normalizedPath == QStringLiteral("..")
        || normalizedPath.startsWith(QStringLiteral("../"))) {
        if (error)
            *error = QStringLiteral("A URL and a repository-relative submodule path are required.");
        return false;
    }

    SubmoduleHandle submodule;
    const QByteArray urlBytes = url.trimmed().toUtf8();
    const QByteArray pathBytes = normalizedPath.toUtf8();
    if (git_submodule_add_setup(submodule.out(), _repository,
                                urlBytes.constData(), pathBytes.constData(), 1) < 0)
        return fail(error, QStringLiteral("Cannot prepare submodule."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_submodule_update_options options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE
        | GIT_CHECKOUT_RECREATE_MISSING;
    configureCallbacks(options.fetch_opts.callbacks, &context);
    RepositoryHandle cloned;
    progress(QStringLiteral("Cloning submodule %1").arg(normalizedPath));
    if (git_submodule_clone(cloned.out(), submodule.get(), &options) < 0)
        return fail(error, isCancelled()
            ? QStringLiteral("Operation cancelled.")
            : QStringLiteral("Cannot clone submodule."));
    if (git_submodule_add_finalize(submodule.get()) < 0)
        return fail(error, QStringLiteral("Cannot finalize submodule."));
    progress(QStringLiteral("Submodule added"), 100);
    return true;
}

bool LibGit2Backend::updateSubmodule(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Submodule name is required.");
        return false;
    }

    SubmoduleHandle submodule;
    const QByteArray nameBytes = name.trimmed().toUtf8();
    if (git_submodule_lookup(submodule.out(), _repository,
                             nameBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot find submodule."));

    NetworkContext context = networkContext(_credentials, _progressCallback,
                                            _cancelFlag);
    git_submodule_update_options options = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    options.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE
        | GIT_CHECKOUT_RECREATE_MISSING;
    configureCallbacks(options.fetch_opts.callbacks, &context);
    progress(QStringLiteral("Updating submodule %1").arg(name));
    if (git_submodule_update(submodule.get(), 1, &options) < 0)
        return fail(error, isCancelled()
            ? QStringLiteral("Operation cancelled.")
            : QStringLiteral("Cannot update submodule."));
    progress(QStringLiteral("Submodule updated"), 100);
    return true;
}

bool LibGit2Backend::syncSubmodule(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Submodule name is required.");
        return false;
    }
    SubmoduleHandle submodule;
    const QByteArray nameBytes = name.trimmed().toUtf8();
    if (git_submodule_lookup(submodule.out(), _repository,
                             nameBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot find submodule."));
    if (git_submodule_sync(submodule.get()) < 0)
        return fail(error, QStringLiteral("Cannot synchronize submodule settings."));
    return true;
}

bool LibGit2Backend::setSubmoduleBranch(const QString& name,
                                        const QString& branch,
                                        QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Submodule name is required.");
        return false;
    }
    const QByteArray nameBytes = name.trimmed().toUtf8();
    const QByteArray branchBytes = branch.trimmed().toUtf8();
    if (git_submodule_set_branch(_repository, nameBytes.constData(),
                                 branchBytes.isEmpty()
                                     ? nullptr : branchBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot set submodule branch."));

    SubmoduleHandle submodule;
    if (git_submodule_lookup(submodule.out(), _repository,
                             nameBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot reload submodule."));
    if (git_submodule_sync(submodule.get()) < 0)
        return fail(error, QStringLiteral("Cannot synchronize submodule settings."));

    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0
        || git_index_add_bypath(index.get(), ".gitmodules") < 0
        || git_index_write(index.get()) < 0)
        return fail(error, QStringLiteral("Cannot stage submodule branch configuration."));
    return true;
}

bool LibGit2Backend::removeSubmodule(const QString& name, bool force,
                                     QString* error)
{
    if (!ensureRepository(_repository, error)
        || !ensureNoActiveOperation(_repository, error))
        return false;
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Submodule name is required.");
        return false;
    }

    const QByteArray nameBytes = name.trimmed().toUtf8();
    SubmoduleHandle submodule;
    if (git_submodule_lookup(submodule.out(), _repository,
                             nameBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot find submodule."));
    const QString path = QDir::cleanPath(text(git_submodule_path(submodule.get())));
    if (path.isEmpty() || QDir::isAbsolutePath(path)
        || path == QStringLiteral("..")
        || path.startsWith(QStringLiteral("../"))) {
        if (error)
            *error = QStringLiteral("Submodule path is unsafe.");
        return false;
    }
    const QString repositoryRoot = QDir::cleanPath(rootPath());
    const QString workdirPath = QDir::cleanPath(
        QFileInfo(QDir(repositoryRoot).filePath(path)).absoluteFilePath());
    const QString repositoryPrefix = repositoryRoot + QLatin1Char('/');
    if (!workdirPath.startsWith(repositoryPrefix, Qt::CaseInsensitive)) {
        if (error)
            *error = QStringLiteral("Submodule path escapes the repository.");
        return false;
    }

    unsigned int status = 0;
    if (git_submodule_status(&status, _repository, nameBytes.constData(),
                             GIT_SUBMODULE_IGNORE_NONE) < 0)
        return fail(error, QStringLiteral("Cannot inspect submodule status."));
    if (!force && !GIT_SUBMODULE_STATUS_IS_UNMODIFIED(status)) {
        if (error)
            *error = QStringLiteral("Submodule has staged or working directory changes.");
        return false;
    }

    const QString modulesFile = QDir(repositoryRoot).filePath(QStringLiteral(".gitmodules"));
    if (QFileInfo::exists(modulesFile)) {
        ConfigHandle modulesConfig;
        if (git_config_open_ondisk(modulesConfig.out(),
                                   modulesFile.toUtf8().constData()) < 0
            || !deleteConfigSection(modulesConfig.get(), name.trimmed(), error))
            return fail(error, QStringLiteral("Cannot update .gitmodules."));
    }

    ConfigHandle repositoryConfig;
    if (git_repository_config(repositoryConfig.out(), _repository) < 0
        || !deleteConfigSection(repositoryConfig.get(), name.trimmed(), error))
        return fail(error, QStringLiteral("Cannot update repository configuration."));

    IndexHandle index;
    const QByteArray pathBytes = path.toUtf8();
    if (git_repository_index(index.out(), _repository) < 0
        || git_index_remove_bypath(index.get(), pathBytes.constData()) < 0)
        return fail(error, QStringLiteral("Cannot remove submodule from index."));
    if (QFileInfo::exists(modulesFile)
        && git_index_add_bypath(index.get(), ".gitmodules") < 0)
        return fail(error, QStringLiteral("Cannot stage .gitmodules."));
    if (git_index_write(index.get()) < 0)
        return fail(error, QStringLiteral("Cannot write repository index."));

    if (QDir(workdirPath).exists() && !QDir(workdirPath).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Cannot remove submodule working directory.");
        return false;
    }

    const QString commonDir = QDir::cleanPath(text(git_repository_commondir(_repository)));
    const QString modulesRoot = QDir(commonDir).filePath(QStringLiteral("modules"));
    const QString metadataPath = QDir(modulesRoot).filePath(name.trimmed());
    const QString rootPrefix = QDir::cleanPath(modulesRoot) + QLatin1Char('/');
    const QString cleanMetadata = QDir::cleanPath(metadataPath);
    if (cleanMetadata.startsWith(rootPrefix, Qt::CaseInsensitive)
        && QDir(cleanMetadata).exists()
        && !QDir(cleanMetadata).removeRecursively()) {
        if (error)
            *error = QStringLiteral("Cannot remove submodule metadata.");
        return false;
    }
    return true;
}

QStringList LibGit2Backend::stashes(QString* error) const
{
    QStringList result;
    if (!ensureRepository(_repository, error))
        return result;
    struct Payload {
        QStringList* values;
    } payload {&result};
    const int rc = git_stash_foreach(
        _repository,
        [](size_t index, const char* message, const git_oid*, void* raw) -> int {
            auto* data = static_cast<Payload*>(raw);
            data->values->append(QStringLiteral("stash@{%1}\t%2")
                                     .arg(index).arg(text(message)));
            return 0;
        },
        &payload);
    if (rc < 0)
        fail(error, QStringLiteral("Cannot list stashes."));
    return result;
}

bool LibGit2Backend::stashPush(const QString& message, bool includeUntracked,
                               QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0)
        return fail(error, QStringLiteral("Git user.name and user.email are required."));
    const QByteArray messageBytes = message.toUtf8();
    git_stash_save_options options = GIT_STASH_SAVE_OPTIONS_INIT;
    options.flags = includeUntracked ? GIT_STASH_INCLUDE_UNTRACKED
                                     : GIT_STASH_DEFAULT;
    options.stasher = signature.get();
    options.message = message.isEmpty() ? nullptr : messageBytes.constData();
    git_oid oid;
    if (git_stash_save_with_opts(&oid, _repository, &options) < 0)
        return fail(error, QStringLiteral("Cannot create stash."));
    return true;
}

bool LibGit2Backend::stashApply(size_t index, bool pop, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    git_stash_apply_options options = GIT_STASH_APPLY_OPTIONS_INIT;
    options.checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE
                                               | GIT_CHECKOUT_RECREATE_MISSING;
    const int rc = pop
        ? git_stash_pop(_repository, index, &options)
        : git_stash_apply(_repository, index, &options);
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot apply stash."));
    return true;
}

bool LibGit2Backend::stashDrop(size_t index, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    if (git_stash_drop(_repository, index) < 0)
        return fail(error, QStringLiteral("Cannot drop stash."));
    return true;
}

bool LibGit2Backend::resolveConflict(const QString& path, bool ours,
                                     QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return fail(error, QStringLiteral("Cannot open repository index."));

    const QByteArray pathBytes = path.toUtf8();
    const git_index_entry* ancestor = nullptr;
    const git_index_entry* ourEntry = nullptr;
    const git_index_entry* theirEntry = nullptr;
    if (git_index_conflict_get(&ancestor, &ourEntry, &theirEntry,
                               index.get(), pathBytes.constData()) < 0)
        return fail(error, QStringLiteral("File is not conflicted."));
    const git_index_entry* selected = ours ? ourEntry : theirEntry;
    git_index_entry resolved {};
    const bool hasSelected = selected != nullptr;
    if (hasSelected) {
        resolved = *selected;
        GIT_INDEX_ENTRY_STAGE_SET(&resolved, 0);
        resolved.path = pathBytes.constData();
    }

    int rc = git_index_conflict_remove(index.get(), pathBytes.constData());
    if (rc < 0)
        return fail(error, QStringLiteral("Cannot remove conflict entries."));

    if (hasSelected) {
        if (git_index_add(index.get(), &resolved) < 0)
            return fail(error, QStringLiteral("Cannot stage conflict resolution."));
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        char* pathValue = const_cast<char*>(pathBytes.constData());
        checkout.checkout_strategy = GIT_CHECKOUT_FORCE
                                   | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
        checkout.paths.count = 1;
        checkout.paths.strings = &pathValue;
        if (git_checkout_index(_repository, index.get(), &checkout) < 0)
            return fail(error, QStringLiteral("Cannot write selected conflict version."));
    } else {
        QFile::remove(QDir(rootPath()).filePath(path));
    }
    if (git_index_write(index.get()) < 0)
        return fail(error, QStringLiteral("Cannot save conflict resolution."));
    return true;
}

HistoryOperationResult LibGit2Backend::continueHistoryOperation(
    const QString& operation, QString* error)
{
    if (!ensureRepository(_repository, error))
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    const QString name = operation.trimmed().toLower();
    const int state = git_repository_state(_repository);
    if (name == QStringLiteral("rebase")) {
        if (state != GIT_REPOSITORY_STATE_REBASE
            && state != GIT_REPOSITORY_STATE_REBASE_INTERACTIVE
            && state != GIT_REPOSITORY_STATE_REBASE_MERGE) {
            if (error)
                *error = QStringLiteral("No matching rebase is in progress.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        RebaseHandle rebase;
        git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
        options.checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE
                                                   | GIT_CHECKOUT_RECREATE_MISSING;
        if (git_rebase_open(rebase.out(), _repository, &options) < 0)
            return historyFailure(error, QStringLiteral("No supported rebase is in progress."));

        const QString sidecar = interactiveRebasePath(_repository);
        if (!sidecar.isEmpty() && QFileInfo::exists(sidecar)) {
            InteractiveRebaseState interactiveState;
            if (!loadInteractiveRebase(_repository, &interactiveState,
                                       error)) {
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            }
            return finishInteractiveRebase(rebase.get(),
                                           std::move(interactiveState), error);
        }
        return finishRebaseResult(rebase.get(), error);
    }
    const bool matchesMerge = name == QStringLiteral("merge")
        && state == GIT_REPOSITORY_STATE_MERGE;
    if (state == GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE
        || state == GIT_REPOSITORY_STATE_REVERT_SEQUENCE) {
        if (error) {
            *error = QStringLiteral(
                "Multi-commit cherry-pick/revert sequences are not supported; use the tool that started the sequence.");
        }
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    const bool matchesCherryPick = (name == QStringLiteral("cherry-pick")
                                    || name == QStringLiteral("cherrypick"))
        && state == GIT_REPOSITORY_STATE_CHERRYPICK;
    const bool matchesRevert = name == QStringLiteral("revert")
        && state == GIT_REPOSITORY_STATE_REVERT;
    if (matchesMerge || matchesCherryPick || matchesRevert) {
        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return historyFailure(error, QStringLiteral("Cannot open operation index."));
        if (git_index_has_conflicts(index.get())) {
            return historyResult(HistoryOperationStatus::Conflicts,
                                 QStringLiteral("Resolve all conflicts before continuing."));
        }
        if (!createStateCommit(name, error)) {
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        return historyResult(HistoryOperationStatus::Completed,
                             QStringLiteral("Operation completed."));
    }

    if (error)
        *error = QStringLiteral("No matching operation is in progress.");
    return historyResult(HistoryOperationStatus::Completed,
                         error ? *error : QString());
}

bool LibGit2Backend::continueOperation(const QString& operation,
                                       QString* error)
{
    const HistoryOperationResult result = continueHistoryOperation(operation,
                                                                   error);
    if (error && !error->isEmpty())
        return false;
    if (result.status == HistoryOperationStatus::Conflicts
        || result.status == HistoryOperationStatus::PausedForEdit) {
        if (error)
            *error = result.message;
        return false;
    }
    return true;
}

bool LibGit2Backend::abortOperation(const QString& operation, QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    const QString name = operation.trimmed().toLower();
    if (name == QStringLiteral("rebase")) {
        const int state = git_repository_state(_repository);
        if (state != GIT_REPOSITORY_STATE_REBASE
            && state != GIT_REPOSITORY_STATE_REBASE_INTERACTIVE
            && state != GIT_REPOSITORY_STATE_REBASE_MERGE) {
            if (error)
                *error = QStringLiteral("No matching rebase is in progress.");
            return false;
        }
        RebaseHandle rebase;
        git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
        options.checkout_options.checkout_strategy = GIT_CHECKOUT_FORCE;
        if (git_rebase_open(rebase.out(), _repository, &options) < 0)
            return fail(error, QStringLiteral("No rebase is in progress."));
        if (git_rebase_abort(rebase.get()) < 0)
            return fail(error, QStringLiteral("Cannot abort rebase."));
        return removeInteractiveRebase(_repository, error);
    }

    const int state = git_repository_state(_repository);
    if (state == GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE
        || state == GIT_REPOSITORY_STATE_REVERT_SEQUENCE) {
        if (error) {
            *error = QStringLiteral(
                "Multi-commit cherry-pick/revert sequences are not supported; use the tool that started the sequence.");
        }
        return false;
    }
    const bool matchesMerge = name == QStringLiteral("merge")
        && state == GIT_REPOSITORY_STATE_MERGE;
    const bool matchesCherryPick = (name == QStringLiteral("cherry-pick")
                                    || name == QStringLiteral("cherrypick"))
        && state == GIT_REPOSITORY_STATE_CHERRYPICK;
    const bool matchesRevert = name == QStringLiteral("revert")
        && state == GIT_REPOSITORY_STATE_REVERT;
    if (!matchesMerge && !matchesCherryPick && !matchesRevert) {
        if (error)
            *error = QStringLiteral("No matching operation is in progress.");
        return false;
    }
    ObjectHandle head;
    if (git_revparse_single(head.out(), _repository, "HEAD^{commit}") < 0)
        return fail(error, QStringLiteral("Cannot resolve HEAD for abort."));
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_FORCE
                               | GIT_CHECKOUT_RECREATE_MISSING;
    if (git_reset(_repository, head.get(), GIT_RESET_HARD, &checkout) < 0
        || git_repository_state_cleanup(_repository) < 0)
        return fail(error, QStringLiteral("Cannot abort operation."));
    return true;
}

bool LibGit2Backend::finishRebase(git_rebase* rebase, QString* error)
{
    const HistoryOperationResult result = finishRebaseResult(rebase, error);
    if (error && !error->isEmpty())
        return false;
    if (result.status == HistoryOperationStatus::Conflicts
        || result.status == HistoryOperationStatus::PausedForEdit) {
        if (error)
            *error = result.message;
        return false;
    }
    return true;
}

HistoryOperationResult LibGit2Backend::finishRebaseResult(git_rebase* rebase,
                                                          QString* error)
{
    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0)
        return historyFailure(error,
                              QStringLiteral("Git user.name and user.email are required."));

    const size_t total = git_rebase_operation_entrycount(rebase);
    size_t current = git_rebase_operation_current(rebase);
    if (current != GIT_REBASE_NO_OPERATION) {
        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return historyFailure(error, QStringLiteral("Cannot open rebase index."));
        if (git_index_has_conflicts(index.get())) {
            return historyResult(
                HistoryOperationStatus::Conflicts,
                QStringLiteral("Resolve all conflicts before continuing rebase."));
        }
        git_oid commitOid;
        const int commitRc = git_rebase_commit(&commitOid, rebase, nullptr,
                                                signature.get(), nullptr, nullptr);
        if (commitRc < 0 && commitRc != GIT_EAPPLIED)
            return historyFailure(error,
                                  QStringLiteral("Cannot commit resolved rebase step."));
        git_error_clear();
    }

    while (true) {
        if (isCancelled()) {
            if (error)
                *error = QStringLiteral("Operation cancelled; rebase remains in progress.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        git_rebase_operation* rebaseOperation = nullptr;
        const int nextRc = git_rebase_next(&rebaseOperation, rebase);
        if (nextRc == GIT_ITEROVER)
            break;
        if (nextRc == GIT_EMERGECONFLICT) {
            return historyResult(HistoryOperationStatus::Conflicts,
                                 QStringLiteral("Rebase stopped because of conflicts."));
        }
        if (nextRc < 0)
            return historyFailure(error,
                                  QStringLiteral("Cannot apply next rebase step."));

        current = git_rebase_operation_current(rebase);
        progress(QStringLiteral("Rebasing %1/%2")
                     .arg(current + 1).arg(total),
                 total > 0 ? static_cast<int>((current + 1) * 100 / total) : -1);

        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return historyFailure(error, QStringLiteral("Cannot open rebase index."));
        if (git_index_has_conflicts(index.get())) {
            return historyResult(HistoryOperationStatus::Conflicts,
                                 QStringLiteral("Rebase stopped because of conflicts."));
        }

        git_oid commitOid;
        const int commitRc = git_rebase_commit(&commitOid, rebase, nullptr,
                                                signature.get(), nullptr, nullptr);
        if (commitRc < 0 && commitRc != GIT_EAPPLIED)
            return historyFailure(error, QStringLiteral("Cannot commit rebase step."));
        git_error_clear();
    }
    if (git_rebase_finish(rebase, signature.get()) < 0)
        return historyFailure(error, QStringLiteral("Cannot finish rebase."));
    progress(QStringLiteral("Rebase completed"), 100);
    return historyResult(total == 0 ? HistoryOperationStatus::UpToDate
                                    : HistoryOperationStatus::Completed,
                         total == 0 ? QStringLiteral("No commits needed rebasing.")
                                    : QStringLiteral("Rebase completed."));
}

HistoryOperationResult LibGit2Backend::finishInteractiveRebase(
    git_rebase* rebase, InteractiveRebaseState state, QString* error)
{
    RebasePlan& plan = state.plan;
    bool& pausedForEdit = state.pausedForEdit;
    int& completedIndex = state.completedIndex;
    const QString originalHead = oidText(git_rebase_orig_head_id(rebase));
    const QString onto = oidText(git_rebase_onto_id(rebase));
    if (originalHead.compare(plan.preview.expectedHead,
                             Qt::CaseInsensitive) != 0
        || onto.compare(plan.preview.targetHash, Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QStringLiteral(
                "Interactive rebase state belongs to a different rebase session.");
        }
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }

    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0) {
        return historyFailure(
            error, QStringLiteral("Git user.name and user.email are required."));
    }

    const size_t total = git_rebase_operation_entrycount(rebase);
    if (total != static_cast<size_t>(plan.items.size())) {
        if (error)
            *error = QStringLiteral("Interactive rebase state does not match the repository.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    for (size_t index = 0; index < total; ++index) {
        const git_rebase_operation* operation =
            git_rebase_operation_byindex(rebase, index);
        if (!operation
            || oidText(&operation->id).compare(plan.items.at(static_cast<int>(index)).hash,
                                               Qt::CaseInsensitive) != 0) {
            if (error) {
                *error = QStringLiteral(
                    "Interactive rebase commit order does not match the saved plan.");
            }
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
    }

    auto restoreHeadTree = [this, error]() -> bool {
        CommitHandle head;
        TreeHandle tree;
        IndexHandle index;
        if (!lookupHeadCommit(_repository, head)
            || git_commit_tree(tree.out(), head.get()) < 0
            || git_repository_index(index.out(), _repository) < 0) {
            return fail(error, QStringLiteral("Cannot restore HEAD while dropping a commit."));
        }
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        checkout.checkout_strategy = GIT_CHECKOUT_FORCE
                                   | GIT_CHECKOUT_RECREATE_MISSING;
        if (git_checkout_tree(_repository,
                              reinterpret_cast<git_object*>(head.get()),
                              &checkout) < 0
            || git_index_read_tree(index.get(), tree.get()) < 0
            || git_index_write(index.get()) < 0) {
            return fail(error, QStringLiteral("Cannot drop rebase commit."));
        }
        return true;
    };

    auto updateRewrittenMapping = [this, error](const QString& source,
                                                const QString& oldTarget,
                                                const QString& newTarget)
        -> bool {
        const char* gitDir = git_repository_path(_repository);
        if (!gitDir) {
            if (error)
                *error = QStringLiteral("Cannot locate interactive rebase state.");
            return false;
        }
        const QString path = QDir(QString::fromUtf8(gitDir))
            .filePath(QStringLiteral("rebase-merge/rewritten"));
        const QString backupPath = QDir(QString::fromUtf8(gitDir))
            .filePath(QStringLiteral("rebase-merge/gitmanager-rewritten"));
        QStringList lines;
        auto readMappings = [](const QString& filePath,
                               QStringList* mappings) {
            QFile input(filePath);
            if (!input.open(QIODevice::ReadOnly))
                return false;
            const QStringList candidate = QString::fromUtf8(input.readAll())
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& line : candidate) {
                const int separator = line.indexOf(QLatin1Char(' '));
                if (separator <= 0
                    || !validOidText(line.left(separator))
                    || !validOidText(line.mid(separator + 1).trimmed())) {
                    return false;
                }
            }
            if (mappings)
                *mappings = candidate;
            return true;
        };
        auto containsMapping = [](const QStringList& mappings,
                                  const QString& from,
                                  const QString& to) {
            for (const QString& line : mappings) {
                const int separator = line.indexOf(QLatin1Char(' '));
                if (line.left(separator).compare(from, Qt::CaseInsensitive) == 0
                    && line.mid(separator + 1).trimmed().compare(
                           to, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
            return false;
        };
        auto containsTarget = [](const QStringList& mappings,
                                 const QString& target) {
            for (const QString& line : mappings) {
                const int separator = line.indexOf(QLatin1Char(' '));
                if (line.mid(separator + 1).trimmed().compare(
                        target, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
            return false;
        };
        const bool mappingExists = QFileInfo::exists(path);
        const bool backupExists = QFileInfo::exists(backupPath);
        QStringList primaryLines;
        QStringList backupLines;
        const bool primaryValid = mappingExists
            && readMappings(path, &primaryLines);
        const bool backupValid = backupExists
            && readMappings(backupPath, &backupLines);

        // The backup is committed before the libgit2-owned file is replaced.
        // Seeing the current post-action mapping in it therefore identifies a
        // complete retry state, even if the primary file contains a valid but
        // truncated prefix from an interrupted write.
        if (backupValid && containsMapping(backupLines, source, newTarget)) {
            lines = backupLines;
        } else if (primaryValid
                   && (containsMapping(primaryLines, source, newTarget)
                       || containsTarget(primaryLines, oldTarget))) {
            lines = primaryLines;
        } else {
            if (error)
                *error = QStringLiteral("Cannot recover rewritten commit mapping.");
            return false;
        }
        bool sourcePresent = false;
        for (QString& line : lines) {
            const int separator = line.indexOf(QLatin1Char(' '));
            const QString from = line.left(separator);
            QString to = line.mid(separator + 1).trimmed();
            if (to.compare(oldTarget, Qt::CaseInsensitive) == 0)
                to = newTarget;
            if (from.compare(source, Qt::CaseInsensitive) == 0) {
                to = newTarget;
                sourcePresent = true;
            }
            line = from + QLatin1Char(' ') + to;
        }
        if (!sourcePresent)
            lines.append(source + QLatin1Char(' ') + newTarget);
        if (!containsMapping(lines, source, newTarget)
            || (oldTarget.compare(newTarget, Qt::CaseInsensitive) != 0
                && containsTarget(lines, oldTarget))) {
            if (error)
                *error = QStringLiteral("Cannot safely update rewritten commit mapping.");
            return false;
        }
        QByteArray data = lines.join(QLatin1Char('\n')).toUtf8();
        data.append('\n');

        QSaveFile backup(backupPath);
        if (!backup.open(QIODevice::WriteOnly)
            || backup.write(data) != data.size()
            || !backup.commit()) {
            if (error) {
                *error = QStringLiteral("Cannot back up rewritten commit mapping: %1")
                             .arg(backup.errorString());
            }
            return false;
        }

        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || output.write(data) != data.size() || !output.commit()) {
            if (error) {
                *error = QStringLiteral("Cannot save rewritten commit mapping: %1")
                             .arg(output.errorString());
            }
            return false;
        }
        return true;
    };

    auto combinedMessage = [](git_commit* previous,
                              const RebasePlanItem& item) {
        QString message = text(git_commit_message(previous));
        if (item.action == RebaseAction::Squash) {
            QString incoming = item.rewrittenMessage.isEmpty()
                ? item.message : item.rewrittenMessage;
            while (message.endsWith(QLatin1Char('\n')))
                message.chop(1);
            incoming = incoming.trimmed();
            if (!incoming.isEmpty())
                message += QStringLiteral("\n\n") + incoming;
        }
        return message;
    };

    auto sameParents = [](git_commit* left, git_commit* right) {
        const size_t count = git_commit_parentcount(left);
        if (count != git_commit_parentcount(right))
            return false;
        for (size_t index = 0; index < count; ++index) {
            if (git_oid_cmp(git_commit_parent_id(left, index),
                            git_commit_parent_id(right, index)) != 0) {
                return false;
            }
        }
        return true;
    };

    auto sameAuthor = [](git_commit* left, git_commit* right) {
        const git_signature* first = git_commit_author(left);
        const git_signature* second = git_commit_author(right);
        if (!first || !second)
            return false;
        return text(first->name) == text(second->name)
            && text(first->email) == text(second->email)
            && first->when.time == second->when.time
            && first->when.offset == second->when.offset
            && first->when.sign == second->when.sign;
    };

    auto amendHead = [this, &signature, &updateRewrittenMapping,
                      &combinedMessage, &sameParents, &sameAuthor,
                      &state, error](size_t currentIndex,
                                     const RebasePlanItem& item) -> bool {
        IndexHandle index;
        CommitHandle head;
        git_oid indexTreeOid;
        if (git_repository_index(index.out(), _repository) < 0
            || git_index_has_conflicts(index.get())
            || !lookupHeadCommit(_repository, head)
            || git_index_write_tree(&indexTreeOid, index.get()) < 0) {
            return fail(error, QStringLiteral("Cannot prepare squash/fixup commit."));
        }

        const QString currentHead = oidText(git_commit_id(head.get()));
        const QString indexTree = oidText(&indexTreeOid);
        if (currentHead.compare(state.plan.preview.targetHash,
                                Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = QStringLiteral(
                    "Cannot squash or fixup without a previous rewritten commit.");
            }
            return false;
        }
        if (state.pendingIndex == -1) {
            state.pendingIndex = static_cast<int>(currentIndex);
            state.preActionHead = currentHead;
            state.pendingTree = indexTree;
            if (!saveInteractiveRebase(_repository, state, error))
                return false;
        } else if (state.pendingIndex != static_cast<int>(currentIndex)
                   || indexTree.compare(state.pendingTree,
                                        Qt::CaseInsensitive) != 0) {
            if (error) {
                *error = QStringLiteral(
                    "Interactive rebase pending state changed unexpectedly.");
            }
            return false;
        }

        CommitHandle previous;
        if (!lookupCommit(_repository, state.preActionHead, previous, error,
                          QStringLiteral("Cannot resolve pre-squash commit."))) {
            return false;
        }
        const QString message = combinedMessage(previous.get(), item);
        if (message.trimmed().isEmpty()) {
            if (error)
                *error = QStringLiteral("Squashed commit message cannot be empty.");
            return false;
        }

        if (currentHead.compare(state.preActionHead,
                                Qt::CaseInsensitive) != 0) {
            TreeHandle currentTree;
            if (git_commit_tree(currentTree.out(), head.get()) < 0
                || oidText(git_tree_id(currentTree.get())).compare(
                       state.pendingTree, Qt::CaseInsensitive) != 0
                || text(git_commit_message(head.get())) != message
                || !sameParents(head.get(), previous.get())
                || !sameAuthor(head.get(), previous.get())) {
                if (error) {
                    *error = QStringLiteral(
                        "Cannot safely recover the pending squash/fixup action.");
                }
                return false;
            }
            if (!updateRewrittenMapping(item.hash, state.preActionHead,
                                        currentHead)) {
                return false;
            }
            state.pendingIndex = -1;
            state.preActionHead.clear();
            state.pendingTree.clear();
            return true;
        }

        TreeHandle tree;
        if (git_tree_lookup(tree.out(), _repository, &indexTreeOid) < 0)
            return fail(error, QStringLiteral("Cannot prepare squash/fixup tree."));
        const QByteArray messageBytes = message.toUtf8();
        git_oid amendedOid;
        if (git_commit_amend(&amendedOid, previous.get(), "HEAD", nullptr,
                             signature.get(), nullptr, messageBytes.constData(),
                             tree.get()) < 0) {
            return fail(error, QStringLiteral("Cannot create squash/fixup commit."));
        }
        CommitHandle verifiedHead;
        if (!lookupHeadCommit(_repository, verifiedHead)
            || git_oid_cmp(git_commit_id(verifiedHead.get()), &amendedOid) != 0) {
            if (error)
                *error = QStringLiteral("Cannot verify squash/fixup commit.");
            return false;
        }
        if (!updateRewrittenMapping(item.hash, state.preActionHead,
                                    oidText(&amendedOid))) {
            return false;
        }
        state.pendingIndex = -1;
        state.preActionHead.clear();
        state.pendingTree.clear();
        return true;
    };

    auto completeCurrent = [this, rebase, &plan, &signature, &restoreHeadTree,
                            &amendHead, error](size_t current,
                                               bool resumeEdit)
        -> HistoryOperationResult {
        if (current >= static_cast<size_t>(plan.items.size())) {
            if (error)
                *error = QStringLiteral("Interactive rebase position is invalid.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
        const RebasePlanItem& item = plan.items.at(static_cast<int>(current));

        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return historyFailure(error, QStringLiteral("Cannot open rebase index."));
        const bool conflicts = git_index_has_conflicts(index.get());
        if (item.action != RebaseAction::Drop && conflicts) {
            return historyResult(HistoryOperationStatus::Conflicts,
                                 QStringLiteral("Rebase stopped because of conflicts."));
        }

        if (item.action == RebaseAction::Drop) {
            if (!restoreHeadTree())
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            return historyResult(HistoryOperationStatus::Completed,
                                 QStringLiteral("Dropped %1.").arg(item.hash.left(8)));
        }

        if (item.action == RebaseAction::Edit && !resumeEdit) {
            return historyResult(HistoryOperationStatus::PausedForEdit,
                                 QStringLiteral("Edit commit %1, stage the result, then continue.")
                                     .arg(item.hash.left(8)));
        }

        if (item.action == RebaseAction::Squash
            || item.action == RebaseAction::Fixup) {
            if (!amendHead(current, item))
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            return historyResult(HistoryOperationStatus::Completed,
                                 QStringLiteral("Combined %1.").arg(item.hash.left(8)));
        }

        QByteArray messageBytes;
        const char* message = nullptr;
        if (item.action == RebaseAction::Reword
            || item.action == RebaseAction::Edit) {
            const QString rewritten = item.rewrittenMessage.isEmpty()
                ? item.message : item.rewrittenMessage;
            if (rewritten.trimmed().isEmpty()) {
                if (error)
                    *error = QStringLiteral("Rebase commit message cannot be empty.");
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            }
            messageBytes = rewritten.toUtf8();
            message = messageBytes.constData();
        }
        git_oid commitOid;
        const int commitRc = git_rebase_commit(&commitOid, rebase, nullptr,
                                                signature.get(), nullptr,
                                                message);
        if (commitRc < 0 && commitRc != GIT_EAPPLIED) {
            return historyFailure(error,
                                  QStringLiteral("Cannot commit interactive rebase step."));
        }
        git_error_clear();
        return historyResult(HistoryOperationStatus::Completed,
                             QStringLiteral("Applied %1.").arg(item.hash.left(8)));
    };

    size_t current = git_rebase_operation_current(rebase);
    if (current == GIT_REBASE_NO_OPERATION) {
        if (completedIndex != -1 || state.pendingIndex != -1
            || pausedForEdit) {
            if (error)
                *error = QStringLiteral("Interactive rebase position is invalid.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
    } else {
        const int currentIndex = static_cast<int>(current);
        const bool positionValid = current < total
            && completedIndex >= currentIndex - 1
            && completedIndex <= currentIndex
            && (state.pendingIndex == -1
                || (state.pendingIndex == currentIndex
                    && completedIndex == currentIndex - 1))
            && (!pausedForEdit
                || (state.pendingIndex == -1
                    && completedIndex == currentIndex - 1
                    && plan.items.at(currentIndex).action
                        == RebaseAction::Edit));
        if (!positionValid) {
            if (error)
                *error = QStringLiteral("Interactive rebase position is invalid.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
    }

    if (current != GIT_REBASE_NO_OPERATION
        && completedIndex != static_cast<int>(current)) {
        const HistoryOperationResult completed = completeCurrent(current,
                                                                 pausedForEdit);
        if (completed.status == HistoryOperationStatus::PausedForEdit) {
            pausedForEdit = true;
            if (!saveInteractiveRebase(_repository, state, error)) {
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            }
            return completed;
        }
        if (completed.status == HistoryOperationStatus::Conflicts
            || (error && !error->isEmpty())) {
            return completed;
        }
        completedIndex = static_cast<int>(current);
        pausedForEdit = false;
        if (state.pendingIndex != -1
            || !saveInteractiveRebase(_repository, state, error)) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral(
                    "Interactive rebase pending state was not completed.");
            }
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
    }

    while (true) {
        if (isCancelled()) {
            if (error)
                *error = QStringLiteral("Operation cancelled; rebase remains in progress.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }

        git_rebase_operation* operation = nullptr;
        const int nextRc = git_rebase_next(&operation, rebase);
        if (nextRc == GIT_ITEROVER)
            break;

        current = git_rebase_operation_current(rebase);
        if (nextRc == GIT_EMERGECONFLICT) {
            if (current < static_cast<size_t>(plan.items.size())
                && plan.items.at(static_cast<int>(current)).action
                    == RebaseAction::Drop) {
                if (!restoreHeadTree()) {
                    return historyResult(HistoryOperationStatus::Completed,
                                         error ? *error : QString());
                }
                completedIndex = static_cast<int>(current);
                pausedForEdit = false;
                if (!saveInteractiveRebase(_repository, state, error)) {
                    return historyResult(HistoryOperationStatus::Completed,
                                         error ? *error : QString());
                }
                continue;
            }
            return historyResult(HistoryOperationStatus::Conflicts,
                                 QStringLiteral("Rebase stopped because of conflicts."));
        }
        if (nextRc < 0)
            return historyFailure(error,
                                  QStringLiteral("Cannot apply next interactive rebase step."));
        if (!operation || current >= static_cast<size_t>(plan.items.size())) {
            if (error)
                *error = QStringLiteral("Interactive rebase position is invalid.");
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }

        progress(QStringLiteral("Interactive rebase %1/%2")
                     .arg(current + 1).arg(total),
                 total > 0
                     ? static_cast<int>((current + 1) * 100 / total) : -1);

        const HistoryOperationResult completed = completeCurrent(current, false);
        if (completed.status == HistoryOperationStatus::PausedForEdit) {
            pausedForEdit = true;
            if (!saveInteractiveRebase(_repository, state, error)) {
                return historyResult(HistoryOperationStatus::Completed,
                                     error ? *error : QString());
            }
            return completed;
        }
        if (completed.status == HistoryOperationStatus::Conflicts
            || (error && !error->isEmpty())) {
            return completed;
        }
        completedIndex = static_cast<int>(current);
        pausedForEdit = false;
        if (state.pendingIndex != -1
            || !saveInteractiveRebase(_repository, state, error)) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral(
                    "Interactive rebase pending state was not completed.");
            }
            return historyResult(HistoryOperationStatus::Completed,
                                 error ? *error : QString());
        }
    }

    if (state.pendingIndex != -1) {
        if (error)
            *error = QStringLiteral("Interactive rebase has an incomplete pending action.");
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    if (git_rebase_finish(rebase, signature.get()) < 0)
        return historyFailure(error, QStringLiteral("Cannot finish interactive rebase."));
    if (!removeInteractiveRebase(_repository, error)) {
        return historyResult(HistoryOperationStatus::Completed,
                             error ? *error : QString());
    }
    progress(QStringLiteral("Interactive rebase completed"), 100);
    return historyResult(HistoryOperationStatus::Completed,
                         QStringLiteral("Interactive rebase completed."));
}

bool LibGit2Backend::createStateCommit(const QString& operation,
                                       QString* error)
{
    IndexHandle index;
    if (git_repository_index(index.out(), _repository) < 0)
        return fail(error, QStringLiteral("Cannot open repository index."));
    if (git_index_has_conflicts(index.get())) {
        if (error)
            *error = QStringLiteral("Resolve all conflicts before continuing.");
        return false;
    }

    git_buf preparedMessage = GIT_BUF_INIT;
    const int messageRc = git_repository_message(&preparedMessage, _repository);
    QString message = messageRc == 0
        ? QString::fromUtf8(preparedMessage.ptr,
                            static_cast<int>(preparedMessage.size)).trimmed()
        : QString();
    git_buf_dispose(&preparedMessage);
    if (message.isEmpty()) {
        git_error_clear();
        message = operation == QStringLiteral("merge")
            ? QStringLiteral("Merge commit")
            : operation == QStringLiteral("revert")
                ? QStringLiteral("Revert commit")
                : QStringLiteral("Cherry-pick commit");
    }

    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0)
        return fail(error, QStringLiteral("Git user.name and user.email are required."));
    CommitHandle sourceCommit;
    const git_signature* author = signature.get();
    if (operation == QStringLiteral("cherry-pick")
        || operation == QStringLiteral("cherrypick")) {
        if (!lookupCommit(_repository, QStringLiteral("CHERRY_PICK_HEAD"),
                          sourceCommit, error,
                          QStringLiteral("Cannot resolve cherry-pick author.")))
            return false;
        author = git_commit_author(sourceCommit.get());
    }
    git_commit_create_options options = GIT_COMMIT_CREATE_OPTIONS_INIT;
    options.author = author;
    options.committer = signature.get();
    git_oid oid;
    if (git_commit_create_from_stage(&oid, _repository,
                                     message.toUtf8().constData(), &options) < 0)
        return fail(error, QStringLiteral("Cannot continue operation with a commit."));
    if (git_repository_state_cleanup(_repository) < 0)
        return fail(error, QStringLiteral("Commit created, but operation state cleanup failed."));
    return true;
}

bool LibGit2Backend::isCancelled() const
{
    return _cancelFlag && _cancelFlag->load(std::memory_order_relaxed);
}

void LibGit2Backend::progress(const QString& value, int percent) const
{
    if (_progressCallback)
        _progressCallback(value, percent);
}

QString LibGit2Backend::lastError(const QString& fallback)
{
    const git_error* error = git_error_last();
    return error && error->message
        ? QString::fromUtf8(error->message) : fallback;
}

QString LibGit2Backend::reversePatch(const QString& patch)
{
    QStringList lines = patch.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    auto oldSidePath = [](QString path) {
        if (path.startsWith(QStringLiteral("b/")))
            path.replace(0, 2, QStringLiteral("a/"));
        else if (path.startsWith(QStringLiteral("\"b/")))
            path.replace(1, 2, QStringLiteral("a/"));
        return path;
    };
    auto newSidePath = [](QString path) {
        if (path.startsWith(QStringLiteral("a/")))
            path.replace(0, 2, QStringLiteral("b/"));
        else if (path.startsWith(QStringLiteral("\"a/")))
            path.replace(1, 2, QStringLiteral("b/"));
        return path;
    };

    for (qsizetype index = 0; index < lines.size(); ++index) {
        QString& line = lines[index];
        if (line.startsWith(QStringLiteral("diff --git "))) {
            static const QRegularExpression expression(
                QStringLiteral("^diff --git a/(.+) b/(.+)$"));
            const QRegularExpressionMatch match = expression.match(line);
            if (match.hasMatch()) {
                line = QStringLiteral("diff --git a/%1 b/%2")
                           .arg(match.captured(2), match.captured(1));
            } else {
                static const QRegularExpression quotedExpression(
                    QStringLiteral("^diff --git \"a/(.*)\" \"b/(.*)\"$"));
                const QRegularExpressionMatch quoted = quotedExpression.match(line);
                if (quoted.hasMatch())
                    line = QStringLiteral("diff --git \"a/%1\" \"b/%2\"")
                               .arg(quoted.captured(2), quoted.captured(1));
            }
        } else if (line.startsWith(QStringLiteral("--- "))
                   && index + 1 < lines.size()
                   && lines[index + 1].startsWith(QStringLiteral("+++ "))) {
            const QString originalOld = line.mid(4);
            const QString originalNew = lines[index + 1].mid(4);
            line = QStringLiteral("--- ") + oldSidePath(originalNew);
            lines[index + 1] = QStringLiteral("+++ ") + newSidePath(originalOld);
            ++index;
        } else if (line.startsWith(QStringLiteral("new file mode "))) {
            line.replace(0, 14, QStringLiteral("deleted file mode "));
        } else if (line.startsWith(QStringLiteral("deleted file mode "))) {
            line.replace(0, 18, QStringLiteral("new file mode "));
        } else if (line.startsWith(QStringLiteral("rename from "))
                   && index + 1 < lines.size()
                   && lines[index + 1].startsWith(QStringLiteral("rename to "))) {
            const QString originalOld = line.mid(12);
            const QString originalNew = lines[index + 1].mid(10);
            line = QStringLiteral("rename from ") + originalNew;
            lines[index + 1] = QStringLiteral("rename to ") + originalOld;
            ++index;
        } else if (line.startsWith(QStringLiteral("index "))) {
            static const QRegularExpression expression(
                QStringLiteral("^index ([0-9a-fA-F]+)\\.\\.([0-9a-fA-F]+)(.*)$"));
            const QRegularExpressionMatch match = expression.match(line);
            if (match.hasMatch())
                line = QStringLiteral("index %1..%2%3")
                           .arg(match.captured(2), match.captured(1), match.captured(3));
        } else if (line.startsWith(QStringLiteral("@@ "))) {
            static const QRegularExpression expression(
                QStringLiteral("^@@ -(\\d+)(,\\d+)? \\+(\\d+)(,\\d+)? @@(.*)$"));
            const QRegularExpressionMatch match = expression.match(line);
            if (match.hasMatch())
                line = QStringLiteral("@@ -%1%2 +%3%4 @@%5")
                           .arg(match.captured(3), match.captured(4),
                                match.captured(1), match.captured(2),
                                match.captured(5));
        } else if (line.startsWith(QLatin1Char('+'))
                   && !line.startsWith(QStringLiteral("+++"))) {
            line[0] = QLatin1Char('-');
        } else if (line.startsWith(QLatin1Char('-'))
                   && !line.startsWith(QStringLiteral("---"))) {
            line[0] = QLatin1Char('+');
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace Git
