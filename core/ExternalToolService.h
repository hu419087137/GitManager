#ifndef EXTERNALTOOLSERVICE_H
#define EXTERNALTOOLSERVICE_H

#include "GitTypes.h"
#include <QString>

namespace Git {

class ExternalToolService {
public:
    static QString diffCommand();
    static QString mergeCommand();
    static void setDiffCommand(const QString& command);
    static void setMergeCommand(const QString& command);
    static bool launchDiff(const QString& repository,
                           const ExternalDiffInput& input,
                           QString* error = nullptr);
    static bool launchMerge(const QString& repository,
                            const ExternalMergeInput& input,
                            QString* error = nullptr);
};

} // namespace Git

#endif // EXTERNALTOOLSERVICE_H
