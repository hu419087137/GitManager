#include "BranchParser.h"
#include <QRegularExpression>

namespace Git {

QVector<Branch> BranchParser::parse(const QByteArray& output, const QString& currentHead)
{
    QVector<Branch> branches;
    for (QByteArray record : output.split('\0')) {
        while (record.startsWith('\n') || record.startsWith('\r'))
            record.remove(0, 1);
        if (record.isEmpty())
            continue;
        const QList<QByteArray> fields = record.split('\t');
        if (fields.size() < 3)
            continue;

        Branch branch;
        branch.fullName = QString::fromUtf8(fields[0]);
        branch.name = QString::fromUtf8(fields[1]);
        branch.hash = QString::fromUtf8(fields[2]);
        if (fields.size() > 3) branch.upstream = QString::fromUtf8(fields[3]);
        if (fields.size() > 4) {
            const QString track = QString::fromUtf8(fields[4]);
            const QRegularExpression aheadRe(QStringLiteral("ahead (\\d+)"));
            const QRegularExpression behindRe(QStringLiteral("behind (\\d+)"));
            const auto aheadMatch = aheadRe.match(track);
            const auto behindMatch = behindRe.match(track);
            if (aheadMatch.hasMatch()) branch.ahead = aheadMatch.captured(1).toInt();
            if (behindMatch.hasMatch()) branch.behind = behindMatch.captured(1).toInt();
        }
        branch.isRemote = branch.fullName.startsWith(QStringLiteral("refs/remotes/"));
        branch.isCurrent = !branch.isRemote && branch.name == currentHead;
        branches.append(branch);
    }
    return branches;
}

} // namespace Git
