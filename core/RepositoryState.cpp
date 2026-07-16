#include "RepositoryState.h"

namespace Git {

QString RepositoryState::displayHead() const
{
    if (unborn)
        return headName.isEmpty() ? QStringLiteral("unborn") : headName + QStringLiteral(" (unborn)");
    if (detached)
        return headHash.isEmpty() ? QStringLiteral("detached HEAD")
                                  : QStringLiteral("detached at %1").arg(headHash.left(8));
    return headName;
}

QString RepositoryState::syncText() const
{
    if (upstream.isEmpty())
        return {};
    if (ahead == 0 && behind == 0)
        return QStringLiteral("与 %1 同步").arg(upstream);
    return QStringLiteral("↑%1 ↓%2").arg(ahead).arg(behind);
}

} // namespace Git
