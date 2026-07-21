#include "CompareWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>

CompareWidget::CompareWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* title = new QLabel(QStringLiteral("Compare"), this);

    _baseCombo = new QComboBox(this);
    _baseCombo->setObjectName(QStringLiteral("compareBaseCombo"));
    _baseCombo->setAccessibleName(QStringLiteral("Base revision"));
    _baseCombo->setEditable(true);
    _baseCombo->setInsertPolicy(QComboBox::NoInsert);
    _baseCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _baseCombo->setMinimumContentsLength(18);
    _baseCombo->setToolTip(QStringLiteral("Base revision: branch name, tag, or commit hash"));

    _targetCombo = new QComboBox(this);
    _targetCombo->setObjectName(QStringLiteral("compareTargetCombo"));
    _targetCombo->setAccessibleName(QStringLiteral("Target revision"));
    _targetCombo->setEditable(true);
    _targetCombo->setInsertPolicy(QComboBox::NoInsert);
    _targetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _targetCombo->setMinimumContentsLength(18);
    _targetCombo->setToolTip(QStringLiteral("Target revision: branch name, tag, or commit hash"));

    auto* swapButton = new QToolButton(this);
    swapButton->setObjectName(QStringLiteral("compareSwapButton"));
    swapButton->setAccessibleName(QStringLiteral("Swap revisions"));
    swapButton->setText(QStringLiteral("Swap"));
    swapButton->setToolTip(QStringLiteral("Swap base and target revisions"));

    _compareButton = new QPushButton(QStringLiteral("Show Diff"), this);
    _compareButton->setObjectName(QStringLiteral("compareRunButton"));
    _compareButton->setAccessibleName(QStringLiteral("Compare revisions"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(title);
    layout->addWidget(_baseCombo, 1);
    layout->addWidget(_targetCombo, 1);
    layout->addWidget(swapButton);
    layout->addWidget(_compareButton);

    const auto update = [this] { updateButtonState(); };
    connect(_baseCombo->lineEdit(), &QLineEdit::textChanged, this, update);
    connect(_targetCombo->lineEdit(), &QLineEdit::textChanged, this, update);
    connect(_baseCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateButtonState(); });
    connect(_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateButtonState(); });
    connect(swapButton, &QToolButton::clicked, this, [this] {
        const QString base = baseRevision();
        const QString target = targetRevision();
        setBaseRevision(target);
        setTargetRevision(base);
    });
    connect(_compareButton, &QPushButton::clicked, this, [this] {
        const QString base = baseRevision();
        const QString target = targetRevision();
        if (!base.isEmpty() && !target.isEmpty() && base != target)
            emit sigCompareRequested(base, target);
    });

    updateButtonState();
}

void CompareWidget::setBranches(const QVector<Git::Branch>& branches)
{
    const QString base = baseRevision();
    const QString target = targetRevision();
    {
        const QSignalBlocker baseBlocker(_baseCombo);
        const QSignalBlocker targetBlocker(_targetCombo);
        _baseCombo->clear();
        _targetCombo->clear();

        QSet<QString> added;
        for (const Git::Branch& branch : branches) {
            const QString revision = branch.fullName.isEmpty() ? branch.name : branch.fullName;
            if (revision.isEmpty() || added.contains(revision))
                continue;
            added.insert(revision);

            QString label = branch.name;
            if (branch.isCurrent)
                label = QStringLiteral("* %1").arg(label);
            else if (branch.isRemote)
                label = QStringLiteral("Remote: %1").arg(label);
            const QString toolTip = branch.fullName.isEmpty() ? branch.name : branch.fullName;
            addRevisionItem(_baseCombo, label, revision, toolTip);
            addRevisionItem(_targetCombo, label, revision, toolTip);
        }
    }
    setBaseRevision(base);
    setTargetRevision(target);
    updateButtonState();
}

void CompareWidget::setBaseRevision(const QString& revision)
{
    setRevision(_baseCombo, revision);
}

void CompareWidget::setTargetRevision(const QString& revision)
{
    setRevision(_targetCombo, revision);
}

QString CompareWidget::baseRevision() const
{
    return _baseCombo->currentText().trimmed();
}

QString CompareWidget::targetRevision() const
{
    return _targetCombo->currentText().trimmed();
}

void CompareWidget::updateButtonState()
{
    const QString base = baseRevision();
    const QString target = targetRevision();
    _compareButton->setEnabled(!base.isEmpty()
                               && !target.isEmpty()
                               && base != target);
}

void CompareWidget::addRevisionItem(QComboBox* combo, const QString& label,
                                    const QString& revision,
                                    const QString& toolTip)
{
    combo->addItem(label, revision);
    combo->setItemData(combo->count() - 1, revision, Qt::UserRole);
    combo->setItemData(combo->count() - 1, toolTip, Qt::ToolTipRole);
}

void CompareWidget::setRevision(QComboBox* combo, const QString& revision)
{
    const QString trimmed = revision.trimmed();
    const QSignalBlocker blocker(combo);
    int index = combo->findData(trimmed, Qt::UserRole);
    if (index < 0)
        index = combo->findData(trimmed);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        combo->setEditText(trimmed);
    } else {
        combo->setCurrentIndex(-1);
        combo->setEditText(trimmed);
    }
    updateButtonState();
}
