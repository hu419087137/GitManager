#include "HookService.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

namespace Git {
HookService::HookService(QString repositoryPath)
    : _repositoryPath(QDir::cleanPath(std::move(repositoryPath))) {}

QString HookService::hooksPath(QString* error) const
{
    QProcess process;
    process.setWorkingDirectory(_repositoryPath);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
                   QStringLiteral("--git-path"), QStringLiteral("hooks")});
    if (!process.waitForStarted(5000) || !process.waitForFinished(5000)
        || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("Cannot resolve the Git hooks directory.");
        return {};
    }
    return QDir::cleanPath(QString::fromUtf8(
        process.readAllStandardOutput()).trimmed());
}

QVector<HookInfo> HookService::hooks(QString* error) const
{
    QVector<HookInfo> result;
    const QString path = hooksPath(error);
    if (path.isEmpty()) return result;
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) {
        if (entry.fileName().endsWith(QStringLiteral(".sample"), Qt::CaseInsensitive))
            continue;
        HookInfo hook;
        hook.name = entry.fileName();
        hook.path = entry.absoluteFilePath();
#ifdef Q_OS_WIN
        hook.executable = true;
#else
        hook.executable = entry.isExecutable();
#endif
        result.append(hook);
    }
    return result;
}

HookResult HookService::run(const QString& name) const
{
    HookResult result;
    result.name = name;
    if (!QRegularExpression(QStringLiteral(R"(^[a-z0-9-]+$)"))
             .match(name).hasMatch()) {
        result.success = false;
        result.exitCode = -1;
        result.output = QStringLiteral("Invalid hook name.");
        return result;
    }
    QString pathError;
    const QVector<HookInfo> available = hooks(&pathError);
    const auto it = std::find_if(available.cbegin(), available.cend(),
        [&name](const HookInfo& hook) { return hook.name == name; });
    if (it == available.cend()) return result;
    if (!it->executable) {
        result.success = false;
        result.exitCode = -1;
        result.output = QStringLiteral("Hook exists but is not executable: %1").arg(it->path);
        return result;
    }
    QProcess process;
    process.setWorkingDirectory(_repositoryPath);
    process.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
    if (shell.isEmpty()) {
        const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
        const QDir gitDir = QFileInfo(git).dir();
        const QString candidate = QDir::cleanPath(
            gitDir.filePath(QStringLiteral("../bin/sh.exe")));
        if (QFileInfo::exists(candidate)) shell = candidate;
    }
    if (shell.isEmpty()) {
        result.success = false;
        result.exitCode = -1;
        result.output = QStringLiteral("Cannot find the shell bundled with Git for Windows.");
        return result;
    }
    process.start(shell, {it->path});
#else
    process.start(it->path, {});
#endif
    if (!process.waitForStarted(5000)) {
        result.success = false;
        result.exitCode = -1;
        result.output = QStringLiteral("Cannot start Git hook.");
        return result;
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        result.success = false;
        result.timedOut = true;
        result.exitCode = -1;
        result.output = QStringLiteral("Hook timed out after 60 seconds.");
        return result;
    }
    result.exitCode = process.exitCode();
    result.success = process.exitStatus() == QProcess::NormalExit
        && result.exitCode == 0;
    result.output = QString::fromUtf8(process.readAll()).trimmed();
    return result;
}
} // namespace Git
