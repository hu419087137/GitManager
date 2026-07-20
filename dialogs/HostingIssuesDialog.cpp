#include "HostingIssuesDialog.h"
#include "../core/HostingService.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

HostingIssuesDialog::HostingIssuesDialog(
    const Git::HostingRemoteInfo& remote,
    const QVector<Git::HostingIssueInfo>& issues,
    const QString& error, QWidget* parent)
    : QDialog(parent), _issues(issues)
{
    setWindowTitle(QStringLiteral("%1 Issues").arg(
        Git::HostingService::providerName(remote.provider)));
    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("hostingIssuesList"));
    for (int index = 0; index < _issues.size(); ++index) {
        const auto& issue = _issues.at(index);
        auto* item = new QListWidgetItem(
            QStringLiteral("#%1  %2  [%3] — %4")
                .arg(issue.id, issue.title, issue.state, issue.author), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(issue.webUrl);
    }
    auto* message = new QLabel(error.isEmpty()
        ? QStringLiteral("%1 open issue(s).").arg(_issues.size()) : error, this);
    message->setWordWrap(true);
    _openButton = new QPushButton(QStringLiteral("Open Issue"), this);
    _openButton->setObjectName(QStringLiteral("hostingOpenIssueButton"));
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(_list, 1);
    layout->addWidget(_openButton);
    layout->addWidget(close);
    connect(_list, &QListWidget::currentRowChanged, this, &HostingIssuesDialog::updateActions);
    connect(_openButton, &QPushButton::clicked, this, &HostingIssuesDialog::slotOpen);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateActions();
}
void HostingIssuesDialog::updateActions() { _openButton->setEnabled(_list->currentItem()); }
void HostingIssuesDialog::slotOpen()
{
    const auto* item = _list->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < _issues.size())
        emit sigOpenRequested(_issues.at(index).webUrl);
}
