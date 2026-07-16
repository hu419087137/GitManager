#ifndef REPOSITORYSTATE_H
#define REPOSITORYSTATE_H

#include "GitTypes.h"
#include <QMetaType>

namespace Git {

struct RepositoryState {
    QString rootPath;
    QString headName;
    QString headHash;
    QString upstream;
    int ahead {0};
    int behind {0};
    bool detached {false};
    bool unborn {false};
    QString refsVersion;
    QVector<Branch> branches;
    QVector<File> files;

    QString displayHead() const;
    QString syncText() const;
};

} // namespace Git

Q_DECLARE_METATYPE(Git::RepositoryState);

#endif // REPOSITORYSTATE_H
