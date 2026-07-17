#include "InteractiveRebaseWidget.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace {

struct ActionChoice {
    Git::RebaseAction action;
    const char* label;
};

constexpr std::array<ActionChoice, 6> kActionChoices {{
    {Git::RebaseAction::Pick, "pick"},
    {Git::RebaseAction::Reword, "reword"},
    {Git::RebaseAction::Edit, "edit"},
    {Git::RebaseAction::Squash, "squash"},
    {Git::RebaseAction::Fixup, "fixup"},
    {Git::RebaseAction::Drop, "drop"},
}};

bool editsMessage(Git::RebaseAction action)
{
    return action == Git::RebaseAction::Reword
        || action == Git::RebaseAction::Squash;
}

} // namespace

InteractiveRebaseWidget::InteractiveRebaseWidget(QWidget* parent)
    : QWidget(parent)
{
    _table = new QTableWidget(this);
    _table->setObjectName(QStringLiteral("interactiveRebaseTable"));
    _table->setColumnCount(4);
    _table->setHorizontalHeaderLabels({
        QStringLiteral("Action"), QStringLiteral("Commit"),
        QStringLiteral("Message"), QStringLiteral("Published")});
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    _table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setDragDropMode(QAbstractItemView::NoDragDrop);

    _messageLabel = new QLabel(QStringLiteral("Commit message"), this);
    _messageLabel->setObjectName(QStringLiteral("rebaseMessageLabel"));
    _messageEdit = new QPlainTextEdit(this);
    _messageEdit->setObjectName(QStringLiteral("rebaseMessageEdit"));
    _messageEdit->setMinimumHeight(84);
    _messageEdit->setReadOnly(true);

    _validationLabel = new QLabel(this);
    _validationLabel->setObjectName(QStringLiteral("rebaseValidationLabel"));
    _validationLabel->setWordWrap(true);
    _validationLabel->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));
    _validationLabel->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_table, 1);
    layout->addWidget(_messageLabel);
    layout->addWidget(_messageEdit);
    layout->addWidget(_validationLabel);

    connect(_table, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) {
        loadMessageEditor(currentRow);
    });
    connect(_messageEdit, &QPlainTextEdit::textChanged, this, [this] {
        if (_updating || _currentRow < 0 || _currentRow >= _plan.items.size())
            return;
        Git::RebasePlanItem& item = _plan.items[_currentRow];
        if (!editsMessage(item.action))
            return;
        item.rewrittenMessage = _messageEdit->toPlainText();
        updateRow(_currentRow);
        updateValidation();
        emit sigPlanChanged();
    });
}

InteractiveRebaseWidget::InteractiveRebaseWidget(const Git::RebasePlan& plan,
                                                 QWidget* parent)
    : InteractiveRebaseWidget(parent)
{
    setRebasePlan(plan);
}

void InteractiveRebaseWidget::setRebasePlan(const Git::RebasePlan& plan)
{
    _plan = plan;
    rebuildTable();
}

Git::RebasePlan InteractiveRebaseWidget::rebasePlan() const
{
    return _plan;
}

void InteractiveRebaseWidget::rebuildTable()
{
    _updating = true;
    _currentRow = -1;
    _table->clearContents();
    _table->setRowCount(_plan.items.size());

    for (int row = 0; row < _plan.items.size(); ++row) {
        const Git::RebasePlanItem& item = _plan.items.at(row);
        auto* actionCombo = new QComboBox(_table);
        actionCombo->setObjectName(
            QStringLiteral("rebaseActionCombo_%1").arg(row));
        actionCombo->setProperty("rebaseRow", row);
        for (const ActionChoice& choice : kActionChoices) {
            actionCombo->addItem(QString::fromLatin1(choice.label),
                                 static_cast<int>(choice.action));
        }
        const int actionIndex = actionCombo->findData(static_cast<int>(item.action));
        actionCombo->setCurrentIndex(actionIndex >= 0 ? actionIndex : 0);
        _table->setCellWidget(row, 0, actionCombo);

        auto* hashItem = new QTableWidgetItem(item.hash.left(8));
        hashItem->setToolTip(item.hash);
        _table->setItem(row, 1, hashItem);
        _table->setItem(row, 2, new QTableWidgetItem);
        _table->setItem(row, 3, new QTableWidgetItem);
        updateRow(row);

        connect(actionCombo, &QComboBox::currentIndexChanged, this,
                [this, row, actionCombo](int index) {
            if (_updating || row < 0 || row >= _plan.items.size() || index < 0)
                return;
            Git::RebasePlanItem& item = _plan.items[row];
            item.action = static_cast<Git::RebaseAction>(
                actionCombo->itemData(index).toInt());
            if (editsMessage(item.action)
                && item.rewrittenMessage.trimmed().isEmpty()) {
                item.rewrittenMessage = effectiveMessage(item);
            }
            updateRow(row);
            if (_currentRow == row)
                loadMessageEditor(row);
            updateValidation();
            emit sigPlanChanged();
        });
    }

    _updating = false;
    if (!_plan.items.isEmpty())
        _table->setCurrentCell(0, 0);
    else
        loadMessageEditor(-1);
    updateValidation();
}

void InteractiveRebaseWidget::loadMessageEditor(int row)
{
    _currentRow = row;
    const QSignalBlocker blocker(_messageEdit);
    _updating = true;

    if (row < 0 || row >= _plan.items.size()) {
        _messageLabel->setText(QStringLiteral("Commit message"));
        _messageEdit->clear();
        _messageEdit->setReadOnly(true);
        _updating = false;
        return;
    }

    Git::RebasePlanItem& item = _plan.items[row];
    const bool editable = editsMessage(item.action);
    if (editable && item.rewrittenMessage.trimmed().isEmpty())
        item.rewrittenMessage = effectiveMessage(item);

    _messageLabel->setText(
        editable
            ? QStringLiteral("New commit message for %1").arg(item.hash.left(8))
            : QStringLiteral("Original commit message for %1").arg(item.hash.left(8)));
    _messageEdit->setPlainText(editable
        ? item.rewrittenMessage : effectiveMessage(item));
    _messageEdit->setReadOnly(!editable);
    _updating = false;
}

void InteractiveRebaseWidget::updateRow(int row)
{
    if (row < 0 || row >= _plan.items.size())
        return;
    const Git::RebasePlanItem& planItem = _plan.items.at(row);
    QTableWidgetItem* hashItem = _table->item(row, 1);
    QTableWidgetItem* messageItem = _table->item(row, 2);
    QTableWidgetItem* publishedItem = _table->item(row, 3);
    if (!hashItem || !messageItem || !publishedItem)
        return;

    const bool dropped = planItem.action == Git::RebaseAction::Drop;
    QString message = planItem.subject;
    if (editsMessage(planItem.action))
        message = effectiveMessage(planItem).section(QLatin1Char('\n'), 0, 0);
    messageItem->setText(message);
    messageItem->setToolTip(effectiveMessage(planItem));
    publishedItem->setText(planItem.published ? QStringLiteral("Yes") : QString());

    const QBrush foreground = dropped
        ? palette().brush(QPalette::Disabled, QPalette::Text)
        : palette().brush(QPalette::Active, QPalette::Text);
    for (QTableWidgetItem* item : {hashItem, messageItem, publishedItem}) {
        item->setForeground(foreground);
        QFont font = item->font();
        font.setStrikeOut(dropped);
        item->setFont(font);
    }
    if (planItem.published && !dropped)
        publishedItem->setForeground(QBrush(QColor(QStringLiteral("#b42318"))));
}

void InteractiveRebaseWidget::updateValidation()
{
    bool valid = true;
    QString message;

    int firstActive = -1;
    for (int row = 0; row < _plan.items.size(); ++row) {
        if (_plan.items.at(row).action != Git::RebaseAction::Drop) {
            firstActive = row;
            break;
        }
    }
    if (firstActive >= 0) {
        const Git::RebaseAction firstAction = _plan.items.at(firstActive).action;
        if (firstAction == Git::RebaseAction::Squash
            || firstAction == Git::RebaseAction::Fixup) {
            valid = false;
            message = QStringLiteral(
                "The first non-drop commit cannot use squash or fixup.");
        }
    }

    if (valid) {
        for (const Git::RebasePlanItem& item : std::as_const(_plan.items)) {
            if (editsMessage(item.action)
                && item.rewrittenMessage.trimmed().isEmpty()) {
                valid = false;
                message = QStringLiteral(
                    "Reword and squash commits require a non-empty message.");
                break;
            }
        }
    }

    _planValid = valid;
    _validationMessage = message;
    _validationLabel->setText(message);
    _validationLabel->setVisible(!valid);
    emit sigValidationChanged(valid, message);
}

QString InteractiveRebaseWidget::effectiveMessage(
    const Git::RebasePlanItem& item) const
{
    if (!item.rewrittenMessage.isEmpty())
        return item.rewrittenMessage;
    if (!item.message.isEmpty())
        return item.message;
    return item.subject;
}
