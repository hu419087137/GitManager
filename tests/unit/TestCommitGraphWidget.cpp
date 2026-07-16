#include "widgets/CommitGraphWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

class TestCommitGraphWidget : public QObject {
    Q_OBJECT

    static Git::Commit makeCommit(QChar fill, const QString& subject,
                                  const QStringList& parents = {})
    {
        Git::Commit commit;
        commit.hash = QString(40, fill);
        commit.shortHash = commit.hash.left(8);
        commit.parents = parents;
        commit.authorName = QStringLiteral("History Author");
        commit.date = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
        commit.subject = subject;
        return commit;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(_settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("GitManagerTests"));
        QCoreApplication::setApplicationName(QStringLiteral("TestCommitGraphWidget"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           _settingsDirectory.path());
        qRegisterMetaType<Git::CommitHistoryQuery>();
    }

    void init()
    {
        QSettings settings;
        settings.clear();
    }

    void modelAppendsIncrementallyAndBuildsIndexes()
    {
        const Git::Commit parent = makeCommit(QLatin1Char('a'),
                                               QStringLiteral("parent"));
        const Git::Commit child = makeCommit(QLatin1Char('b'),
                                              QStringLiteral("child"),
                                              {parent.hash});
        const Git::Commit grandchild = makeCommit(QLatin1Char('c'),
                                                   QStringLiteral("grandchild"),
                                                   {child.hash});

        CommitGraphModel model;
        model.setCommits({child});
        QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        QVERIFY(insertedSpy.isValid());
        QVERIFY(resetSpy.isValid());

        model.appendCommits({parent, child, grandchild, grandchild});

        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(insertedSpy.count(), 1);
        QCOMPARE(insertedSpy.first().at(1).toInt(), 1);
        QCOMPARE(insertedSpy.first().at(2).toInt(), 2);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.rowForHash(parent.hash), 1);
        QCOMPARE(model.rowForHash(child.shortHash), 0);
        QCOMPARE(model.rowForHash(grandchild.hash), 2);
        QCOMPARE(model.childHashes(parent.hash), QStringList{child.hash});
        QCOMPARE(model.childHashes(child.hash), QStringList{grandchild.hash});
    }

    void widgetAppendPreservesSelection()
    {
        const Git::Commit parent = makeCommit(QLatin1Char('d'),
                                               QStringLiteral("parent"));
        const Git::Commit child = makeCommit(QLatin1Char('e'),
                                              QStringLiteral("child"),
                                              {parent.hash});
        const Git::Commit tip = makeCommit(QLatin1Char('f'),
                                            QStringLiteral("tip"),
                                            {child.hash});

        CommitGraphWidget widget;
        Git::CommitHistoryPage firstPage;
        firstPage.commits = {tip, child};
        firstPage.hasMore = true;
        widget.resetHistory(firstPage);
        widget.selectCommit(child.hash);

        auto* view = widget.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
        auto* model = widget.findChild<CommitGraphModel*>(QStringLiteral("commitGraphModel"));
        QVERIFY(view);
        QVERIFY(model);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        const QPersistentModelIndex selectedBefore(
            view->selectionModel()->selectedRows().first());
        QCOMPARE(model->commitAt(selectedBefore.row()).hash, child.hash);

        Git::CommitHistoryPage secondPage;
        secondPage.offset = 2;
        secondPage.commits = {parent};
        widget.appendHistory(secondPage);

        QVERIFY(selectedBefore.isValid());
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        const QModelIndex selectedAfter = view->selectionModel()->selectedRows().first();
        QCOMPARE(selectedAfter.row(), selectedBefore.row());
        QCOMPARE(model->commitAt(selectedAfter.row()).hash, child.hash);
        QCOMPARE(model->rowCount(), 3);

        QSignalSpy clearedSpy(&widget,
                              &CommitGraphWidget::sigCommitSelectionCleared);
        Git::CommitHistoryPage filteredPage;
        filteredPage.commits = {parent};
        widget.resetHistory(filteredPage);
        QCOMPARE(clearedSpy.count(), 1);
        QVERIFY(view->selectionModel()->selectedRows().isEmpty());
    }

    void graphColumnExpandsForLoadedLanes()
    {
        CommitGraphWidget widget;
        Git::Commit commit = makeCommit(QLatin1Char('9'), QStringLiteral("wide graph"));
        commit.lane = 9;
        commit.activeLanes.resize(10);
        Git::CommitHistoryPage page;
        page.commits = {commit};
        widget.resetHistory(page);

        auto* view = widget.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
        QVERIFY(view);
        QVERIFY(view->columnWidth(CommitGraphModel::ColGraph) >= 168);
    }

    void queryDefaultsAndBranchUpdatesAreStable()
    {
        CommitGraphWidget widget;
        const Git::CommitHistoryQuery initial = widget.historyQuery();
        QVERIFY(initial.searchText.isEmpty());
        QVERIFY(initial.author.isEmpty());
        QVERIFY(initial.branch.isEmpty());
        QVERIFY(initial.path.isEmpty());
        QVERIFY(!initial.fromDate.isValid());
        QVERIFY(!initial.toDate.isValid());
        QVERIFY(!initial.oldestFirst);
        QCOMPARE(initial.offset, 0);
        QCOMPARE(initial.limit, 200);

        QSignalSpy querySpy(&widget, &CommitGraphWidget::sigHistoryQueryChanged);
        QVERIFY(querySpy.isValid());
        Git::Branch branch;
        branch.name = QStringLiteral("main");
        branch.fullName = QStringLiteral("refs/heads/main");
        branch.isCurrent = true;
        widget.setBranches({branch});
        QCOMPARE(querySpy.count(), 0);

        auto* branchCombo = widget.findChild<QComboBox*>(
            QStringLiteral("commitBranchCombo"));
        QVERIFY(branchCombo);
        const int branchIndex = branchCombo->findData(branch.fullName);
        QVERIFY(branchIndex >= 0);
        branchCombo->setCurrentIndex(branchIndex);
        QCOMPARE(querySpy.count(), 1);
        const Git::CommitHistoryQuery selectedQuery =
            qvariant_cast<Git::CommitHistoryQuery>(querySpy.first().at(0));
        QCOMPARE(selectedQuery.branch, branch.fullName);

        widget.setBranches({branch});
        QCOMPARE(querySpy.count(), 1);
        QCOMPARE(widget.historyQuery().branch, branch.fullName);
    }

    void restoresHeaderAndSortSettings()
    {
        {
            CommitGraphWidget widget;
            auto* view = widget.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
            auto* order = widget.findChild<QComboBox*>(QStringLiteral("commitOrderCombo"));
            QVERIFY(view);
            QVERIFY(order);
            view->setColumnWidth(CommitGraphModel::ColHash, 137);
            order->setCurrentIndex(1);
            QVERIFY(widget.historyQuery().oldestFirst);
        }

        CommitGraphWidget restored;
        auto* view = restored.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
        QVERIFY(view);
        QCOMPARE(view->columnWidth(CommitGraphModel::ColHash), 137);
        QVERIFY(restored.historyQuery().oldestFirst);
    }

    void loadingPreventsRepeatedLoadMoreRequests()
    {
        CommitGraphWidget widget;
        Git::CommitHistoryPage page;
        page.commits = {makeCommit(QLatin1Char('1'), QStringLiteral("commit"))};
        page.hasMore = true;
        widget.resetHistory(page);

        auto* loadMore = widget.findChild<QPushButton*>(
            QStringLiteral("commitHistoryLoadMoreButton"));
        QVERIFY(loadMore);
        QVERIFY(loadMore->isEnabled());
        QSignalSpy loadSpy(&widget, &CommitGraphWidget::sigLoadMoreRequested);
        QVERIFY(loadSpy.isValid());

        loadMore->click();
        loadMore->click();
        QCOMPARE(loadSpy.count(), 1);
        QVERIFY(!loadMore->isEnabled());

        widget.setHistoryLoading(false);
        QVERIFY(loadMore->isEnabled());
        loadMore->click();
        QCOMPARE(loadSpy.count(), 2);

        Git::CommitHistoryPage finalPage;
        finalPage.offset = 1;
        finalPage.hasMore = false;
        widget.appendHistory(finalPage);
        QVERIFY(!loadMore->isEnabled());
    }

private:
    QTemporaryDir _settingsDirectory;
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);
    TestCommitGraphWidget test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestCommitGraphWidget.moc"
