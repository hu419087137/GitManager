#ifndef GITCOMMANDRUNNER_H
#define GITCOMMANDRUNNER_H

#include "GitResult.h"
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>

namespace Git {

class GitCommandRunner : public QObject {
    Q_OBJECT

public:
    explicit GitCommandRunner(QObject* parent = nullptr);

    quint64 enqueue(const QString& operation, const QString& workingDirectory,
                    const QStringList& arguments, bool networkOperation = false,
                    int timeoutMs = -1);
    void cancelCurrent();
    void cancelAll();
    bool isBusy() const { return _process.state() != QProcess::NotRunning; }

    static QString formatCommand(const QStringList& arguments);

signals:
    void sigStarted(quint64 requestId, const QString& operation, const QString& command);
    void sigOutput(quint64 requestId, const QByteArray& data, bool standardError);
    void sigFinished(const Git::GitResult& result);
    void sigBusyChanged(bool busy);

private:
    struct Request {
        quint64 id;
        QString operation;
        QString workingDirectory;
        QStringList arguments;
        int timeoutMs;
    };

    void startNext();
    void finish(GitError error, int exitCode = -1);

    QProcess _process;
    QTimer _timer;
    QQueue<Request> _queue;
    Request _current {};
    QByteArray _stdout;
    QByteArray _stderr;
    quint64 _nextId {1};
    bool _finishing {false};
    GitError _forcedError {GitError::None};
};

} // namespace Git

#endif // GITCOMMANDRUNNER_H
