#include "core/HostingService.h"
#include <QtTest>

class TestHostingService : public QObject {
    Q_OBJECT
private slots:
    void recognizesGitHubSsh()
    {
        Git::RemoteInfo remote {QStringLiteral("origin"),
            QStringLiteral("git@github.com:openai/codex.git"), {}};
        const auto value = Git::HostingService::describe(
            remote, QStringLiteral("abc123"), QStringLiteral("feature/test"));
        QCOMPARE(value.provider, Git::HostingProvider::GitHub);
        QCOMPARE(value.webUrl, QStringLiteral("https://github.com/openai/codex"));
        QCOMPARE(value.commitUrl, QStringLiteral("https://github.com/openai/codex/commit/abc123"));
        QCOMPARE(value.changesUrl, QStringLiteral("https://github.com/openai/codex/pulls"));
        QVERIFY(value.createChangeUrl.contains(QStringLiteral("feature%2Ftest")));
        QCOMPARE(value.issuesUrl, QStringLiteral("https://github.com/openai/codex/issues"));
    }

    void recognizesGitLabHttps()
    {
        Git::RemoteInfo remote {QStringLiteral("origin"),
            QStringLiteral("https://gitlab.com/group/project.git"), {}};
        const auto value = Git::HostingService::describe(
            remote, QStringLiteral("deadbeef"), QStringLiteral("topic"));
        QCOMPARE(value.provider, Git::HostingProvider::GitLab);
        QCOMPARE(value.webUrl, QStringLiteral("https://gitlab.com/group/project"));
        QVERIFY(value.createChangeUrl.contains(QStringLiteral("merge_requests/new")));
        QCOMPARE(value.changesUrl,
                 QStringLiteral("https://gitlab.com/group/project/-/merge_requests"));
        QVERIFY(value.createChangeUrl.contains(QStringLiteral("source_branch")));
        QCOMPARE(value.issuesUrl, QStringLiteral("https://gitlab.com/group/project/-/issues"));
    }

    void recognizesAzureSsh()
    {
        Git::RemoteInfo remote {QStringLiteral("origin"),
            QStringLiteral("git@ssh.dev.azure.com:v3/org/team/repository"), {}};
        const auto value = Git::HostingService::describe(
            remote, QStringLiteral("1234"), QStringLiteral("main"));
        QCOMPARE(value.provider, Git::HostingProvider::AzureDevOps);
        QCOMPARE(value.webUrl,
                 QStringLiteral("https://dev.azure.com/org/team/_git/repository"));
        QCOMPARE(value.commitUrl,
                 QStringLiteral("https://dev.azure.com/org/team/_git/repository/commit/1234"));
        QCOMPARE(value.changesUrl,
                 QStringLiteral("https://dev.azure.com/org/team/_git/repository/pullrequests"));
        QCOMPARE(value.issuesUrl,
                 QStringLiteral("https://dev.azure.com/org/team/_workitems/recentlyupdated"));
    }

    void leavesUnknownRemoteUnrecognized()
    {
        Git::RemoteInfo remote {QStringLiteral("origin"),
            QStringLiteral("https://example.com/team/repository.git"), {}};
        QCOMPARE(Git::HostingService::describe(remote, {}, {}).provider,
                 Git::HostingProvider::Unknown);
    }
};

QTEST_GUILESS_MAIN(TestHostingService)
#include "TestHostingService.moc"
