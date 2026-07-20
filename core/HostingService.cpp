#include "HostingService.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace Git {
namespace {

QString stripGitSuffix(QString path)
{
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive))
        path.chop(4);
    return path;
}

QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

struct ParsedRemote {
    QString host;
    QString path;
};

ParsedRemote parseRemote(const QString& source)
{
    const QString value = source.trimmed();
    QUrl url = QUrl::fromUserInput(value);
    if (url.isValid() && !url.host().isEmpty())
        return {url.host().toLower(), stripGitSuffix(url.path())};

    const QRegularExpression scp(
        QStringLiteral(R"(^(?:[^@]+@)?([^:]+):/?(.+)$)"));
    const auto match = scp.match(value);
    if (match.hasMatch())
        return {match.captured(1).toLower(),
                stripGitSuffix(QStringLiteral("/") + match.captured(2))};
    return {};
}

QString httpsUrl(const QString& host, const QString& path)
{
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}

} // namespace

HostingRemoteInfo HostingService::describe(const RemoteInfo& remote,
                                           const QString& headHash,
                                           const QString& headBranch)
{
    HostingRemoteInfo result;
    result.remoteName = remote.name;
    result.sourceUrl = remote.fetchUrl.isEmpty() ? remote.pushUrl : remote.fetchUrl;
    const ParsedRemote parsed = parseRemote(result.sourceUrl);
    if (parsed.host.isEmpty() || parsed.path.isEmpty())
        return result;

    QString repositoryPath = parsed.path;
    QString host = parsed.host;
    if (host == QStringLiteral("github.com")) {
        result.provider = HostingProvider::GitHub;
        result.webUrl = httpsUrl(host, repositoryPath);
        if (!headHash.isEmpty())
            result.commitUrl = result.webUrl + QStringLiteral("/commit/") + encoded(headHash);
        result.changesUrl = result.webUrl + QStringLiteral("/pulls");
        if (!headBranch.isEmpty())
            result.createChangeUrl = result.webUrl + QStringLiteral("/compare/")
                + encoded(headBranch) + QStringLiteral("?expand=1");
        result.issuesUrl = result.webUrl + QStringLiteral("/issues");
    } else if (host == QStringLiteral("gitlab.com")
               || host.contains(QStringLiteral("gitlab"))) {
        result.provider = HostingProvider::GitLab;
        result.webUrl = httpsUrl(host, repositoryPath);
        if (!headHash.isEmpty())
            result.commitUrl = result.webUrl + QStringLiteral("/-/commit/") + encoded(headHash);
        result.changesUrl = result.webUrl + QStringLiteral("/-/merge_requests");
        if (!headBranch.isEmpty()) {
            QUrl url(result.webUrl + QStringLiteral("/-/merge_requests/new"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("merge_request[source_branch]"), headBranch);
            url.setQuery(query);
            result.createChangeUrl = url.toString(QUrl::FullyEncoded);
        }
        result.issuesUrl = result.webUrl + QStringLiteral("/-/issues");
    } else if (host == QStringLiteral("ssh.dev.azure.com")
               || host == QStringLiteral("vs-ssh.visualstudio.com")) {
        const QStringList components = repositoryPath.split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
        if (components.size() == 4 && components.first() == QStringLiteral("v3")) {
            result.provider = HostingProvider::AzureDevOps;
            repositoryPath = QStringLiteral("/%1/%2/_git/%3")
                .arg(components.at(1), components.at(2), components.at(3));
            result.webUrl = httpsUrl(QStringLiteral("dev.azure.com"), repositoryPath);
        }
    } else if (host == QStringLiteral("dev.azure.com")
               || host.endsWith(QStringLiteral(".visualstudio.com"))) {
        result.provider = HostingProvider::AzureDevOps;
        result.webUrl = httpsUrl(host, repositoryPath);
    }

    if (result.provider == HostingProvider::AzureDevOps && !result.webUrl.isEmpty()) {
        if (!headHash.isEmpty())
            result.commitUrl = result.webUrl + QStringLiteral("/commit/") + encoded(headHash);
        if (!headBranch.isEmpty()) {
            QUrl url(result.webUrl + QStringLiteral("/pullrequestcreate"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("sourceRef"), headBranch);
            url.setQuery(query);
            result.createChangeUrl = url.toString(QUrl::FullyEncoded);
        }
        result.changesUrl = result.webUrl + QStringLiteral("/pullrequests");
        const int gitMarker = result.webUrl.indexOf(QStringLiteral("/_git/"));
        result.issuesUrl = (gitMarker >= 0
            ? result.webUrl.left(gitMarker) : result.webUrl)
            + QStringLiteral("/_workitems/recentlyupdated");
    }
    return result;
}

QString HostingService::providerName(HostingProvider provider)
{
    switch (provider) {
    case HostingProvider::GitHub: return QStringLiteral("GitHub");
    case HostingProvider::GitLab: return QStringLiteral("GitLab");
    case HostingProvider::AzureDevOps: return QStringLiteral("Azure DevOps");
    case HostingProvider::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

} // namespace Git
