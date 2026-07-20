#ifndef EXTERNALTOOLSERVICE_H
#define EXTERNALTOOLSERVICE_H

#include <QString>

namespace Git {

class ExternalToolService {
public:
    static QString diffCommand();
    static QString mergeCommand();
    static void setDiffCommand(const QString& command);
    static void setMergeCommand(const QString& command);
    static bool launchDiff(const QString& repository, const QString& file,
                           QString* error = nullptr);
    static bool launchMerge(const QString& base, const QString& local,
                            const QString& remote, const QString& merged,
                            QString* error = nullptr);
};

} // namespace Git

#endif // EXTERNALTOOLSERVICE_H
