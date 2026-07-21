#include "ExternalToolService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace Git {
namespace {
QSettings settings()
{
    return QSettings(QStringLiteral("GitManager"), QStringLiteral("GitManager"));
}
QString command(const char* key)
{
    return settings().value(QStringLiteral("externalTools/%1")
                                 .arg(QString::fromLatin1(key))).toString().trimmed();
}

QString quoted(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool writeFile(const QString& path, const QByteArray& contents, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString temporaryDirectory(QString* error)
{
    const QString path = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(
            QStringLiteral("GitManager/external/%1").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(path)) {
        if (error) *error = QStringLiteral("Cannot create external tool temporary directory.");
        return {};
    }
    return path;
}

bool launch(const QString& templateText, const QHash<QString, QString>& values,
            const QString& workingDirectory, QString* error)
{
    if (templateText.isEmpty()) {
        if (error) *error = QStringLiteral("No external tool is configured.");
        return false;
    }
    QString commandLine = templateText;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        commandLine.replace(QStringLiteral("{%1}").arg(it.key()), quoted(it.value()));
    const QStringList parts = QProcess::splitCommand(commandLine);
    if (parts.isEmpty()) {
        if (error) *error = QStringLiteral("The external tool command is empty.");
        return false;
    }
    if (!QProcess::startDetached(parts.first(), parts.mid(1), workingDirectory)) {
        if (error) *error = QStringLiteral("Cannot start external tool: %1").arg(parts.first());
        return false;
    }
    return true;
}
} // namespace

QString ExternalToolService::diffCommand() { return command("diffCommand"); }
QString ExternalToolService::mergeCommand() { return command("mergeCommand"); }
void ExternalToolService::setDiffCommand(const QString& value)
{
    QSettings store = settings();
    store.setValue(QStringLiteral("externalTools/diffCommand"), value.trimmed());
    store.sync();
}
void ExternalToolService::setMergeCommand(const QString& value)
{
    QSettings store = settings();
    store.setValue(QStringLiteral("externalTools/mergeCommand"), value.trimmed());
    store.sync();
}
bool ExternalToolService::launchDiff(const QString& repository,
                                     const ExternalDiffInput& input,
                                     QString* error)
{
    const QString directory = temporaryDirectory(error);
    if (directory.isEmpty()) return false;
    const QString name = QFileInfo(input.path).fileName().isEmpty()
        ? QStringLiteral("file") : QFileInfo(input.path).fileName();
    const QString left = QDir(directory).filePath(QStringLiteral("LEFT_%1").arg(name));
    const QString right = QDir(directory).filePath(QStringLiteral("RIGHT_%1").arg(name));
    if (!writeFile(left, input.left, error) || !writeFile(right, input.right, error))
        return false;
    return launch(diffCommand(), {{QStringLiteral("repo"), repository},
                                  {QStringLiteral("file"), right},
                                  {QStringLiteral("left"), left},
                                  {QStringLiteral("right"), right}},
                  repository, error);
}
bool ExternalToolService::launchMerge(const QString& repository,
                                      const ExternalMergeInput& input,
                                      QString* error)
{
    const QString directory = temporaryDirectory(error);
    if (directory.isEmpty()) return false;
    const QString name = QFileInfo(input.path).fileName().isEmpty()
        ? QStringLiteral("file") : QFileInfo(input.path).fileName();
    const QString base = QDir(directory).filePath(QStringLiteral("BASE_%1").arg(name));
    const QString local = QDir(directory).filePath(QStringLiteral("LOCAL_%1").arg(name));
    const QString remote = QDir(directory).filePath(QStringLiteral("REMOTE_%1").arg(name));
    if (!writeFile(base, input.base, error) || !writeFile(local, input.local, error)
        || !writeFile(remote, input.remote, error)) return false;
    const QString merged = QDir(repository).filePath(input.path);
    return launch(mergeCommand(), {{QStringLiteral("repo"), repository},
                                   {QStringLiteral("base"), base},
                                   {QStringLiteral("local"), local},
                                   {QStringLiteral("remote"), remote},
                                   {QStringLiteral("merged"), merged}},
                  repository, error);
}
} // namespace Git
