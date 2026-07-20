#ifndef HOSTINGAPISERVICE_H
#define HOSTINGAPISERVICE_H

#include "GitTypes.h"
#include <QUrl>

namespace Git {

class HostingApiService {
public:
    QVector<HostingChangeInfo> changes(const HostingRemoteInfo& remote,
                                       const QString& token,
                                       QString* error = nullptr) const;
    QVector<HostingIssueInfo> issues(const HostingRemoteInfo& remote,
                                     const QString& token,
                                     QString* error = nullptr) const;
    QVector<HostingReviewFile> reviewFiles(
        const HostingRemoteInfo& remote, const HostingChangeInfo& change,
        const QString& token, QString* error = nullptr) const;
    bool postReviewComment(const HostingRemoteInfo& remote,
                           const HostingChangeInfo& change,
                           const HostingReviewFile& file, int line,
                           const QString& body, const QString& token,
                           QString* error = nullptr) const;

    static QUrl changesEndpoint(const HostingRemoteInfo& remote);
    static QUrl issuesEndpoint(const HostingRemoteInfo& remote);
    static QUrl reviewFilesEndpoint(const HostingRemoteInfo& remote,
                                    const QString& changeId);
    static QUrl reviewCommentEndpoint(const HostingRemoteInfo& remote,
                                      const QString& changeId);
    static QByteArray reviewCommentBody(HostingProvider provider,
                                        const HostingChangeInfo& change,
                                        const HostingReviewFile& file,
                                        int line, const QString& body,
                                        QString* error = nullptr);
    static QVector<HostingChangeInfo> parseChanges(
        HostingProvider provider, const QByteArray& json,
        QString* error = nullptr);
    static QVector<HostingIssueInfo> parseIssues(
        HostingProvider provider, const QByteArray& json,
        QString* error = nullptr);
    static QVector<HostingReviewFile> parseReviewFiles(
        HostingProvider provider, const QByteArray& json,
        QString* error = nullptr);
};

} // namespace Git

#endif // HOSTINGAPISERVICE_H
