#include "core/CredentialStore.h"
#include <QtTest>

class TestCredentialStore : public QObject {
    Q_OBJECT
private slots:
    void usesStableProviderKeys()
    {
        QCOMPARE(Git::CredentialStore::key(Git::HostingProvider::GitHub),
                 QStringLiteral("GitManager/Hosting/GitHub"));
        QCOMPARE(Git::CredentialStore::key(Git::HostingProvider::GitLab),
                 QStringLiteral("GitManager/Hosting/GitLab"));
        QCOMPARE(Git::CredentialStore::key(Git::HostingProvider::AzureDevOps),
                 QStringLiteral("GitManager/Hosting/Azure DevOps"));
    }
};

QTEST_GUILESS_MAIN(TestCredentialStore)
#include "TestCredentialStore.moc"
