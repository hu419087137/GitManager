#include "core/parsers/BranchParser.h"
#include <QtTest>

class TestBranchParser : public QObject {
    Q_OBJECT
private slots:
    void distinguishesRefsAndTracking()
    {
        QByteArray data;
        data += "refs/heads/feature/login\tfeature/login\tabc\torigin/feature/login\tahead 2, behind 1";
        data += '\0';
        data += "refs/remotes/origin/main\torigin/main\tdef\t\t";
        data += '\0';
        const auto branches = Git::BranchParser::parse(data, QString("feature/login"));
        QCOMPARE(branches.size(), 2);
        QVERIFY(!branches[0].isRemote);
        QVERIFY(branches[0].isCurrent);
        QCOMPARE(branches[0].ahead, 2);
        QCOMPARE(branches[0].behind, 1);
        QVERIFY(branches[1].isRemote);
    }
};

QTEST_GUILESS_MAIN(TestBranchParser)
#include "TestBranchParser.moc"
