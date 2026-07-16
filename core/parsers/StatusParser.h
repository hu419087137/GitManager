#ifndef STATUSPARSER_H
#define STATUSPARSER_H

#include "../GitTypes.h"
#include <QByteArray>

namespace Git {

class StatusParser {
public:
    static StatusSummary parse(const QByteArray& output);
};

} // namespace Git

#endif // STATUSPARSER_H
