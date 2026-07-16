#ifndef BRANCHPARSER_H
#define BRANCHPARSER_H

#include "../GitTypes.h"
#include <QByteArray>

namespace Git {

class BranchParser {
public:
    static QVector<Branch> parse(const QByteArray& output, const QString& currentHead);
};

} // namespace Git

#endif // BRANCHPARSER_H
