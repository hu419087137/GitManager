#include "core/HookService.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class TestHookService : public QObject {
    Q_OBJECT
private slots:
    void discoversAndRunsHook()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QProcess git;
        git.setWorkingDirectory(directory.path());
        git.start(QStringLiteral("git"), {QStringLiteral("init")});
        QVERIFY(git.waitForFinished(10000));
        QCOMPARE(git.exitCode(), 0);

        const QString hookPath = QDir(directory.path()).filePath(
            QStringLiteral(".git/hooks/pre-commit"));
        QFile hook(hookPath);
        QVERIFY(hook.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        hook.write("#!/bin/sh\necho hook-output\nexit 3\n");
#else
        hook.write("#!/bin/sh\necho hook-output\nexit 3\n");
#endif
        hook.close();
        hook.setPermissions(hook.permissions() | QFileDevice::ExeOwner
                            | QFileDevice::ExeGroup | QFileDevice::ExeOther);

        Git::HookService service(directory.path());
        QString error;
        const auto hooks = service.hooks(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(hooks.size(), 1);
        QCOMPARE(hooks.first().name, QStringLiteral("pre-commit"));
        const auto result = service.run(QStringLiteral("pre-commit"));
        QVERIFY(!result.success);
        QVERIFY2(result.exitCode != 0, qPrintable(result.output));
        QVERIFY2(result.output.contains(QStringLiteral("hook-output")),
                 qPrintable(result.output));
    }

    void rejectsUnsafeHookName()
    {
        Git::HookService service(QStringLiteral("."));
        QVERIFY(!service.run(QStringLiteral("../unsafe")).success);
    }
};
QTEST_GUILESS_MAIN(TestHookService)
#include "TestHookService.moc"
