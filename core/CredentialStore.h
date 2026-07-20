#ifndef CREDENTIALSTORE_H
#define CREDENTIALSTORE_H

#include "GitTypes.h"

namespace Git {
class CredentialStore {
public:
    static QString key(HostingProvider provider);
    static bool read(HostingProvider provider, QString* token,
                     QString* error = nullptr);
    static bool write(HostingProvider provider, const QString& token,
                      QString* error = nullptr);
    static bool remove(HostingProvider provider, QString* error = nullptr);
};
} // namespace Git

#endif // CREDENTIALSTORE_H
