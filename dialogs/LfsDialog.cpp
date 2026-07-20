#include "LfsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

LfsDialog::LfsDialog(const Git::LfsState& state, const QString& error,
                     QWidget* parent)
    : QDialog(parent), _state(state)
{
    setWindowTitle(QStringLiteral("Git LFS"));
    resize(760, 460);

    auto* status = new QLabel(this);
    status->setObjectName(QStringLiteral("lfsStatusLabel"));
    if (!_state.installed)
        status->setText(error.isEmpty() ? QStringLiteral("Git LFS is not installed.") : error);
    else
        status->setText(_state.version);

    _patterns = new QListWidget(this);
    _patterns->setObjectName(QStringLiteral("lfsPatternList"));
    _patterns->addItems(_state.trackedPatterns);
    _locks = new QListWidget(this);
    _locks->setObjectName(QStringLiteral("lfsLockList"));
    for (int index = 0; index < _state.locks.size(); ++index) {
        const auto& lock = _state.locks.at(index);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2").arg(lock.path, lock.owner), _locks);
        item->setData(Qt::UserRole, index);
        item->setToolTip(lock.lockedAt);
    }

    _patternEdit = new QLineEdit(this);
    _patternEdit->setObjectName(QStringLiteral("lfsPatternEdit"));
    _patternEdit->setPlaceholderText(QStringLiteral("Pattern, e.g. *.psd"));
    _lockPathEdit = new QLineEdit(this);
    _lockPathEdit->setObjectName(QStringLiteral("lfsLockPathEdit"));
    _lockPathEdit->setPlaceholderText(QStringLiteral("Repository-relative file path"));
    _forceUnlock = new QCheckBox(QStringLiteral("Force unlock another user's lock"), this);
    _forceUnlock->setObjectName(QStringLiteral("lfsForceUnlockCheck"));

    _trackButton = new QPushButton(QStringLiteral("Track"), this);
    _trackButton->setObjectName(QStringLiteral("lfsTrackButton"));
    _untrackButton = new QPushButton(QStringLiteral("Untrack Selected"), this);
    _untrackButton->setObjectName(QStringLiteral("lfsUntrackButton"));
    _lockButton = new QPushButton(QStringLiteral("Lock"), this);
    _lockButton->setObjectName(QStringLiteral("lfsLockButton"));
    _unlockButton = new QPushButton(QStringLiteral("Unlock Selected"), this);
    _unlockButton->setObjectName(QStringLiteral("lfsUnlockButton"));

    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(QStringLiteral("Tracked patterns:"), this));
    left->addWidget(_patterns, 1);
    left->addWidget(_patternEdit);
    auto* patternButtons = new QHBoxLayout;
    patternButtons->addWidget(_trackButton);
    patternButtons->addWidget(_untrackButton);
    left->addLayout(patternButtons);

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(QStringLiteral("Remote locks:"), this));
    right->addWidget(_locks, 1);
    if (!_state.locksError.isEmpty()) {
        auto* lockError = new QLabel(_state.locksError, this);
        lockError->setWordWrap(true);
        right->addWidget(lockError);
    }
    right->addWidget(_lockPathEdit);
    right->addWidget(_forceUnlock);
    auto* lockButtons = new QHBoxLayout;
    lockButtons->addWidget(_lockButton);
    lockButtons->addWidget(_unlockButton);
    right->addLayout(lockButtons);

    auto* content = new QHBoxLayout;
    content->addLayout(left, 1);
    content->addLayout(right, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(status);
    layout->addLayout(content, 1);
    layout->addWidget(buttons);

    connect(_patterns, &QListWidget::currentRowChanged, this, &LfsDialog::updateActions);
    connect(_locks, &QListWidget::currentRowChanged, this, &LfsDialog::updateActions);
    connect(_patternEdit, &QLineEdit::textChanged, this, &LfsDialog::updateActions);
    connect(_lockPathEdit, &QLineEdit::textChanged, this, &LfsDialog::updateActions);
    connect(_trackButton, &QPushButton::clicked, this, &LfsDialog::slotTrack);
    connect(_untrackButton, &QPushButton::clicked, this, &LfsDialog::slotUntrack);
    connect(_lockButton, &QPushButton::clicked, this, &LfsDialog::slotLock);
    connect(_unlockButton, &QPushButton::clicked, this, &LfsDialog::slotUnlock);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateActions();
}

void LfsDialog::updateActions()
{
    const bool available = _state.installed;
    _trackButton->setEnabled(available && !_patternEdit->text().trimmed().isEmpty());
    _untrackButton->setEnabled(available && _patterns->currentItem());
    _lockButton->setEnabled(available && !_lockPathEdit->text().trimmed().isEmpty());
    _unlockButton->setEnabled(available && _locks->currentItem());
}

void LfsDialog::slotTrack() { emit sigTrackRequested(_patternEdit->text().trimmed()); }
void LfsDialog::slotUntrack()
{
    if (_patterns->currentItem()) emit sigUntrackRequested(_patterns->currentItem()->text());
}
void LfsDialog::slotLock() { emit sigLockRequested(_lockPathEdit->text().trimmed()); }
void LfsDialog::slotUnlock()
{
    const auto* item = _locks->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < _state.locks.size())
        emit sigUnlockRequested(_state.locks.at(index).path, _forceUnlock->isChecked());
}
