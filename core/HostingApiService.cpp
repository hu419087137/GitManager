#include "HostingApiService.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace Git {
namespace {

QByteArray authorization(HostingProvider provider, const QString& token)
{
    const QByteArray secret = token.toUtf8();
    if (provider == HostingProvider::AzureDevOps)
        return QByteArray("Basic ") + (QByteArray(":") + secret).toBase64();
    return QByteArray("Bearer ") + secret;
}

QString jsonString(const QJsonObject& object, const char* name)
{
    return object.value(QString::fromLatin1(name)).toVariant().toString();
}

struct NetworkResponse {
    bool success {false};
    QByteArray body;
};

NetworkResponse sendRequest(const HostingRemoteInfo& remote,
                            const QString& token, const QUrl& endpoint,
                            const QByteArray& method = QByteArray("GET"),
                            const QByteArray& requestBody = {},
                            QString* error = nullptr)
{
    NetworkResponse result;
    if (!endpoint.isValid() || endpoint.isEmpty()) {
        if (error) *error = QStringLiteral("Cannot determine hosting API endpoint.");
        return result;
    }
    if (token.isEmpty()) {
        if (error) *error = QStringLiteral("A session access token is required.");
        return result;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(endpoint);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "GitManager/1.0");
    if (!requestBody.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
    if (remote.provider == HostingProvider::GitLab)
        request.setRawHeader("PRIVATE-TOKEN", token.toUtf8());
    else
        request.setRawHeader("Authorization", authorization(remote.provider, token));

    QNetworkReply* reply = method == QByteArray("GET")
        ? manager.get(request)
        : manager.sendCustomRequest(request, method, requestBody);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&deadline, &QTimer::timeout, reply, [&] {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    deadline.start(35000);
    loop.exec();
    deadline.stop();
    result.body = reply->readAll();
    const auto networkError = reply->error();
    const QString networkMessage = reply->errorString();
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    if (timedOut || networkError != QNetworkReply::NoError
        || status < 200 || status >= 300) {
        if (error) {
            if (timedOut) {
                *error = QStringLiteral("Hosting API request timed out after 35 seconds.");
                return result;
            }
            const QJsonDocument failure = QJsonDocument::fromJson(result.body);
            QString detail = failure.object().value(QStringLiteral("message")).toString();
            if (detail.isEmpty()) detail = networkMessage;
            if (detail.size() > 1000)
                detail = detail.left(1000) + QStringLiteral("...");
            *error = QStringLiteral("Hosting API request failed (%1): %2")
                         .arg(status).arg(detail);
        }
        return result;
    }
    result.success = true;
    return result;
}

} // namespace

QUrl HostingApiService::changesEndpoint(const HostingRemoteInfo& remote)
{
    const QUrl web(remote.webUrl);
    const QString path = web.path();
    if (remote.provider == HostingProvider::GitHub) {
        QUrl result(QStringLiteral("https://api.github.com/repos")
                    + path + QStringLiteral("/pulls"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), QStringLiteral("open"));
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
        result.setQuery(query);
        return result;
    }
    if (remote.provider == HostingProvider::GitLab) {
        const QString project = QString::fromLatin1(
            QUrl::toPercentEncoding(path.mid(1)));
        QUrl result;
        result.setScheme(web.scheme());
        result.setHost(web.host());
        result.setPath(QStringLiteral("/api/v4/projects/%1/merge_requests").arg(project),
                       QUrl::TolerantMode);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
        result.setQuery(query);
        return result;
    }
    if (remote.provider == HostingProvider::AzureDevOps) {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int gitIndex = parts.indexOf(QStringLiteral("_git"));
        if (gitIndex >= 2 && gitIndex + 1 < parts.size()) {
            QUrl result;
            result.setScheme(web.scheme());
            result.setHost(web.host());
            result.setPath(QStringLiteral("/%1/%2/_apis/git/repositories/%3/pullrequests")
                               .arg(parts.at(gitIndex - 2), parts.at(gitIndex - 1),
                                    parts.at(gitIndex + 1)));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("searchCriteria.status"),
                               QStringLiteral("active"));
            query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
            result.setQuery(query);
            return result;
        }
    }
    return {};
}

QUrl HostingApiService::issuesEndpoint(const HostingRemoteInfo& remote)
{
    const QUrl web(remote.webUrl);
    const QString path = web.path();
    if (remote.provider == HostingProvider::GitHub) {
        QUrl result(QStringLiteral("https://api.github.com/repos")
                    + path + QStringLiteral("/issues"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), QStringLiteral("open"));
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
        result.setQuery(query);
        return result;
    }
    if (remote.provider == HostingProvider::GitLab) {
        const QString project = QString::fromLatin1(
            QUrl::toPercentEncoding(path.mid(1)));
        QUrl result;
        result.setScheme(web.scheme());
        result.setHost(web.host());
        result.setPath(QStringLiteral("/api/v4/projects/%1/issues").arg(project),
                       QUrl::TolerantMode);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
        result.setQuery(query);
        return result;
    }
    if (remote.provider == HostingProvider::AzureDevOps) {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int gitIndex = parts.indexOf(QStringLiteral("_git"));
        if (gitIndex >= 2) {
            QUrl result;
            result.setScheme(web.scheme());
            result.setHost(web.host());
            result.setPath(QStringLiteral("/%1/%2/_apis/wit/wiql")
                               .arg(parts.at(gitIndex - 2), parts.at(gitIndex - 1)));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
            result.setQuery(query);
            return result;
        }
    }
    return {};
}

QUrl HostingApiService::reviewFilesEndpoint(const HostingRemoteInfo& remote,
                                            const QString& changeId)
{
    QUrl endpoint = changesEndpoint(remote);
    if (endpoint.isEmpty() || changeId.isEmpty()) return {};
    QString path = endpoint.path(QUrl::FullyEncoded);
    if (remote.provider == HostingProvider::GitHub)
        path += QStringLiteral("/%1/files").arg(changeId);
    else if (remote.provider == HostingProvider::GitLab)
        path += QStringLiteral("/%1/changes").arg(changeId);
    else if (remote.provider == HostingProvider::AzureDevOps) {
        path += QStringLiteral("/%1/iterations").arg(changeId);
    }
    endpoint.setPath(path, QUrl::TolerantMode);
    QUrlQuery query;
    if (remote.provider == HostingProvider::GitHub
        || remote.provider == HostingProvider::GitLab) {
        query.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
    } else {
        query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
    }
    endpoint.setQuery(query);
    return endpoint;
}

QUrl HostingApiService::reviewCommentEndpoint(const HostingRemoteInfo& remote,
                                              const QString& changeId)
{
    QUrl endpoint = changesEndpoint(remote);
    if (endpoint.isEmpty() || changeId.isEmpty()) return {};
    QString path = endpoint.path(QUrl::FullyEncoded);
    if (remote.provider == HostingProvider::GitHub)
        path += QStringLiteral("/%1/comments").arg(changeId);
    else if (remote.provider == HostingProvider::GitLab)
        path += QStringLiteral("/%1/discussions").arg(changeId);
    else
        path += QStringLiteral("/%1/threads").arg(changeId);
    endpoint.setPath(path, QUrl::TolerantMode);
    QUrlQuery query;
    if (remote.provider == HostingProvider::AzureDevOps)
        query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
    endpoint.setQuery(query);
    return endpoint;
}

QByteArray HostingApiService::reviewCommentBody(
    HostingProvider provider, const HostingChangeInfo& change,
    const HostingReviewFile& file, int line, const QString& body,
    QString* error)
{
    if (error) error->clear();
    if (file.path.isEmpty() || line <= 0 || body.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("File, target line, and comment are required.");
        return {};
    }
    QJsonObject object;
    if (provider == HostingProvider::GitHub) {
        const QString commitId = file.headSha.isEmpty() ? change.headSha : file.headSha;
        if (commitId.isEmpty()) {
            if (error) *error = QStringLiteral("GitHub review commit ID is missing.");
            return {};
        }
        object.insert(QStringLiteral("body"), body.trimmed());
        object.insert(QStringLiteral("commit_id"), commitId);
        object.insert(QStringLiteral("path"), file.path);
        object.insert(QStringLiteral("line"), line);
        object.insert(QStringLiteral("side"), QStringLiteral("RIGHT"));
    } else if (provider == HostingProvider::GitLab) {
        if (file.baseSha.isEmpty() || file.startSha.isEmpty() || file.headSha.isEmpty()) {
            if (error) *error = QStringLiteral("GitLab diff references are missing.");
            return {};
        }
        object.insert(QStringLiteral("body"), body.trimmed());
        object.insert(QStringLiteral("position"), QJsonObject{
            {QStringLiteral("position_type"), QStringLiteral("text")},
            {QStringLiteral("base_sha"), file.baseSha},
            {QStringLiteral("start_sha"), file.startSha},
            {QStringLiteral("head_sha"), file.headSha},
            {QStringLiteral("new_path"), file.path},
            {QStringLiteral("old_path"), file.previousPath.isEmpty()
                 ? file.path : file.previousPath},
            {QStringLiteral("new_line"), line}
        });
    } else if (provider == HostingProvider::AzureDevOps) {
        object.insert(QStringLiteral("comments"), QJsonArray{
            QJsonObject{{QStringLiteral("parentCommentId"), 0},
                        {QStringLiteral("content"), body.trimmed()},
                        {QStringLiteral("commentType"), 1}}
        });
        object.insert(QStringLiteral("status"), 1);
        object.insert(QStringLiteral("threadContext"), QJsonObject{
            {QStringLiteral("filePath"), file.path},
            {QStringLiteral("rightFileStart"), QJsonObject{
                 {QStringLiteral("line"), line}, {QStringLiteral("offset"), 1}}},
            {QStringLiteral("rightFileEnd"), QJsonObject{
                 {QStringLiteral("line"), line}, {QStringLiteral("offset"), 1}}}
        });
    } else {
        if (error) *error = QStringLiteral("Unsupported hosting provider.");
        return {};
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QVector<HostingChangeInfo> HostingApiService::parseChanges(
    HostingProvider provider, const QByteArray& json, QString* error)
{
    if (error) error->clear();
    QVector<HostingChangeInfo> result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Cannot parse hosting API response.");
        return result;
    }
    QJsonArray values;
    if (provider == HostingProvider::AzureDevOps && document.isObject())
        values = document.object().value(QStringLiteral("value")).toArray();
    else if (document.isArray())
        values = document.array();
    else {
        if (error) *error = QStringLiteral("Unexpected hosting API response.");
        return result;
    }

    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        HostingChangeInfo change;
        if (provider == HostingProvider::GitHub) {
            change.id = jsonString(object, "number");
            change.title = jsonString(object, "title");
            change.author = object.value(QStringLiteral("user"))
                                .toObject().value(QStringLiteral("login")).toString();
            change.state = jsonString(object, "state");
            change.webUrl = jsonString(object, "html_url");
            change.headSha = object.value(QStringLiteral("head"))
                                 .toObject().value(QStringLiteral("sha")).toString();
            change.draft = object.value(QStringLiteral("draft")).toBool();
        } else if (provider == HostingProvider::GitLab) {
            change.id = jsonString(object, "iid");
            change.title = jsonString(object, "title");
            change.author = object.value(QStringLiteral("author"))
                                .toObject().value(QStringLiteral("name")).toString();
            change.state = jsonString(object, "state");
            change.webUrl = jsonString(object, "web_url");
            change.headSha = object.value(QStringLiteral("diff_refs"))
                                 .toObject().value(QStringLiteral("head_sha")).toString();
            change.draft = object.value(QStringLiteral("draft")).toBool()
                || object.value(QStringLiteral("work_in_progress")).toBool();
        } else if (provider == HostingProvider::AzureDevOps) {
            change.id = jsonString(object, "pullRequestId");
            change.title = jsonString(object, "title");
            change.author = object.value(QStringLiteral("createdBy"))
                                .toObject().value(QStringLiteral("displayName")).toString();
            change.state = jsonString(object, "status");
            change.draft = object.value(QStringLiteral("isDraft")).toBool();
            change.headSha = object.value(QStringLiteral("lastMergeSourceCommit"))
                                 .toObject().value(QStringLiteral("commitId")).toString();
        }
        if (!change.id.isEmpty() && !change.title.isEmpty())
            result.append(change);
    }
    return result;
}

QVector<HostingIssueInfo> HostingApiService::parseIssues(
    HostingProvider provider, const QByteArray& json, QString* error)
{
    if (error) error->clear();
    QVector<HostingIssueInfo> result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Cannot parse hosting issue response.");
        return result;
    }
    QJsonArray values;
    if (provider == HostingProvider::AzureDevOps && document.isObject())
        values = document.object().value(QStringLiteral("value")).toArray();
    else if (document.isArray())
        values = document.array();
    else {
        if (error) *error = QStringLiteral("Unexpected hosting issue response.");
        return result;
    }
    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        HostingIssueInfo issue;
        if (provider == HostingProvider::GitHub) {
            if (object.contains(QStringLiteral("pull_request")))
                continue;
            issue.id = jsonString(object, "number");
            issue.title = jsonString(object, "title");
            issue.author = object.value(QStringLiteral("user"))
                               .toObject().value(QStringLiteral("login")).toString();
            issue.state = jsonString(object, "state");
            issue.webUrl = jsonString(object, "html_url");
        } else if (provider == HostingProvider::GitLab) {
            issue.id = jsonString(object, "iid");
            issue.title = jsonString(object, "title");
            issue.author = object.value(QStringLiteral("author"))
                               .toObject().value(QStringLiteral("name")).toString();
            issue.state = jsonString(object, "state");
            issue.webUrl = jsonString(object, "web_url");
        } else if (provider == HostingProvider::AzureDevOps) {
            const QJsonObject fields = object.value(QStringLiteral("fields")).toObject();
            issue.id = jsonString(object, "id");
            issue.title = fields.value(QStringLiteral("System.Title")).toString();
            issue.state = fields.value(QStringLiteral("System.State")).toString();
            const QJsonValue assigned = fields.value(QStringLiteral("System.AssignedTo"));
            issue.author = assigned.isObject()
                ? assigned.toObject().value(QStringLiteral("displayName")).toString()
                : assigned.toString();
            issue.webUrl = object.value(QStringLiteral("_links")).toObject()
                               .value(QStringLiteral("html")).toObject()
                               .value(QStringLiteral("href")).toString();
        }
        if (!issue.id.isEmpty() && !issue.title.isEmpty())
            result.append(issue);
    }
    return result;
}

QVector<HostingReviewFile> HostingApiService::parseReviewFiles(
    HostingProvider provider, const QByteArray& json, QString* error)
{
    if (error) error->clear();
    QVector<HostingReviewFile> result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Cannot parse review file response.");
        return result;
    }
    QJsonArray values;
    if (provider == HostingProvider::GitHub && document.isArray())
        values = document.array();
    else if (provider == HostingProvider::GitLab && document.isObject())
        values = document.object().value(QStringLiteral("changes")).toArray();
    else if (provider == HostingProvider::AzureDevOps && document.isObject())
        values = document.object().value(QStringLiteral("changeEntries")).toArray();
    else {
        if (error) *error = QStringLiteral("Unexpected review file response.");
        return result;
    }
    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        HostingReviewFile file;
        if (provider == HostingProvider::GitHub) {
            file.path = jsonString(object, "filename");
            file.previousPath = jsonString(object, "previous_filename");
            file.status = jsonString(object, "status");
            file.patch = jsonString(object, "patch");
            file.webUrl = jsonString(object, "blob_url");
        } else if (provider == HostingProvider::GitLab) {
            file.path = jsonString(object, "new_path");
            file.previousPath = jsonString(object, "old_path");
            file.status = object.value(QStringLiteral("new_file")).toBool()
                ? QStringLiteral("added")
                : object.value(QStringLiteral("deleted_file")).toBool()
                    ? QStringLiteral("deleted")
                    : object.value(QStringLiteral("renamed_file")).toBool()
                        ? QStringLiteral("renamed") : QStringLiteral("modified");
            file.patch = jsonString(object, "diff");
            const QJsonObject refs = document.object().value(
                QStringLiteral("diff_refs")).toObject();
            file.baseSha = refs.value(QStringLiteral("base_sha")).toString();
            file.startSha = refs.value(QStringLiteral("start_sha")).toString();
            file.headSha = refs.value(QStringLiteral("head_sha")).toString();
        } else {
            const QJsonObject item = object.value(QStringLiteral("item")).toObject();
            file.path = item.value(QStringLiteral("path")).toString();
            file.status = jsonString(object, "changeType");
        }
        if (!file.path.isEmpty()) result.append(file);
    }
    return result;
}

QVector<HostingChangeInfo> HostingApiService::changes(
    const HostingRemoteInfo& remote, const QString& token, QString* error) const
{
    if (error) error->clear();
    const NetworkResponse response = sendRequest(
        remote, token, changesEndpoint(remote), QByteArray("GET"), {}, error);
    if (!response.success)
        return {};
    QVector<HostingChangeInfo> result = parseChanges(
        remote.provider, response.body, error);
    if (remote.provider == HostingProvider::AzureDevOps) {
        for (HostingChangeInfo& change : result)
            change.webUrl = remote.webUrl + QStringLiteral("/pullrequest/") + change.id;
    }
    return result;
}

QVector<HostingIssueInfo> HostingApiService::issues(
    const HostingRemoteInfo& remote, const QString& token, QString* error) const
{
    if (error) error->clear();
    if (remote.provider != HostingProvider::AzureDevOps) {
        const NetworkResponse response = sendRequest(
            remote, token, issuesEndpoint(remote), QByteArray("GET"), {}, error);
        return response.success
            ? parseIssues(remote.provider, response.body, error)
            : QVector<HostingIssueInfo>();
    }

    const QByteArray wiql = QJsonDocument(QJsonObject{
        {QStringLiteral("query"),
         QStringLiteral("Select [System.Id] From WorkItems "
                        "Where [System.TeamProject] = @project "
                        "And [System.State] <> 'Closed' "
                        "Order By [System.ChangedDate] Desc")}
    }).toJson(QJsonDocument::Compact);
    const NetworkResponse query = sendRequest(
        remote, token, issuesEndpoint(remote), QByteArray("POST"), wiql, error);
    if (!query.success)
        return {};
    const QJsonArray references = QJsonDocument::fromJson(query.body)
                                      .object().value(QStringLiteral("workItems")).toArray();
    QStringList ids;
    for (const QJsonValue& reference : references) {
        const QString id = reference.toObject().value(QStringLiteral("id"))
                               .toVariant().toString();
        if (!id.isEmpty()) ids.append(id);
        if (ids.size() >= 100) break;
    }
    if (ids.isEmpty())
        return {};

    QUrl details = issuesEndpoint(remote);
    QString detailsPath = details.path();
    detailsPath.chop(QStringLiteral("/wiql").size());
    detailsPath += QStringLiteral("/workitems");
    details.setPath(detailsPath);
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("ids"), ids.join(QLatin1Char(',')));
    parameters.addQueryItem(QStringLiteral("fields"),
        QStringLiteral("System.Id,System.Title,System.State,System.AssignedTo"));
    parameters.addQueryItem(QStringLiteral("$expand"), QStringLiteral("links"));
    parameters.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
    details.setQuery(parameters);
    const NetworkResponse response = sendRequest(
        remote, token, details, QByteArray("GET"), {}, error);
    return response.success
        ? parseIssues(remote.provider, response.body, error)
        : QVector<HostingIssueInfo>();
}

QVector<HostingReviewFile> HostingApiService::reviewFiles(
    const HostingRemoteInfo& remote, const HostingChangeInfo& change,
    const QString& token, QString* error) const
{
    if (error) error->clear();
    const QUrl endpoint = reviewFilesEndpoint(remote, change.id);
    if (remote.provider != HostingProvider::AzureDevOps) {
        const NetworkResponse response = sendRequest(
            remote, token, endpoint, QByteArray("GET"), {}, error);
        return response.success
            ? parseReviewFiles(remote.provider, response.body, error)
            : QVector<HostingReviewFile>();
    }

    const NetworkResponse iterations = sendRequest(
        remote, token, endpoint, QByteArray("GET"), {}, error);
    if (!iterations.success) return {};
    const QJsonArray values = QJsonDocument::fromJson(iterations.body)
                                  .object().value(QStringLiteral("value")).toArray();
    int latest = -1;
    for (const QJsonValue& value : values)
        latest = qMax(latest, value.toObject().value(QStringLiteral("id")).toInt(-1));
    if (latest < 0) {
        if (error) *error = QStringLiteral("Azure pull request has no iterations.");
        return {};
    }
    QUrl files = endpoint;
    files.setPath(endpoint.path() + QStringLiteral("/%1/changes").arg(latest));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("2000"));
    query.addQueryItem(QStringLiteral("api-version"), QStringLiteral("7.1"));
    files.setQuery(query);
    const NetworkResponse response = sendRequest(
        remote, token, files, QByteArray("GET"), {}, error);
    QVector<HostingReviewFile> result = response.success
        ? parseReviewFiles(remote.provider, response.body, error)
        : QVector<HostingReviewFile>();
    for (HostingReviewFile& file : result)
        file.webUrl = change.webUrl;
    return result;
}

bool HostingApiService::postReviewComment(
    const HostingRemoteInfo& remote, const HostingChangeInfo& change,
    const HostingReviewFile& file, int line, const QString& body,
    const QString& token, QString* error) const
{
    if (error) error->clear();
    const QByteArray requestBody = reviewCommentBody(
        remote.provider, change, file, line, body, error);
    if (requestBody.isEmpty()) return false;
    return sendRequest(remote, token,
                       reviewCommentEndpoint(remote, change.id),
                       QByteArray("POST"), requestBody, error).success;
}

} // namespace Git
