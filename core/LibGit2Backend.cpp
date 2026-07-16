#include "LibGit2Backend.h"

#include <git2.h>
#include <git2/sys/errors.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <utility>

namespace Git {
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
using DiffHandle = GitHandle<git_diff, git_diff_free>;
using IndexHandle = GitHandle<git_index, git_index_free>;
using ObjectHandle = GitHandle<git_object, git_object_free>;
using RebaseHandle = GitHandle<git_rebase, git_rebase_free>;
using ReferenceHandle = GitHandle<git_reference, git_reference_free>;
using RemoteHandle = GitHandle<git_remote, git_remote_free>;
using SignatureHandle = GitHandle<git_signature, git_signature_free>;
using StatusListHandle = GitHandle<git_status_list, git_status_list_free>;
using TreeHandle = GitHandle<git_tree, git_tree_free>;
using TreeEntryHandle = GitHandle<git_tree_entry, git_tree_entry_free>;

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

File::Status statusValue(unsigned int flags, bool index)
{
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

QString branchNameFromRef(const char* refName)
{
    const QString full = text(refName);
    if (full.startsWith(QStringLiteral("refs/heads/")))
        return full.mid(11);
    if (full.startsWith(QStringLiteral("refs/remotes/")))
        return full.mid(13);
    return full;
}

struct NetworkContext {
    QByteArray username;
    QByteArray secret;
    LibGit2Backend::ProgressCallback progress;
    const std::atomic_bool* cancelFlag {nullptr};
    QString pushError;

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

} // namespace

LibGit2Backend::LibGit2Backend()
{
    git_libgit2_init();
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

    QHash<QString, QStringList> refsByCommit;
    if (result.detached && !result.headHash.isEmpty())
        refsByCommit[result.headHash].append(QStringLiteral("HEAD"));
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
                addRef(refsByCommit, git_object_id(commitObject.get()),
                       value.isCurrent
                           ? QStringLiteral("HEAD -> %1").arg(value.name)
                           : value.name);
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

    git_reference_iterator* referenceIterator = nullptr;
    if (git_reference_iterator_glob_new(&referenceIterator, _repository,
                                        "refs/tags/*") == 0) {
        git_reference* rawReference = nullptr;
        while (git_reference_next(&rawReference, referenceIterator) == 0) {
            ReferenceHandle reference(rawReference);
            rawReference = nullptr;
            ObjectHandle commitObject;
            if (git_reference_peel(commitObject.out(), reference.get(),
                                   GIT_OBJECT_COMMIT) == 0) {
                addRef(refsByCommit, git_object_id(commitObject.get()),
                       QStringLiteral("tag: %1")
                           .arg(text(git_reference_shorthand(reference.get()))));
            } else {
                git_error_clear();
            }
        }
        git_reference_iterator_free(referenceIterator);
        git_error_clear();
    } else {
        git_error_clear();
    }

    if (!result.unborn && !result.headHash.isEmpty()) {
        git_revwalk* walk = nullptr;
        if (git_revwalk_new(&walk, _repository) == 0) {
            git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
            int pushRc = git_revwalk_push_head(walk);
            git_oid oid;
            int count = 0;
            while (pushRc == 0 && count < 1000
                   && git_revwalk_next(&oid, walk) == 0) {
                CommitHandle commit;
                if (git_commit_lookup(commit.out(), _repository, &oid) < 0) {
                    git_error_clear();
                    continue;
                }
                Commit value;
                value.hash = oidText(&oid);
                value.shortHash = value.hash.left(8);
                const unsigned int parentCount = git_commit_parentcount(commit.get());
                for (unsigned int parent = 0; parent < parentCount; ++parent)
                    value.parents.append(oidText(git_commit_parent_id(commit.get(), parent)));
                const git_signature* author = git_commit_author(commit.get());
                value.authorName = author ? text(author->name) : QString();
                value.date = QDateTime::fromSecsSinceEpoch(
                    git_commit_time(commit.get()), Qt::UTC);
                value.subject = text(git_commit_summary(commit.get()));
                value.refs = refsByCommit.value(value.hash);
                result.commits.append(value);
                ++count;
            }
            git_revwalk_free(walk);
            git_error_clear();
        } else {
            fail(error, QStringLiteral("Cannot read commit history."));
            git_error_clear();
        }
    }
    assignLanes(result.commits);

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

bool LibGit2Backend::checkoutBranch(const QString& name, QString* error)
{
    if (!ensureRepository(_repository, error))
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
    if (!ensureRepository(_repository, error))
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
    if (!ensureRepository(_repository, error))
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

bool LibGit2Backend::createTag(const QString& name, const QString& target,
                               const QString& message, QString* error)
{
    if (!ensureRepository(_repository, error))
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
    if (!ensureRepository(_repository, error))
        return false;
    if (git_tag_delete(_repository, name.toUtf8().constData()) < 0)
        return fail(error, QStringLiteral("Cannot delete tag."));
    return true;
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
                          bool setUpstream, QString* error)
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
    const QByteArray refspecValue = QStringLiteral("refs/heads/%1:refs/heads/%2")
        .arg(localBranch, remoteBranch).toUtf8();
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

    if (setUpstream) {
        const QString remoteTracking = actualRemote + QLatin1Char('/') + remoteBranch;
        const QByteArray localBytes = localBranch.toUtf8();
        ReferenceHandle local;
        if (git_branch_lookup(local.out(), _repository, localBytes.constData(),
                              GIT_BRANCH_LOCAL) < 0)
            return fail(error, QStringLiteral("Push succeeded, but upstream could not be set."));
        const git_oid* localOid = git_reference_target(local.get());
        const QByteArray trackingRef = QStringLiteral("refs/remotes/%1/%2")
            .arg(actualRemote, remoteBranch).toUtf8();
        ReferenceHandle tracking;
        if (!localOid
            || git_reference_create(tracking.out(), _repository,
                                    trackingRef.constData(), localOid, 1,
                                    "update by push") < 0
            || git_branch_set_upstream(local.get(),
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

bool LibGit2Backend::continueOperation(const QString& operation,
                                       QString* error)
{
    if (!ensureRepository(_repository, error))
        return false;
    const QString name = operation.trimmed().toLower();
    const int state = git_repository_state(_repository);
    if (name == QStringLiteral("rebase")) {
        if (state != GIT_REPOSITORY_STATE_REBASE
            && state != GIT_REPOSITORY_STATE_REBASE_INTERACTIVE
            && state != GIT_REPOSITORY_STATE_REBASE_MERGE) {
            if (error)
                *error = QStringLiteral("No matching rebase is in progress.");
            return false;
        }
        RebaseHandle rebase;
        git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
        options.checkout_options.checkout_strategy = GIT_CHECKOUT_SAFE
                                                   | GIT_CHECKOUT_RECREATE_MISSING;
        if (git_rebase_open(rebase.out(), _repository, &options) < 0)
            return fail(error, QStringLiteral("No rebase is in progress."));
        return finishRebase(rebase.get(), error);
    }
    const bool matchesMerge = name == QStringLiteral("merge")
        && state == GIT_REPOSITORY_STATE_MERGE;
    const bool matchesCherryPick = (name == QStringLiteral("cherry-pick")
                                    || name == QStringLiteral("cherrypick"))
        && (state == GIT_REPOSITORY_STATE_CHERRYPICK
            || state == GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE);
    const bool matchesRevert = name == QStringLiteral("revert")
        && (state == GIT_REPOSITORY_STATE_REVERT
            || state == GIT_REPOSITORY_STATE_REVERT_SEQUENCE);
    if (matchesMerge || matchesCherryPick || matchesRevert)
        return createStateCommit(name, error);

    if (error)
        *error = QStringLiteral("No matching operation is in progress.");
    return false;
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
        return true;
    }

    const int state = git_repository_state(_repository);
    const bool matchesMerge = name == QStringLiteral("merge")
        && state == GIT_REPOSITORY_STATE_MERGE;
    const bool matchesCherryPick = (name == QStringLiteral("cherry-pick")
                                    || name == QStringLiteral("cherrypick"))
        && (state == GIT_REPOSITORY_STATE_CHERRYPICK
            || state == GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE);
    const bool matchesRevert = name == QStringLiteral("revert")
        && (state == GIT_REPOSITORY_STATE_REVERT
            || state == GIT_REPOSITORY_STATE_REVERT_SEQUENCE);
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
    SignatureHandle signature;
    if (git_signature_default(signature.out(), _repository) < 0)
        return fail(error, QStringLiteral("Git user.name and user.email are required."));

    const size_t total = git_rebase_operation_entrycount(rebase);
    size_t current = git_rebase_operation_current(rebase);
    if (current != GIT_REBASE_NO_OPERATION) {
        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return fail(error, QStringLiteral("Cannot open rebase index."));
        if (git_index_has_conflicts(index.get())) {
            if (error)
                *error = QStringLiteral("Resolve all conflicts before continuing rebase.");
            return false;
        }
        git_oid commitOid;
        const int commitRc = git_rebase_commit(&commitOid, rebase, nullptr,
                                                signature.get(), nullptr, nullptr);
        if (commitRc < 0 && commitRc != GIT_EAPPLIED)
            return fail(error, QStringLiteral("Cannot commit resolved rebase step."));
        git_error_clear();
    }

    while (true) {
        if (isCancelled()) {
            if (error)
                *error = QStringLiteral("Operation cancelled; rebase remains in progress.");
            return false;
        }
        git_rebase_operation* rebaseOperation = nullptr;
        const int nextRc = git_rebase_next(&rebaseOperation, rebase);
        if (nextRc == GIT_ITEROVER)
            break;
        if (nextRc == GIT_EMERGECONFLICT) {
            if (error)
                *error = QStringLiteral("Rebase stopped because of conflicts.");
            return false;
        }
        if (nextRc < 0)
            return fail(error, QStringLiteral("Cannot apply next rebase step."));

        current = git_rebase_operation_current(rebase);
        progress(QStringLiteral("Rebasing %1/%2")
                     .arg(current + 1).arg(total),
                 total > 0 ? static_cast<int>((current + 1) * 100 / total) : -1);

        IndexHandle index;
        if (git_repository_index(index.out(), _repository) < 0)
            return fail(error, QStringLiteral("Cannot open rebase index."));
        if (git_index_has_conflicts(index.get())) {
            if (error)
                *error = QStringLiteral("Rebase stopped because of conflicts.");
            return false;
        }

        git_oid commitOid;
        const int commitRc = git_rebase_commit(&commitOid, rebase, nullptr,
                                                signature.get(), nullptr, nullptr);
        if (commitRc < 0 && commitRc != GIT_EAPPLIED)
            return fail(error, QStringLiteral("Cannot commit rebase step."));
        git_error_clear();
    }
    if (git_rebase_finish(rebase, signature.get()) < 0)
        return fail(error, QStringLiteral("Cannot finish rebase."));
    progress(QStringLiteral("Rebase completed"), 100);
    return true;
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

void LibGit2Backend::assignLanes(QVector<Commit>& commits)
{
    QVector<QString> lanes;
    for (Commit& commit : commits) {
        int lane = lanes.indexOf(commit.hash);
        if (lane < 0) {
            lane = lanes.indexOf(QString());
            if (lane < 0) {
                lane = lanes.size();
                lanes.append(QString());
            }
        }
        commit.lane = lane;
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
        while (!commit.activeLanes.isEmpty()
               && commit.activeLanes.last().isEmpty())
            commit.activeLanes.removeLast();
    }
}

} // namespace Git
