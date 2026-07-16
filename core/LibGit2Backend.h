#ifndef LIBGIT2BACKEND_H
#define LIBGIT2BACKEND_H

#include "GitTypes.h"
#include <QString>
#include <git2/types.h>

namespace Git {

class LibGit2Backend {
public:
    LibGit2Backend();
    ~LibGit2Backend();
    LibGit2Backend(const LibGit2Backend&) = delete;
    LibGit2Backend& operator=(const LibGit2Backend&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const { return _repository != nullptr; }
    QString rootPath() const;
    StatusSummary status(QString* error = nullptr) const;
    bool stage(const QString& path, QString* error = nullptr);
    bool unstage(const QString& path, QString* error = nullptr);
    bool stageAll(QString* error = nullptr);
    bool discard(const QString& path, QString* error = nullptr);

    static QString lastError(const QString& fallback);

private:
    git_repository* _repository {nullptr};
};

} // namespace Git
#endif
