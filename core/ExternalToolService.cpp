#include "ExternalToolService.h"
#include <QProcess>
#include <QSettings>

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

bool launch(const QString& templateText, const QHash<QString, QString>& values,
            QString* error)
{
    if (templateText.isEmpty()) {
        if (error) *error = QStringLiteral("No external tool is configured.");
        return false;
    }
    QString commandLine = templateText;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        commandLine.replace(QStringLiteral("{%1}").arg(it.key()), it.value());
    const QStringList parts = QProcess::splitCommand(commandLine);
    if (parts.isEmpty()) {
        if (error) *error = QStringLiteral("The external tool command is empty.");
        return false;
    }
    if (!QProcess::startDetached(parts.first(), parts.mid(1))) {
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
bool ExternalToolService::launchDiff(const QString& repository, const QString& file,
                                     QString* error)
{
    return launch(diffCommand(), {{QStringLiteral("repo"), repository},
                                  {QStringLiteral("file"), file}}, error);
}
bool ExternalToolService::launchMerge(const QString& base, const QString& local,
                                      const QString& remote, const QString& merged,
                                      QString* error)
{
    return launch(mergeCommand(), {{QStringLiteral("base"), base},
                                   {QStringLiteral("local"), local},
                                   {QStringLiteral("remote"), remote},
                                   {QStringLiteral("merged"), merged}}, error);
}
} // namespace Git
