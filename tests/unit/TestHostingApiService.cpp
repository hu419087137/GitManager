#include "core/HostingApiService.h"
#include <QJsonDocument>
#include <QtTest>

class TestHostingApiService : public QObject {
    Q_OBJECT
private slots:
    void githubEndpointAndParser()
    {
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::GitHub;
        remote.webUrl = QStringLiteral("https://github.com/org/repo");
        const QUrl endpoint = Git::HostingApiService::changesEndpoint(remote);
        QCOMPARE(endpoint.toString(),
                 QStringLiteral("https://api.github.com/repos/org/repo/pulls?state=open&per_page=100"));
        QString error;
        const auto changes = Git::HostingApiService::parseChanges(
            remote.provider,
            R"([{"number":7,"title":"Improve docs","state":"open","draft":false,"html_url":"https://github.com/org/repo/pull/7","user":{"login":"alice"}}])",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().id, QStringLiteral("7"));
        QCOMPARE(changes.first().author, QStringLiteral("alice"));
    }

    void gitlabParserSupportsDraft()
    {
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::GitLab;
        remote.webUrl = QStringLiteral("https://gitlab.com/group/project");
        QVERIFY(Git::HostingApiService::changesEndpoint(remote)
                    .toEncoded().contains("projects/group%2Fproject/merge_requests"));
        QString error;
        const auto changes = Git::HostingApiService::parseChanges(
            Git::HostingProvider::GitLab,
            R"([{"iid":3,"title":"Draft change","state":"opened","draft":true,"web_url":"https://gitlab.com/o/r/-/merge_requests/3","author":{"name":"Bob"}}])",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changes.first().id, QStringLiteral("3"));
        QVERIFY(changes.first().draft);
    }

    void azureEndpointAndParser()
    {
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::AzureDevOps;
        remote.webUrl = QStringLiteral("https://dev.azure.com/org/team/_git/repo");
        QVERIFY(Git::HostingApiService::changesEndpoint(remote).toString()
                    .contains(QStringLiteral("/_apis/git/repositories/repo/pullrequests")));
        QString error;
        const auto changes = Git::HostingApiService::parseChanges(
            remote.provider,
            R"({"value":[{"pullRequestId":12,"title":"Review","status":"active","isDraft":true,"createdBy":{"displayName":"Carol"}}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changes.first().id, QStringLiteral("12"));
        QCOMPARE(changes.first().author, QStringLiteral("Carol"));
        QVERIFY(changes.first().draft);
    }

    void parsesIssuesAcrossProviders()
    {
        QString error;
        auto issues = Git::HostingApiService::parseIssues(
            Git::HostingProvider::GitHub,
            R"([{"number":4,"title":"Bug","state":"open","html_url":"https://github.com/o/r/issues/4","user":{"login":"alice"}},{"number":5,"title":"PR","pull_request":{},"user":{"login":"bob"}}])",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().id, QStringLiteral("4"));

        issues = Git::HostingApiService::parseIssues(
            Git::HostingProvider::GitLab,
            R"([{"iid":8,"title":"Regression","state":"opened","web_url":"https://gitlab.com/o/r/-/issues/8","author":{"name":"Bob"}}])",
            &error);
        QCOMPARE(issues.first().author, QStringLiteral("Bob"));

        issues = Git::HostingApiService::parseIssues(
            Git::HostingProvider::AzureDevOps,
            R"({"value":[{"id":21,"fields":{"System.Title":"Work item","System.State":"Active","System.AssignedTo":{"displayName":"Carol"}},"_links":{"html":{"href":"https://dev.azure.com/o/p/_workitems/edit/21"}}}]})",
            &error);
        QCOMPARE(issues.first().id, QStringLiteral("21"));
        QCOMPARE(issues.first().author, QStringLiteral("Carol"));
    }

    void createsIssueEndpoints()
    {
        Git::HostingRemoteInfo github;
        github.provider = Git::HostingProvider::GitHub;
        github.webUrl = QStringLiteral("https://github.com/o/r");
        QVERIFY(Git::HostingApiService::issuesEndpoint(github).toString()
                    .contains(QStringLiteral("/repos/o/r/issues")));
        Git::HostingRemoteInfo azure;
        azure.provider = Git::HostingProvider::AzureDevOps;
        azure.webUrl = QStringLiteral("https://dev.azure.com/o/p/_git/r");
        QVERIFY(Git::HostingApiService::issuesEndpoint(azure).toString()
                    .contains(QStringLiteral("/_apis/wit/wiql")));
    }

    void parsesReviewFilesAcrossProviders()
    {
        Git::HostingRemoteInfo github;
        github.provider = Git::HostingProvider::GitHub;
        github.webUrl = QStringLiteral("https://github.com/o/r");
        QVERIFY(Git::HostingApiService::reviewFilesEndpoint(github, QStringLiteral("7"))
                    .toString().contains(QStringLiteral("/pulls/7/files")));
        QString error;
        auto files = Git::HostingApiService::parseReviewFiles(
            github.provider,
            R"([{"filename":"src/main.cpp","previous_filename":"src/old.cpp","status":"renamed","patch":"@@ -1 +1 @@\n-old\n+new","blob_url":"https://github.com/o/r/blob/x/src/main.cpp"}])",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(files.first().path, QStringLiteral("src/main.cpp"));
        QVERIFY(files.first().patch.contains(QStringLiteral("+new")));

        files = Git::HostingApiService::parseReviewFiles(
            Git::HostingProvider::GitLab,
            R"({"changes":[{"old_path":"a.txt","new_path":"b.txt","renamed_file":true,"diff":"@@ -1 +1 @@"}]})",
            &error);
        QCOMPARE(files.first().status, QStringLiteral("renamed"));

        files = Git::HostingApiService::parseReviewFiles(
            Git::HostingProvider::AzureDevOps,
            R"({"changeEntries":[{"changeType":"edit","item":{"path":"/src/app.cpp"}}]})",
            &error);
        QCOMPARE(files.first().path, QStringLiteral("/src/app.cpp"));
    }

    void successfulParseClearsPreviousError()
    {
        QString error = QStringLiteral("stale error");
        const auto changes = Git::HostingApiService::parseChanges(
            Git::HostingProvider::GitHub,
            R"([{"number":1,"title":"Ready","html_url":"https://example.test/1","user":{"login":"a"}}])",
            &error);
        QCOMPARE(changes.size(), 1);
        QVERIFY(error.isEmpty());
    }

    void buildsLineCommentPayloads()
    {
        Git::HostingChangeInfo change;
        change.id = QStringLiteral("7");
        change.headSha = QStringLiteral("head");
        Git::HostingReviewFile file;
        file.path = QStringLiteral("src/main.cpp");
        file.previousPath = QStringLiteral("src/old.cpp");
        file.baseSha = QStringLiteral("base");
        file.startSha = QStringLiteral("start");
        file.headSha = QStringLiteral("head");
        QString error;

        auto document = QJsonDocument::fromJson(
            Git::HostingApiService::reviewCommentBody(
                Git::HostingProvider::GitHub, change, file, 12,
                QStringLiteral("Please fix"), &error));
        QCOMPARE(document.object().value(QStringLiteral("line")).toInt(), 12);
        QCOMPARE(document.object().value(QStringLiteral("side")).toString(),
                 QStringLiteral("RIGHT"));

        document = QJsonDocument::fromJson(
            Git::HostingApiService::reviewCommentBody(
                Git::HostingProvider::GitLab, change, file, 13,
                QStringLiteral("Please fix"), &error));
        QCOMPARE(document.object().value(QStringLiteral("position")).toObject()
                     .value(QStringLiteral("new_line")).toInt(), 13);

        document = QJsonDocument::fromJson(
            Git::HostingApiService::reviewCommentBody(
                Git::HostingProvider::AzureDevOps, change, file, 14,
                QStringLiteral("Please fix"), &error));
        QCOMPARE(document.object().value(QStringLiteral("threadContext")).toObject()
                     .value(QStringLiteral("rightFileStart")).toObject()
                     .value(QStringLiteral("line")).toInt(), 14);
    }
};

QTEST_GUILESS_MAIN(TestHostingApiService)
#include "TestHostingApiService.moc"
