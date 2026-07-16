#include "core/LibGit2Backend.h"
#include <git2.h>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestLibGit2Backend : public QObject {
    Q_OBJECT
private slots:
    void opensStagesAndReadsStatus()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        Git::LibGit2Backend backend; QString error;
        QVERIFY2(backend.initialize(dir.path(), &error), qPrintable(error));
        backend.close();
        QVERIFY2(backend.open(dir.path(),&error),qPrintable(error));
        QFile file(dir.filePath(QStringLiteral("中文 file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("content\n"); file.close();
        auto state=backend.snapshot(&error); QCOMPARE(state.files.size(),1);
        QCOMPARE(state.files[0].workStatus,Git::File::Status::E_Untracked);
        QVERIFY2(backend.stage(QStringLiteral("中文 file.txt"),&error),qPrintable(error));
        state=backend.snapshot(&error); QVERIFY(state.files[0].isStaged());
    }
};
QTEST_GUILESS_MAIN(TestLibGit2Backend)
#include "TestLibGit2Backend.moc"
