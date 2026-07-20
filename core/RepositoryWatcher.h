#ifndef REPOSITORYWATCHER_H
#define REPOSITORYWATCHER_H

#include <QObject>

class QFileSystemWatcher;
class QTimer;

namespace Git {

class RepositoryWatcher : public QObject {
    Q_OBJECT

public:
    explicit RepositoryWatcher(QObject* parent = nullptr);

    void setRepositoryPath(const QString& path);
    QString repositoryPath() const { return _repositoryPath; }
    void notifyWindowActivated();

signals:
    void sigRefreshRequested();

private slots:
    void scheduleRefresh();

private:
    void rebuildWatchList();
    bool hasWatchedChanges() const;

    QFileSystemWatcher* _watcher {nullptr};
    QTimer* _debounceTimer {nullptr};
    QString _repositoryPath;
    QString _gitPath;
};

} // namespace Git

#endif // REPOSITORYWATCHER_H
