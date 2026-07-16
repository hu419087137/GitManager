#ifndef DIFFPARSER_H
#define DIFFPARSER_H

#include <QString>

namespace Git {

class DiffParser {
public:
    static QString hunkAtLine(const QString& diff, int lineNumber);
};

} // namespace Git
#endif
