#include "HostingChangesDialog.h"
#include "../core/HostingService.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

HostingChangesDialog::HostingChangesDialog(
    const Git::HostingRemoteInfo& remote,
    const QVector<Git::HostingChangeInfo>& changes,
    const QString& error, QWidget* parent)
    : QDialog(parent), _changes(changes), _remote(remote)
{
    setWindowTitle(QStringLiteral("%1 Reviews").arg(
        Git::HostingService::providerName(remote.provider)));
    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("hostingChangesList"));
    for (int index = 0; index < _changes.size(); ++index) {
        const auto& change = _changes.at(index);
        auto* item = new QListWidgetItem(
            QStringLiteral("#%1  %2  [%3] — %4")
                .arg(change.id, change.title, change.state, change.author), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(change.webUrl);
    }
    auto* message = new QLabel(this);
    message->setText(error.isEmpty()
        ? QStringLiteral("%1 open review(s).").arg(_changes.size()) : error);
    message->setWordWrap(true);
    _openButton = new QPushButton(QStringLiteral("Open Review"), this);
    _openButton->setObjectName(QStringLiteral("hostingOpenChangeButton"));
    _reviewButton = new QPushButton(QStringLiteral("Review Files"), this);
    _reviewButton->setObjectName(QStringLiteral("hostingReviewFilesButton"));
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(_list, 1);
    layout->addWidget(_openButton);
    layout->addWidget(_reviewButton);
    layout->addWidget(close);
    connect(_list, &QListWidget::currentRowChanged, this, &HostingChangesDialog::updateActions);
    connect(_openButton, &QPushButton::clicked, this, &HostingChangesDialog::slotOpen);
    connect(_reviewButton, &QPushButton::clicked, this, &HostingChangesDialog::slotReviewFiles);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateActions();
}
void HostingChangesDialog::updateActions()
{
    _openButton->setEnabled(_list->currentItem());
    _reviewButton->setEnabled(_list->currentItem());
}
void HostingChangesDialog::slotOpen()
{
    const auto* item = _list->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < _changes.size())
        emit sigOpenRequested(_changes.at(index).webUrl);
}
void HostingChangesDialog::slotReviewFiles()
{
    const auto* item = _list->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < _changes.size())
        emit sigReviewFilesRequested(_remote, _changes.at(index));
}
