#ifndef GITDIAGNOSTICSERVICE_H
#define GITDIAGNOSTICSERVICE_H

#include "GitTypes.h"

namespace Git {
class GitDiagnosticService {
public:
    explicit GitDiagnosticService(QString repositoryPath);
    GitDiagnosticReport inspect(const QVector<RemoteInfo>& remotes) const;
    QString testRemote(const QString& remoteUrl, QString* error = nullptr) const;

    static QString redact(const QString& value);
    static QString classifyRemote(const QString& url);
private:
    QString config(const QString& key) const;
    QString _repositoryPath;
};
} // namespace Git
#endif // GITDIAGNOSTICSERVICE_H
