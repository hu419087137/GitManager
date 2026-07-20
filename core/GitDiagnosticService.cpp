#include "GitDiagnosticService.h"

#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <utility>

namespace Git {
namespace {
QString runCommand(const QString& program, const QStringList& arguments,
                   const QString& workingDirectory, int timeout = 5000)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(program, arguments);
    if (!process.waitForStarted(timeout) || !process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

void append(QVector<DiagnosticItem>& items, const QString& category,
            const QString& name, QString value, bool warning = false)
{
    if (value.isEmpty()) value = QStringLiteral("Not configured");
    items.append({category, name, value, warning});
}
}

GitDiagnosticService::GitDiagnosticService(QString repositoryPath)
    : _repositoryPath(QDir::cleanPath(std::move(repositoryPath))) {}

QString GitDiagnosticService::config(const QString& key) const
{
    return runCommand(QStringLiteral("git"),
                      {QStringLiteral("config"), QStringLiteral("--get"), key},
                      _repositoryPath);
}

QString GitDiagnosticService::redact(const QString& value)
{
    QString result = value;
    result.replace(QRegularExpression(
        QStringLiteral(R"((https?://)[^/@\s:]+(?::[^@\s]*)?@)")),
        QStringLiteral("\\1***@"));
    result.replace(QRegularExpression(
        QStringLiteral(R"((?i)(token|password|passwd|secret)=([^&\s]+))")),
        QStringLiteral("\\1=***"));
    return result;
}

QString GitDiagnosticService::classifyRemote(const QString& url)
{
    if (url.startsWith(QStringLiteral("ssh://"), Qt::CaseInsensitive)
        || QRegularExpression(QStringLiteral(R"(^[^/@\s]+@[^:]+:.+$)"))
               .match(url).hasMatch())
        return QStringLiteral("SSH");
    if (url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return QStringLiteral("HTTPS");
    if (url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        return QStringLiteral("HTTP (insecure)");
    if (url.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)
        || QDir::isAbsolutePath(url))
        return QStringLiteral("Local");
    return QStringLiteral("Unknown");
}

GitDiagnosticReport GitDiagnosticService::inspect(
    const QVector<RemoteInfo>& remotes) const
{
    GitDiagnosticReport report;
    report.remotes = remotes;
    append(report.items, QStringLiteral("Tools"), QStringLiteral("Git"),
           runCommand(QStringLiteral("git"), {QStringLiteral("--version")},
                      _repositoryPath),
           QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty());
    append(report.items, QStringLiteral("Tools"), QStringLiteral("SSH executable"),
           QStandardPaths::findExecutable(QStringLiteral("ssh")),
           QStandardPaths::findExecutable(QStringLiteral("ssh")).isEmpty());
    append(report.items, QStringLiteral("SSH"), QStringLiteral("core.sshCommand"),
           redact(config(QStringLiteral("core.sshCommand"))));
    append(report.items, QStringLiteral("Credentials"), QStringLiteral("credential.helper"),
           redact(config(QStringLiteral("credential.helper"))));
    append(report.items, QStringLiteral("Proxy"), QStringLiteral("http.proxy"),
           redact(config(QStringLiteral("http.proxy"))));
    append(report.items, QStringLiteral("Proxy"), QStringLiteral("https.proxy"),
           redact(config(QStringLiteral("https.proxy"))));
    append(report.items, QStringLiteral("TLS"), QStringLiteral("http.sslVerify"),
           config(QStringLiteral("http.sslVerify")));
    append(report.items, QStringLiteral("TLS"), QStringLiteral("http.sslCAInfo"),
           redact(config(QStringLiteral("http.sslCAInfo"))));

    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const QString& key : {QStringLiteral("HTTP_PROXY"), QStringLiteral("HTTPS_PROXY"),
                               QStringLiteral("NO_PROXY"), QStringLiteral("GIT_SSH"),
                               QStringLiteral("GIT_SSH_COMMAND")}) {
        if (environment.contains(key))
            append(report.items, QStringLiteral("Environment"), key,
                   redact(environment.value(key)));
    }
    for (const RemoteInfo& remote : remotes) {
        const QString url = remote.fetchUrl.isEmpty() ? remote.pushUrl : remote.fetchUrl;
        append(report.items, QStringLiteral("Remote"), remote.name,
               QStringLiteral("%1 — %2").arg(classifyRemote(url), redact(url)),
               classifyRemote(url).contains(QStringLiteral("insecure")));
    }
    return report;
}

QString GitDiagnosticService::testRemote(const QString& remoteUrl,
                                         QString* error) const
{
    if (remoteUrl.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Remote URL is required.");
        return {};
    }
    QProcess process;
    process.setWorkingDirectory(_repositoryPath);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("ls-remote"), QStringLiteral("--heads"),
                   remoteUrl.trimmed()});
    if (!process.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Cannot start Git.");
        return {};
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        if (error) *error = QStringLiteral("Remote connection test timed out after 30 seconds.");
        return {};
    }
    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString standardError = redact(
        QString::fromUtf8(process.readAllStandardError()).trimmed());
    if (process.exitCode() != 0) {
        if (error) *error = standardError.isEmpty()
            ? QStringLiteral("Remote connection test failed.") : standardError;
        return {};
    }
    return output.isEmpty()
        ? QStringLiteral("Connection succeeded; no branch references returned.")
        : QStringLiteral("Connection succeeded; remote branch references are available.");
}

} // namespace Git
