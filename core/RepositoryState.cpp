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

QString RepositoryState::activeOperationText() const
{
    switch (activeOperation) {
    case RepositoryOperation::None:       return {};
    case RepositoryOperation::Merge:      return QStringLiteral("merge");
    case RepositoryOperation::Rebase:     return QStringLiteral("rebase");
    case RepositoryOperation::CherryPick: return QStringLiteral("cherry-pick");
    case RepositoryOperation::Revert:     return QStringLiteral("revert");
    case RepositoryOperation::Unknown:    return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

} // namespace Git
