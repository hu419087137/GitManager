#include "core/parsers/StatusParser.h"
#include <QtTest>

class TestStatusParser : public QObject {
    Q_OBJECT

private slots:
    void parsesBranchAndDualState()
    {
        QByteArray data = "# branch.oid abcdef012345\n# branch.head feature/login\n"
                          "# branch.upstream origin/feature/login\n# branch.ab +2 -3\n";
        data += "1 MM N... 100644 100644 100644 abc def src/main file.cpp\0";
        const auto result = Git::StatusParser::parse(data);
        QCOMPARE(result.headName, QString("feature/login"));
        QCOMPARE(result.upstream, QString("origin/feature/login"));
        QCOMPARE(result.ahead, 2);
        QCOMPARE(result.behind, 3);
        QCOMPARE(result.files.size(), 1);
        QVERIFY(result.files[0].isStaged());
        QVERIFY(result.files[0].isUnstaged());
        QCOMPARE(result.files[0].path, QString("src/main file.cpp"));
    }

    void parsesRenameConflictAndSpecialPaths()
    {
        QByteArray data;
        data += "2 R. N... 100644 100644 100644 abc def R100 new name.cpp";
        data += '\0';
        data += "old name.cpp";
        data += '\0';
        data += "u UU N... 100644 100644 100644 100644 a b c 冲突.txt";
        data += '\0';
        data += QByteArray("? line\nbreak.txt", 16);
        data += '\0';
        data += "1 .D N... 100644 100644 000000 abc def quoted\"name.cpp";
        data += '\0';
        const auto result = Git::StatusParser::parse(data);
        QCOMPARE(result.files.size(), 4);
        QCOMPARE(result.files[0].path, QString("new name.cpp"));
        QCOMPARE(result.files[0].originalPath, QString("old name.cpp"));
        QVERIFY(result.files[1].conflicted);
        QCOMPARE(result.files[2].path, QString("line\nbreak.txt"));
        QVERIFY(!result.files[2].tracked);
        QCOMPARE(result.files[3].workStatus, Git::File::Status::E_Deleted);
        QCOMPARE(result.files[3].path, QString("quoted\"name.cpp"));
    }

    void parsesUnbornAndDetached()
    {
        auto unborn = Git::StatusParser::parse("# branch.oid (initial)\n# branch.head main\n");
        QVERIFY(unborn.unborn);
        QCOMPARE(unborn.headName, QString("main"));
        auto detached = Git::StatusParser::parse("# branch.oid abc\n# branch.head (detached)\n");
        QVERIFY(detached.detached);
    }
};

QTEST_GUILESS_MAIN(TestStatusParser)
#include "TestStatusParser.moc"
