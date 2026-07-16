#include "LibGit2Backend.h"
#include <git2.h>
#include <QDir>

namespace Git {
namespace {
File::Status state(unsigned int flags, bool index)
{
    if (flags & (index ? GIT_STATUS_INDEX_NEW : GIT_STATUS_WT_NEW)) return File::Status::E_Added;
    if (flags & (index ? GIT_STATUS_INDEX_MODIFIED : GIT_STATUS_WT_MODIFIED)) return File::Status::E_Modified;
    if (flags & (index ? GIT_STATUS_INDEX_DELETED : GIT_STATUS_WT_DELETED)) return File::Status::E_Deleted;
    if (flags & (index ? GIT_STATUS_INDEX_RENAMED : GIT_STATUS_WT_RENAMED)) return File::Status::E_Renamed;
    if (flags & (index ? GIT_STATUS_INDEX_TYPECHANGE : GIT_STATUS_WT_TYPECHANGE)) return File::Status::E_TypeChanged;
    return File::Status::E_Unmodified;
}
}

LibGit2Backend::LibGit2Backend() { git_libgit2_init(); }
LibGit2Backend::~LibGit2Backend() { close(); git_libgit2_shutdown(); }

bool LibGit2Backend::open(const QString& path, QString* error)
{
    close();
    const QByteArray native = QDir::toNativeSeparators(path).toUtf8();
    if (git_repository_open_ext(&_repository, native.constData(), 0, nullptr) < 0) {
        if (error) *error = lastError(QStringLiteral("Cannot open repository."));
        return false;
    }
    return true;
}

void LibGit2Backend::close() { git_repository_free(_repository); _repository = nullptr; }

QString LibGit2Backend::rootPath() const
{
    if (!_repository) return {};
    const char* workdir = git_repository_workdir(_repository);
    return workdir ? QDir::cleanPath(QString::fromUtf8(workdir)) : QString();
}

StatusSummary LibGit2Backend::status(QString* error) const
{
    StatusSummary result;
    if (!_repository) return result;
    git_reference* head = nullptr;
    const int headResult = git_repository_head(&head, _repository);
    if (headResult == 0) {
        result.headName = QString::fromUtf8(git_reference_shorthand(head));
        const git_oid* oid = git_reference_target(head);
        if (oid) { char text[GIT_OID_HEXSZ + 1]{}; git_oid_tostr(text, sizeof(text), oid); result.headHash = QString::fromLatin1(text); }
    } else if (headResult == GIT_EUNBORNBRANCH) {
        result.unborn = true;
        git_reference* symbolic = nullptr;
        if (git_repository_head(&symbolic, _repository) == 0) git_reference_free(symbolic);
    }
    result.detached = git_repository_head_detached(_repository) == 1;
    git_reference_free(head);

    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                  | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
    git_status_list* list = nullptr;
    if (git_status_list_new(&list, _repository, &options) < 0) {
        if (error) *error = lastError(QStringLiteral("Cannot read repository status."));
        return result;
    }
    const size_t count = git_status_list_entrycount(list);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry* entry = git_status_byindex(list, i);
        File file;
        const git_diff_delta* delta = entry->index_to_workdir ? entry->index_to_workdir : entry->head_to_index;
        if (!delta) continue;
        file.path = QString::fromUtf8(delta->new_file.path ? delta->new_file.path : delta->old_file.path);
        if (delta->old_file.path && delta->new_file.path && qstrcmp(delta->old_file.path, delta->new_file.path) != 0)
            file.originalPath = QString::fromUtf8(delta->old_file.path);
        file.indexStatus = state(entry->status, true);
        file.workStatus = state(entry->status, false);
        file.tracked = !(entry->status & GIT_STATUS_WT_NEW);
        file.conflicted = (entry->status & GIT_STATUS_CONFLICTED) != 0;
        if (!file.tracked) file.workStatus = File::Status::E_Untracked;
        if (file.conflicted) file.indexStatus = file.workStatus = File::Status::E_Unmerged;
        result.files.append(file);
    }
    git_status_list_free(list);
    return result;
}

bool LibGit2Backend::stage(const QString& path, QString* error)
{
    git_index* index=nullptr; if(git_repository_index(&index,_repository)<0)return false;
    const QByteArray p=path.toUtf8(); const int rc=git_index_add_bypath(index,p.constData());
    const int writeRc=rc==0?git_index_write(index):rc; git_index_free(index);
    if(writeRc<0&&error)*error=lastError(QStringLiteral("Cannot stage file.")); return writeRc==0;
}
bool LibGit2Backend::stageAll(QString* error)
{
    git_index* index=nullptr; if(git_repository_index(&index,_repository)<0)return false;
    git_strarray paths{}; const int rc=git_index_add_all(index,&paths,GIT_INDEX_ADD_DEFAULT,nullptr,nullptr);
    const int writeRc=rc==0?git_index_write(index):rc; git_index_free(index);
    if(writeRc<0&&error)*error=lastError(QStringLiteral("Cannot stage files.")); return writeRc==0;
}
bool LibGit2Backend::unstage(const QString& path, QString* error)
{
    git_object* head=nullptr; git_revparse_single(&head,_repository,"HEAD^{tree}");
    const QByteArray p=path.toUtf8(); const char* value=p.constData(); git_strarray paths{const_cast<char**>(&value),1};
    const int rc=git_reset_default(_repository,head,&paths); git_object_free(head);
    if(rc<0&&error)*error=lastError(QStringLiteral("Cannot unstage file.")); return rc==0;
}
bool LibGit2Backend::discard(const QString& path, QString* error)
{
    git_checkout_options opts=GIT_CHECKOUT_OPTIONS_INIT; opts.checkout_strategy=GIT_CHECKOUT_FORCE;
    QByteArray p=path.toUtf8(); char* value=p.data(); git_strarray paths{&value,1}; opts.paths=paths;
    const int rc=git_checkout_head(_repository,&opts); if(rc<0&&error)*error=lastError(QStringLiteral("Cannot discard file.")); return rc==0;
}
QString LibGit2Backend::lastError(const QString& fallback)
{
    const git_error* e=git_error_last(); return e&&e->message?QString::fromUtf8(e->message):fallback;
}
} // namespace Git
