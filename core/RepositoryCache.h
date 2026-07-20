#ifndef REPOSITORYCACHE_H
#define REPOSITORYCACHE_H

#include "RepositoryState.h"
#include <QString>

namespace Git {

class RepositoryCache {
public:
    void clear();

    bool tryGetState(const QString& repositoryPath,
                     qint64 nowMs,
                     RepositoryState* state) const;
    void storeState(const QString& repositoryPath,
                    const RepositoryState& state,
                    qint64 nowMs);

private:
    QString _repositoryPath;
    RepositoryState _state;
    qint64 _stateTimestampMs {-1};
};

} // namespace Git

#endif // REPOSITORYCACHE_H
