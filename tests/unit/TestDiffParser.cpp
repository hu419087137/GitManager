#include "core/DiffParser.h"
#include "core/LibGit2Backend.h"
#include <QtTest>

class TestDiffParser : public QObject {
    Q_OBJECT
private slots:
    void extractsSelectedHunk()
    {
        const QString diff = "diff --git a/a.txt b/a.txt\nindex 111..222 100644\n--- a/a.txt\n+++ b/a.txt\n"
                             "@@ -1 +1 @@\n-old\n+new\n@@ -3 +3 @@\n-x\n+y\n";
        const QString patch = Git::DiffParser::hunkAtLine(diff, 6);
        QVERIFY(patch.contains("@@ -1 +1 @@"));
        QVERIFY(!patch.contains("@@ -3 +3 @@"));
        QVERIFY(patch.startsWith("diff --git"));
    }

    void reversesPatchHeaders()
    {
        const QString patch = "diff --git a/old.txt b/new.txt\n"
                              "similarity index 90%\n"
                              "rename from old.txt\n"
                              "rename to new.txt\n"
                              "--- a/old.txt\n"
                              "+++ b/new.txt\n"
                              "@@ -1 +1 @@\n-old\n+new\n";
        const QString reversed = Git::LibGit2Backend::reversePatch(patch);
        QVERIFY(reversed.contains("diff --git a/new.txt b/old.txt"));
        QVERIFY(reversed.contains("rename from new.txt\nrename to old.txt"));
        QVERIFY(reversed.contains("--- a/new.txt\n+++ b/old.txt"));
        QVERIFY(reversed.contains("@@ -1 +1 @@\n+old\n-new"));
    }
};
QTEST_GUILESS_MAIN(TestDiffParser)
#include "TestDiffParser.moc"
