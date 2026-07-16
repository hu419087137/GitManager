#ifndef GITRESULT_H
#define GITRESULT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QMetaType>

namespace Git {

enum class GitError {
    None,
    FailedToStart,
    TimedOut,
    Cancelled,
    NonZeroExit
};

struct GitResult {
    quint64 requestId {0};
    QString operation;
    QString workingDirectory;
    QStringList arguments;
    QByteArray standardOutput;
    QByteArray standardError;
    int exitCode {-1};
    GitError error {GitError::None};

    bool success() const { return error == GitError::None; }
    bool cancelled() const { return error == GitError::Cancelled; }
};

} // namespace Git

Q_DECLARE_METATYPE(Git::GitResult);

#endif // GITRESULT_H
