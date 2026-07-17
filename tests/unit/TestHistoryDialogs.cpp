#include "dialogs/RebaseDialog.h"
#include "dialogs/ResetDialog.h"
#include "widgets/InteractiveRebaseWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QtTest>

#include <array>

class TestHistoryDialogs : public QObject {
    Q_OBJECT

    static Git::HistoryRewritePreview preview()
    {
        Git::HistoryRewritePreview value;
        value.revision = QStringLiteral("main~2");
        value.targetHash = QString(40, QLatin1Char('a'));
        value.expectedHead = QString(40, QLatin1Char('f'));
        value.currentBranch = QStringLiteral("main");
        value.upstream = QStringLiteral("origin/main");
        value.affectedCount = 2;
        value.activeOperation = Git::RepositoryOperation::None;
        return value;
    }

    static Git::RebasePlan plan(int itemCount = 2)
    {
        Git::RebasePlan value;
        value.preview = preview();
        for (int row = 0; row < itemCount; ++row) {
            Git::RebasePlanItem item;
            item.hash = QString(40, QChar::fromLatin1(
                static_cast<char>('b' + row)));
            item.subject = QStringLiteral("commit %1").arg(row + 1);
            item.message = item.subject + QStringLiteral("\n\nbody");
            item.action = Git::RebaseAction::Pick;
            item.parentCount = 1;
            value.items.append(item);
        }
        value.preview.affectedCount = itemCount;
        return value;
    }

    static QComboBox* actionCombo(InteractiveRebaseWidget& widget, int row)
    {
        return widget.findChild<QComboBox*>(
            QStringLiteral("rebaseActionCombo_%1").arg(row));
    }

    static void setAction(InteractiveRebaseWidget& widget, int row,
                          Git::RebaseAction action)
    {
        QComboBox* combo = actionCombo(widget, row);
        QVERIFY(combo);
        const int index = combo->findData(static_cast<int>(action));
        QVERIFY(index >= 0);
        combo->setCurrentIndex(index);
    }

private slots:
    void resetDefaultsToMixed()
    {
        ResetDialog dialog(preview());
        QCOMPARE(dialog.resetMode(), Git::ResetMode::Mixed);

        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        auto* mixedRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetMixedRadio"));
        QVERIFY(resetButton);
        QVERIFY(mixedRadio);
        QVERIFY(mixedRadio->isChecked());
        QVERIFY(resetButton->isEnabled());
    }

    void hardResetRequiresConfirmation()
    {
        ResetDialog dialog(preview());
        auto* hardRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetHardRadio"));
        auto* hardConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetHardConfirmCheck"));
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        QVERIFY(hardRadio);
        QVERIFY(hardConfirm);
        QVERIFY(resetButton);

        hardRadio->setChecked(true);
        QCOMPARE(dialog.resetMode(), Git::ResetMode::Hard);
        QVERIFY(!resetButton->isEnabled());
        hardConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
    }

    void publishedResetRequiresIndependentConfirmation()
    {
        Git::HistoryRewritePreview value = preview();
        value.publishedCount = 2;
        ResetDialog dialog(value);
        auto* publishedConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetPublishedConfirmCheck"));
        auto* hardRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetHardRadio"));
        auto* hardConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetHardConfirmCheck"));
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        QVERIFY(publishedConfirm);
        QVERIFY(hardRadio);
        QVERIFY(hardConfirm);
        QVERIFY(resetButton);

        QVERIFY(!resetButton->isEnabled());
        publishedConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
        hardRadio->setChecked(true);
        QVERIFY(!resetButton->isEnabled());
        hardConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
    }

    void resetBlocksActiveRepositoryOperation()
    {
        Git::HistoryRewritePreview value = preview();
        value.activeOperation = Git::RepositoryOperation::Rebase;
        ResetDialog dialog(value);
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        auto* blockingReason = dialog.findChild<QLabel*>(
            QStringLiteral("resetBlockingReasonLabel"));
        QVERIFY(resetButton);
        QVERIFY(blockingReason);
        QVERIFY(!resetButton->isEnabled());
        QVERIFY(blockingReason->isVisibleTo(&dialog)
                || !blockingReason->text().isEmpty());
    }

    void interactiveWidgetSupportsAllActions()
    {
        InteractiveRebaseWidget widget(plan());
        const std::array<Git::RebaseAction, 6> actions {{
            Git::RebaseAction::Pick,
            Git::RebaseAction::Reword,
            Git::RebaseAction::Edit,
            Git::RebaseAction::Squash,
            Git::RebaseAction::Fixup,
            Git::RebaseAction::Drop,
        }};

        for (Git::RebaseAction action : actions) {
            setAction(widget, 1, action);
            QCOMPARE(widget.rebasePlan().items.at(1).action, action);
        }
    }

    void interactiveWidgetValidatesFirstNonDropAndMessages()
    {
        InteractiveRebaseWidget widget(plan());
        setAction(widget, 0, Git::RebaseAction::Drop);
        setAction(widget, 1, Git::RebaseAction::Squash);
        QVERIFY(!widget.isPlanValid());

        setAction(widget, 1, Git::RebaseAction::Fixup);
        QVERIFY(!widget.isPlanValid());

        setAction(widget, 0, Git::RebaseAction::Pick);
        setAction(widget, 1, Git::RebaseAction::Squash);
        QVERIFY(widget.isPlanValid());

        auto* table = widget.findChild<QTableWidget*>(
            QStringLiteral("interactiveRebaseTable"));
        auto* messageEdit = widget.findChild<QPlainTextEdit*>(
            QStringLiteral("rebaseMessageEdit"));
        QVERIFY(table);
        QVERIFY(messageEdit);
        table->setCurrentCell(1, 0);
        setAction(widget, 1, Git::RebaseAction::Reword);
        messageEdit->clear();
        QVERIFY(!widget.isPlanValid());
        messageEdit->setPlainText(QStringLiteral("rewritten message"));
        QVERIFY(widget.isPlanValid());
        QCOMPARE(widget.rebasePlan().items.at(1).rewrittenMessage,
                 QStringLiteral("rewritten message"));
    }

    void rebaseDialogDefaultsToNormalAndReturnsInteractivePlan()
    {
        RebaseDialog dialog(plan());
        auto* startButton = dialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"));
        auto* interactiveRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("rebaseInteractiveRadio"));
        auto* editor = dialog.findChild<InteractiveRebaseWidget*>(
            QStringLiteral("interactiveRebaseWidget"));
        QVERIFY(startButton);
        QVERIFY(interactiveRadio);
        QVERIFY(editor);
        QVERIFY(!dialog.interactiveMode());
        QVERIFY(startButton->isEnabled());

        interactiveRadio->setChecked(true);
        QVERIFY(dialog.interactiveMode());
        setAction(*editor, 1, Git::RebaseAction::Drop);
        QCOMPARE(dialog.rebasePlan().items.at(1).action,
                 Git::RebaseAction::Drop);
    }

    void rebaseDialogGatesPublishedDirtyActiveAndEmptyPlans()
    {
        Git::RebasePlan published = plan();
        published.preview.publishedCount = 1;
        published.items[0].published = true;
        RebaseDialog publishedDialog(published);
        auto* publishedStart = publishedDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"));
        auto* publishedConfirm = publishedDialog.findChild<QCheckBox*>(
            QStringLiteral("rebasePublishedConfirmCheck"));
        QVERIFY(publishedStart);
        QVERIFY(publishedConfirm);
        QVERIFY(!publishedStart->isEnabled());
        publishedConfirm->setChecked(true);
        QVERIFY(publishedStart->isEnabled());

        Git::RebasePlan dirty = plan();
        dirty.preview.dirty = true;
        RebaseDialog dirtyDialog(dirty);
        QVERIFY(!dirtyDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());

        Git::RebasePlan active = plan();
        active.preview.activeOperation = Git::RepositoryOperation::Merge;
        RebaseDialog activeDialog(active);
        QVERIFY(!activeDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());

        RebaseDialog emptyDialog(plan(0));
        QVERIFY(!emptyDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);
    TestHistoryDialogs test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestHistoryDialogs.moc"
