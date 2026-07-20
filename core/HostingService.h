#ifndef HOSTINGSERVICE_H
#define HOSTINGSERVICE_H

#include "GitTypes.h"

namespace Git {

class HostingService {
public:
    static HostingRemoteInfo describe(const RemoteInfo& remote,
                                      const QString& headHash,
                                      const QString& headBranch);
    static QString providerName(HostingProvider provider);
};

} // namespace Git

#endif // HOSTINGSERVICE_H
