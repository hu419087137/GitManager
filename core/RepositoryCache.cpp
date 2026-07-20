#include "RepositoryCache.h"

namespace {
constexpr qint64 kStateCacheWindowMs = 500;
}

namespace Git {

void RepositoryCache::clear()
{
    _repositoryPath.clear();
    _state = {};
    _stateTimestampMs = -1;
}

bool RepositoryCache::tryGetState(const QString& repositoryPath,
                                  qint64 nowMs,
                                  RepositoryState* state) const
{
    if (!state || _repositoryPath != repositoryPath || _stateTimestampMs < 0)
        return false;
    if (nowMs - _stateTimestampMs > kStateCacheWindowMs)
        return false;
    *state = _state;
    return true;
}

void RepositoryCache::storeState(const QString& repositoryPath,
                                 const RepositoryState& state,
                                 qint64 nowMs)
{
    _repositoryPath = repositoryPath;
    _state = state;
    _stateTimestampMs = nowMs;
}

} // namespace Git
