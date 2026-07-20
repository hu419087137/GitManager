#include "core/GitDiagnosticService.h"
#include <QtTest>

class TestGitDiagnosticService : public QObject {
    Q_OBJECT
private slots:
    void redactsCredentialsAndSecrets()
    {
        const QString value = Git::GitDiagnosticService::redact(
            QStringLiteral("https://user:password@example.com/repo?token=abc&x=1"));
        QVERIFY(!value.contains(QStringLiteral("password")));
        QVERIFY(!value.contains(QStringLiteral("abc")));
        QVERIFY(value.contains(QStringLiteral("***")));
    }

    void classifiesRemoteProtocols()
    {
        QCOMPARE(Git::GitDiagnosticService::classifyRemote(
                     QStringLiteral("git@example.com:org/repo.git")),
                 QStringLiteral("SSH"));
        QCOMPARE(Git::GitDiagnosticService::classifyRemote(
                     QStringLiteral("https://example.com/org/repo.git")),
                 QStringLiteral("HTTPS"));
        QCOMPARE(Git::GitDiagnosticService::classifyRemote(
                     QStringLiteral("http://example.com/org/repo.git")),
                 QStringLiteral("HTTP (insecure)"));
    }
};

QTEST_GUILESS_MAIN(TestGitDiagnosticService)
#include "TestGitDiagnosticService.moc"
