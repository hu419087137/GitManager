#ifndef LFSSERVICE_H
#define LFSSERVICE_H

#include "GitTypes.h"
#include <QString>

namespace Git {

class LfsService {
public:
    explicit LfsService(QString repositoryPath);

    LfsState state(bool includeLocks, QString* error = nullptr) const;
    bool track(const QString& pattern, QString* error = nullptr) const;
    bool untrack(const QString& pattern, QString* error = nullptr) const;
    bool lock(const QString& path, QString* error = nullptr) const;
    bool unlock(const QString& path, bool force, QString* error = nullptr) const;

    static QStringList parseAttributes(const QByteArray& content);
    static QVector<LfsLockInfo> parseLocks(const QByteArray& content,
                                           QString* error = nullptr);

private:
    bool run(const QStringList& arguments, QByteArray* output,
             QString* error) const;
    QString _repositoryPath;
};

} // namespace Git

#endif // LFSSERVICE_H
