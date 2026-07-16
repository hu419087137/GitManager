#include "GitCommandRunner.h"
#include <QRegularExpression>

namespace Git {

GitCommandRunner::GitCommandRunner(QObject* parent)
    : QObject(parent)
{
    _timer.setSingleShot(true);

    connect(&_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray data = _process.readAllStandardOutput();
        _stdout += data;
        emit sigOutput(_current.id, data, false);
    });
    connect(&_process, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray data = _process.readAllStandardError();
        _stderr += data;
        emit sigOutput(_current.id, data, true);
    });
    connect(&_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(GitError::FailedToStart);
    });
    connect(&_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        if (_finishing)
            return;
        finish(_forcedError != GitError::None ? _forcedError
                   : (status == QProcess::NormalExit && exitCode == 0
                          ? GitError::None : GitError::NonZeroExit),
               exitCode);
    });
    connect(&_timer, &QTimer::timeout, this, [this] {
        _forcedError = GitError::TimedOut;
        _process.kill();
    });
}

quint64 GitCommandRunner::enqueue(const QString& operation, const QString& workingDirectory,
                                  const QStringList& arguments, bool networkOperation,
                                  int timeoutMs)
{
    const quint64 id = _nextId++;
    const int effectiveTimeout = timeoutMs >= 0 ? timeoutMs : (networkOperation ? 0 : 30000);
    _queue.enqueue({id, operation, workingDirectory, arguments, effectiveTimeout});
    QTimer::singleShot(0, this, &GitCommandRunner::startNext);
    return id;
}

void GitCommandRunner::cancelCurrent()
{
    if (!isBusy())
        return;
    _forcedError = GitError::Cancelled;
    _process.kill();
}

void GitCommandRunner::cancelAll()
{
    _queue.clear();
    cancelCurrent();
}

QString GitCommandRunner::formatCommand(const QStringList& arguments)
{
    QStringList safe;
    bool redactNext = false;
    for (const QString& arg : arguments) {
        QString value = arg;
        if (redactNext
            || value.contains(QStringLiteral("token"), Qt::CaseInsensitive)
            || value.contains(QStringLiteral("password"), Qt::CaseInsensitive))
            value = QStringLiteral("<redacted>");
        else
            value.replace(QRegularExpression(QStringLiteral("(://)[^/@]+@")),
                          QStringLiteral("\\1<redacted>@"));
        redactNext = arg.compare(QStringLiteral("--password"), Qt::CaseInsensitive) == 0
                     || arg.compare(QStringLiteral("--token"), Qt::CaseInsensitive) == 0;
        if (value.contains(' ') || value.contains('"')) {
            value.replace('"', QStringLiteral("\\\""));
            value = '"' + value + '"';
        }
        safe.append(value);
    }
    return QStringLiteral("git %1").arg(safe.join(' '));
}

void GitCommandRunner::startNext()
{
    if (isBusy() || _queue.isEmpty())
        return;

    _current = _queue.dequeue();
    _stdout.clear();
    _stderr.clear();
    _finishing = false;
    _forcedError = GitError::None;
    _process.setWorkingDirectory(_current.workingDirectory);
    emit sigBusyChanged(true);
    emit sigStarted(_current.id, _current.operation, formatCommand(_current.arguments));
    _process.start(QStringLiteral("git"), _current.arguments);
    if (_current.timeoutMs > 0)
        _timer.start(_current.timeoutMs);
}

void GitCommandRunner::finish(GitError error, int exitCode)
{
    if (_finishing || _current.id == 0)
        return;
    _finishing = true;
    _timer.stop();
    const QByteArray stdoutRemainder = _process.readAllStandardOutput();
    const QByteArray stderrRemainder = _process.readAllStandardError();
    _stdout += stdoutRemainder;
    _stderr += stderrRemainder;
    if (!stdoutRemainder.isEmpty()) emit sigOutput(_current.id, stdoutRemainder, false);
    if (!stderrRemainder.isEmpty()) emit sigOutput(_current.id, stderrRemainder, true);

    GitResult result;
    result.requestId = _current.id;
    result.operation = _current.operation;
    result.workingDirectory = _current.workingDirectory;
    result.arguments = _current.arguments;
    result.standardOutput = _stdout;
    result.standardError = _stderr;
    result.exitCode = exitCode;
    result.error = error;

    _current = {};
    emit sigFinished(result);
    const bool more = !_queue.isEmpty();
    if (!more)
        emit sigBusyChanged(false);
    _finishing = false;
    startNext();
}

} // namespace Git
