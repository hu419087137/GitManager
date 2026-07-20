#include "RepositoryWatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace {
constexpr int kRefreshDebounceMs = 350;
}

namespace Git {

RepositoryWatcher::RepositoryWatcher(QObject* parent)
    : QObject(parent)
{
    _watcher = new QFileSystemWatcher(this);
    _debounceTimer = new QTimer(this);
    _debounceTimer->setSingleShot(true);
    _debounceTimer->setInterval(kRefreshDebounceMs);

    connect(_watcher, &QFileSystemWatcher::fileChanged,
            this, &RepositoryWatcher::scheduleRefresh);
    connect(_watcher, &QFileSystemWatcher::directoryChanged,
            this, &RepositoryWatcher::scheduleRefresh);
    connect(_debounceTimer, &QTimer::timeout,
            this, &RepositoryWatcher::sigRefreshRequested);
}

void RepositoryWatcher::setRepositoryPath(const QString& path)
{
    const QString normalized = QDir(path).absolutePath();
    if (_repositoryPath == normalized)
        return;

    _repositoryPath = normalized;
    _gitPath.clear();
    _debounceTimer->stop();

    const QStringList files = _watcher->files();
    if (!files.isEmpty())
        _watcher->removePaths(files);
    const QStringList directories = _watcher->directories();
    if (!directories.isEmpty())
        _watcher->removePaths(directories);

    if (_repositoryPath.isEmpty())
        return;

    _gitPath = QDir(_repositoryPath).filePath(QStringLiteral(".git"));
    const QFileInfo dotGitInfo(_gitPath);
    if (dotGitInfo.isFile()) {
        // Worktree/submodule setups may store a .git file that points elsewhere.
        _watcher->addPath(_gitPath);
        return;
    }

    rebuildWatchList();
}

void RepositoryWatcher::notifyWindowActivated()
{
    if (_repositoryPath.isEmpty())
        return;
    if (hasWatchedChanges())
        scheduleRefresh();
}

void RepositoryWatcher::scheduleRefresh()
{
    if (_repositoryPath.isEmpty())
        return;
    rebuildWatchList();
    _debounceTimer->start();
}

void RepositoryWatcher::rebuildWatchList()
{
    if (_repositoryPath.isEmpty())
        return;

    const QStringList currentFiles = _watcher->files();
    if (!currentFiles.isEmpty())
        _watcher->removePaths(currentFiles);
    const QStringList currentDirs = _watcher->directories();
    if (!currentDirs.isEmpty())
        _watcher->removePaths(currentDirs);

    if (_gitPath.isEmpty())
        _gitPath = QDir(_repositoryPath).filePath(QStringLiteral(".git"));

    const QFileInfo dotGitInfo(_gitPath);
    if (dotGitInfo.isFile()) {
        _watcher->addPath(_gitPath);
        return;
    }

    const QString headPath = QDir(_gitPath).filePath(QStringLiteral("HEAD"));
    const QString indexPath = QDir(_gitPath).filePath(QStringLiteral("index"));
    const QString refsHeadsPath = QDir(_gitPath).filePath(QStringLiteral("refs/heads"));
    const QString refsTagsPath = QDir(_gitPath).filePath(QStringLiteral("refs/tags"));
    const QString refsRemotesPath = QDir(_gitPath).filePath(QStringLiteral("refs/remotes"));
    const QString packedRefsPath = QDir(_gitPath).filePath(QStringLiteral("packed-refs"));

    if (QFileInfo::exists(headPath))
        _watcher->addPath(headPath);
    if (QFileInfo::exists(indexPath))
        _watcher->addPath(indexPath);
    if (QFileInfo::exists(packedRefsPath))
        _watcher->addPath(packedRefsPath);
    if (QFileInfo(refsHeadsPath).isDir())
        _watcher->addPath(refsHeadsPath);
    if (QFileInfo(refsTagsPath).isDir())
        _watcher->addPath(refsTagsPath);
    if (QFileInfo(refsRemotesPath).isDir())
        _watcher->addPath(refsRemotesPath);
}

bool RepositoryWatcher::hasWatchedChanges() const
{
    if (_gitPath.isEmpty())
        return false;
    const QFileInfo dotGitInfo(_gitPath);
    if (dotGitInfo.isFile())
        return dotGitInfo.exists();
    return QFileInfo(QDir(_gitPath).filePath(QStringLiteral("HEAD"))).exists();
}

} // namespace Git
