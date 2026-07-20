#include "widgets/CommitGraphWidget.h"
#include "widgets/BranchListWidget.h"
#include "widgets/CompareWidget.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
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

    static QMenu* visibleMenu()
    {
        QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu)
            return menu;
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            menu = qobject_cast<QMenu*>(topLevel);
            if (menu && menu->isVisible())
                return menu;
        }
        return nullptr;
    }

    static void scheduleMenuFailsafe()
    {
        QTimer::singleShot(250, [] {
            if (QMenu* menu = visibleMenu())
                menu->close();
        });
    }

    static bool triggerContextAction(CommitGraphWidget* widget, QTreeView* view,
                                     const QString& objectName, bool* enabled,
                                     int row = 0)
    {
        bool found = false;
        QTimer::singleShot(0, [&] {
            QMenu* menu = visibleMenu();
            if (!menu)
                return;
            QAction* action = menu->findChild<QAction*>(objectName);
            if (action) {
                found = true;
                *enabled = action->isEnabled();
                if (*enabled)
                    action->trigger();
            }
            menu->close();
        });
        scheduleMenuFailsafe();

        const QModelIndex index = view->model()->index(row, CommitGraphModel::ColMessage);
        const QPoint position = view->visualRect(index).center();
        const bool invoked = QMetaObject::invokeMethod(
            widget, "slotContextMenu", Qt::DirectConnection, Q_ARG(QPoint, position));
        return invoked && found;
    }

    static bool triggerBranchAction(BranchListWidget* widget,
                                    QTreeWidgetItem* item,
                                    const QString& objectName, bool* enabled)
    {
        bool found = false;
        QTimer::singleShot(0, [&] {
            QMenu* menu = visibleMenu();
            if (!menu)
                return;
            QAction* action = menu->findChild<QAction*>(objectName);
            if (action) {
                found = true;
                *enabled = action->isEnabled();
                if (*enabled)
                    action->trigger();
            }
            menu->close();
        });
        scheduleMenuFailsafe();
        const QPoint position = widget->visualItemRect(item).center();
        const bool invoked = QMetaObject::invokeMethod(
            widget, "slotContextMenu", Qt::DirectConnection, Q_ARG(QPoint, position));
        return invoked && found;
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

    void historyActionsExposeMainlineAndHonorBusyState()
    {
        const QString firstParent(40, QLatin1Char('a'));
        const QString secondParent(40, QLatin1Char('b'));
        const Git::Commit merge = makeCommit(QLatin1Char('c'),
                                              QStringLiteral("merge commit"),
                                              {firstParent, secondParent});
        CommitGraphWidget widget;
        widget.resize(900, 500);
        Git::CommitHistoryPage page;
        page.commits = {merge};
        widget.resetHistory(page);
        widget.show();
        QCoreApplication::processEvents();

        auto* view = widget.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
        QVERIFY(view);
        QSignalSpy cherryPickSpy(&widget,
                                 &CommitGraphWidget::sigCherryPickRequested);
        QSignalSpy resetSpy(&widget, &CommitGraphWidget::sigResetRequested);

        widget.setOperationsEnabled(true);
        bool enabled = false;
        QVERIFY(triggerContextAction(
            &widget, view, QStringLiteral("historyCherryPickMainline2Action"),
            &enabled));
        QVERIFY(enabled);
        QCOMPARE(cherryPickSpy.count(), 1);
        QCOMPARE(cherryPickSpy.first().at(0).toString(), merge.hash);
        QCOMPARE(cherryPickSpy.first().at(1).toInt(), 2);

        widget.setOperationsEnabled(false);
        enabled = true;
        QVERIFY(triggerContextAction(
            &widget, view, QStringLiteral("historyResetAction"), &enabled));
        QVERIFY(!enabled);
        QCOMPARE(resetSpy.count(), 0);
    }

    void compareActionsAndWidgetEmitRequestedRevisions()
    {
        const Git::Commit newer = makeCommit(QLatin1Char('a'),
                                             QStringLiteral("newer"));
        const Git::Commit older = makeCommit(QLatin1Char('b'),
                                             QStringLiteral("older"),
                                             {newer.hash});

        CommitGraphWidget widget;
        widget.resize(900, 500);
        Git::CommitHistoryPage page;
        page.commits = {newer, older};
        widget.resetHistory(page);
        widget.show();
        QCoreApplication::processEvents();

        auto* view = widget.findChild<QTreeView*>(QStringLiteral("commitHistoryView"));
        QVERIFY(view);

        QSignalSpy compareBaseSpy(&widget,
                                  &CommitGraphWidget::sigCompareBaseSelected);
        QSignalSpy compareSpy(&widget, &CommitGraphWidget::sigCompareRequested);

        bool enabled = false;
        QVERIFY(triggerContextAction(&widget, view,
                                     QStringLiteral("historyCompareBaseAction"),
                                     &enabled, 0));
        QVERIFY(enabled);
        QCOMPARE(compareBaseSpy.count(), 1);
        QCOMPARE(compareBaseSpy.first().at(0).toString(), newer.hash);

        QVERIFY(triggerContextAction(&widget, view,
                                     QStringLiteral("historyCompareWithBaseAction"),
                                     &enabled, 1));
        QVERIFY(enabled);
        QCOMPARE(compareSpy.count(), 1);
        QCOMPARE(compareSpy.first().at(0).toString(), newer.hash);
        QCOMPARE(compareSpy.first().at(1).toString(), older.hash);

        QVERIFY(triggerContextAction(&widget, view,
                                     QStringLiteral("historyCompareClearBaseAction"),
                                     &enabled, 1));
        QVERIFY(enabled);
        QCOMPARE(compareBaseSpy.count(), 2);
        QVERIFY(compareBaseSpy.at(1).at(0).toString().isEmpty());
    }

    void compareWidgetSupportsBranchAndHashSelection()
    {
        CompareWidget widget;
        Git::Branch branch;
        branch.name = QStringLiteral("main");
        branch.fullName = QStringLiteral("refs/heads/main");
        branch.isCurrent = true;
        widget.setBranches({branch});
        widget.setBaseRevision(branch.fullName);
        widget.setTargetRevision(QString(40, QLatin1Char('c')));

        auto* runButton = widget.findChild<QPushButton*>(QStringLiteral("compareRunButton"));
        auto* swapButton = widget.findChild<QToolButton*>(QStringLiteral("compareSwapButton"));
        QVERIFY(runButton);
        QVERIFY(swapButton);
        QVERIFY(runButton->isEnabled());

        QSignalSpy compareSpy(&widget, &CompareWidget::sigCompareRequested);
        runButton->click();
        QCOMPARE(compareSpy.count(), 1);
        QCOMPARE(compareSpy.first().at(0).toString(), branch.fullName);
        QCOMPARE(compareSpy.first().at(1).toString(), QString(40, QLatin1Char('c')));

        swapButton->click();
        QCOMPARE(widget.baseRevision(), QString(40, QLatin1Char('c')));
        QCOMPARE(widget.targetRevision(), branch.fullName);
    }

    void branchActionsRejectCurrentBranchAndEmitTargets()
    {
        Git::Branch current;
        current.name = QStringLiteral("main");
        current.fullName = QStringLiteral("refs/heads/main");
        current.isCurrent = true;
        Git::Branch feature;
        feature.name = QStringLiteral("feature");
        feature.fullName = QStringLiteral("refs/heads/feature");
        Git::Branch tracked;
        tracked.name = QStringLiteral("tracked");
        tracked.fullName = QStringLiteral("refs/heads/tracked");
        tracked.upstream = QStringLiteral("origin/tracked");

        BranchListWidget widget;
        widget.resize(360, 420);
        widget.setBranches({current, feature, tracked});
        widget.setOperationsEnabled(true);
        widget.show();
        QCoreApplication::processEvents();

        QTreeWidgetItem* localRoot = widget.topLevelItem(0);
        QVERIFY(localRoot);
        QCOMPARE(localRoot->childCount(), 3);
        QSignalSpy mergeSpy(&widget, &BranchListWidget::sigMergeRequested);
        QSignalSpy checkoutSpy(&widget, &BranchListWidget::sigCheckoutRequested);
        QSignalSpy renameSpy(&widget, &BranchListWidget::sigRenameRequested);
        QSignalSpy publishSpy(&widget, &BranchListWidget::sigPublishRequested);
        QSignalSpy untrackSpy(&widget, &BranchListWidget::sigUntrackRequested);

        widget.setOperationsEnabled(false);
        QTest::mouseDClick(widget.viewport(), Qt::LeftButton, Qt::NoModifier,
                          widget.visualItemRect(localRoot->child(1)).center());
        QCOMPARE(checkoutSpy.count(), 0);
        widget.setOperationsEnabled(true);
        QTest::mouseDClick(widget.viewport(), Qt::LeftButton, Qt::NoModifier,
                          widget.visualItemRect(localRoot->child(1)).center());
        QCOMPARE(checkoutSpy.count(), 1);
        QCOMPARE(checkoutSpy.first().at(0).toString(), QStringLiteral("feature"));

        bool enabled = true;
        QVERIFY(triggerBranchAction(&widget, localRoot->child(0),
                                    QStringLiteral("branchMergeAction"), &enabled));
        QVERIFY(!enabled);
        QCOMPARE(mergeSpy.count(), 0);

        enabled = false;
        QVERIFY(triggerBranchAction(&widget, localRoot->child(1),
                                    QStringLiteral("branchMergeAction"), &enabled));
        QVERIFY(enabled);
        QCOMPARE(mergeSpy.count(), 1);
        QCOMPARE(mergeSpy.first().at(0).toString(), QStringLiteral("feature"));

        QVERIFY(triggerBranchAction(&widget, localRoot->child(1),
                                    QStringLiteral("branchRenameAction"), &enabled));
        QVERIFY(enabled);
        QCOMPARE(renameSpy.count(), 1);
        QCOMPARE(renameSpy.first().at(0).toString(), QStringLiteral("feature"));

        QVERIFY(triggerBranchAction(&widget, localRoot->child(1),
                                    QStringLiteral("branchPublishAction"), &enabled));
        QVERIFY(enabled);
        QCOMPARE(publishSpy.count(), 1);
        QCOMPARE(publishSpy.first().at(0).toString(), QStringLiteral("feature"));

        QVERIFY(triggerBranchAction(&widget, localRoot->child(2),
                                    QStringLiteral("branchUntrackAction"), &enabled));
        QVERIFY(enabled);
        QCOMPARE(untrackSpy.count(), 1);
        QCOMPARE(untrackSpy.first().at(0).toString(), QStringLiteral("tracked"));
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
