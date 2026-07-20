#include "LfsService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <utility>

namespace Git {

LfsService::LfsService(QString repositoryPath)
    : _repositoryPath(QDir::cleanPath(std::move(repositoryPath)))
{
}

bool LfsService::run(const QStringList& arguments, QByteArray* output,
                     QString* error) const
{
    QProcess process;
    process.setWorkingDirectory(_repositoryPath);
    process.setProgram(QStringLiteral("git"));
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("Cannot start Git. Ensure Git is installed and available in PATH.");
        return false;
    }
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        if (error)
            *error = QStringLiteral("Git LFS command timed out.");
        return false;
    }
    const QByteArray standardOutput = process.readAllStandardOutput();
    const QByteArray standardError = process.readAllStandardError();
    if (output)
        *output = standardOutput;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString detail = QString::fromUtf8(standardError).trimmed();
            *error = detail.isEmpty()
                ? QStringLiteral("Git LFS command failed.") : detail;
        }
        return false;
    }
    return true;
}

QStringList LfsService::parseAttributes(const QByteArray& content)
{
    QStringList patterns;
    const QString text = QString::fromUtf8(content);
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        int tokenEnd = -1;
        if (trimmed.startsWith(QLatin1Char('"'))) {
            bool escaped = false;
            for (int index = 1; index < trimmed.size(); ++index) {
                const QChar character = trimmed.at(index);
                if (character == QLatin1Char('"') && !escaped) {
                    tokenEnd = index + 1;
                    break;
                }
                escaped = character == QLatin1Char('\\') && !escaped;
                if (character != QLatin1Char('\\'))
                    escaped = false;
            }
        } else {
            for (int index = 0; index < trimmed.size(); ++index) {
                if (trimmed.at(index).isSpace()) {
                    tokenEnd = index;
                    break;
                }
            }
        }
        if (tokenEnd <= 0)
            continue;
        const QString attributes = trimmed.mid(tokenEnd);
        if (!QRegularExpression(QStringLiteral(R"((?:^|\s)filter=lfs(?:\s|$))"))
                 .match(attributes).hasMatch())
            continue;
        QString pattern = trimmed.left(tokenEnd);
        if (pattern.startsWith(QLatin1Char('"'))) {
            const QJsonDocument decoded = QJsonDocument::fromJson(
                QByteArray("[") + pattern.toUtf8() + QByteArray("]"));
            if (decoded.isArray() && !decoded.array().isEmpty())
                pattern = decoded.array().first().toString();
        }
        if (!pattern.isEmpty() && !patterns.contains(pattern))
            patterns.append(pattern);
    }
    return patterns;
}

QVector<LfsLockInfo> LfsService::parseLocks(const QByteArray& content,
                                            QString* error)
{
    QVector<LfsLockInfo> result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(content, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Cannot parse Git LFS lock response.");
        return result;
    }
    const QJsonArray locks = document.object().value(QStringLiteral("locks")).toArray();
    for (const QJsonValue& value : locks) {
        const QJsonObject object = value.toObject();
        LfsLockInfo lock;
        lock.id = object.value(QStringLiteral("id")).toString();
        lock.path = object.value(QStringLiteral("path")).toString();
        lock.lockedAt = object.value(QStringLiteral("locked_at")).toString();
        const QJsonObject owner = object.value(QStringLiteral("owner")).toObject();
        lock.owner = owner.value(QStringLiteral("name")).toString();
        lock.ownedByCurrentUser = object.value(QStringLiteral("ours")).toBool();
        if (!lock.path.isEmpty())
            result.append(lock);
    }
    return result;
}

LfsState LfsService::state(bool includeLocks, QString* error) const
{
    LfsState result;
    QByteArray version;
    QString commandError;
    if (!run({QStringLiteral("lfs"), QStringLiteral("version")},
             &version, &commandError)) {
        if (error)
            *error = commandError;
        return result;
    }
    result.installed = true;
    result.version = QString::fromUtf8(version).trimmed();

    QFile attributes(QDir(_repositoryPath).filePath(QStringLiteral(".gitattributes")));
    if (attributes.open(QIODevice::ReadOnly))
        result.trackedPatterns = parseAttributes(attributes.readAll());

    if (includeLocks) {
        QByteArray locks;
        if (run({QStringLiteral("lfs"), QStringLiteral("locks"),
                 QStringLiteral("--json")}, &locks, &commandError)) {
            result.locks = parseLocks(locks, &result.locksError);
        } else {
            result.locksError = commandError;
        }
    }
    return result;
}

bool LfsService::track(const QString& pattern, QString* error) const
{
    if (pattern.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("LFS tracking pattern is required.");
        return false;
    }
    return run({QStringLiteral("lfs"), QStringLiteral("track"),
                QStringLiteral("--"), pattern.trimmed()}, nullptr, error);
}

bool LfsService::untrack(const QString& pattern, QString* error) const
{
    if (pattern.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("LFS tracking pattern is required.");
        return false;
    }
    return run({QStringLiteral("lfs"), QStringLiteral("untrack"),
                QStringLiteral("--"), pattern.trimmed()}, nullptr, error);
}

bool LfsService::lock(const QString& path, QString* error) const
{
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("LFS lock path is required.");
        return false;
    }
    return run({QStringLiteral("lfs"), QStringLiteral("lock"),
                QStringLiteral("--"), path.trimmed()}, nullptr, error);
}

bool LfsService::unlock(const QString& path, bool force, QString* error) const
{
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("LFS lock path is required.");
        return false;
    }
    QStringList arguments {QStringLiteral("lfs"), QStringLiteral("unlock")};
    if (force)
        arguments.append(QStringLiteral("--force"));
    arguments.append(QStringLiteral("--"));
    arguments.append(path.trimmed());
    return run(arguments, nullptr, error);
}

} // namespace Git
