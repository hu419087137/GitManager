#include "GitDiagnosticsDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

GitDiagnosticsDialog::GitDiagnosticsDialog(
    const Git::GitDiagnosticReport& report, const QString& error, QWidget* parent)
    : QDialog(parent), _remotes(report.remotes)
{
    setWindowTitle(QStringLiteral("Git Connection Diagnostics"));
    resize(820, 520);
    auto* message = new QLabel(error, this);
    message->setWordWrap(true);
    message->setVisible(!error.isEmpty());
    _tree = new QTreeWidget(this);
    _tree->setObjectName(QStringLiteral("gitDiagnosticsTree"));
    _tree->setHeaderLabels({QStringLiteral("Category"), QStringLiteral("Setting"),
                            QStringLiteral("Value")});
    for (const auto& item : report.items) {
        auto* row = new QTreeWidgetItem(_tree,
            {item.category, item.name, item.value});
        if (item.warning)
            row->setForeground(2, QBrush(QColor(QStringLiteral("#cf222e"))));
    }
    _tree->resizeColumnToContents(0);
    _tree->resizeColumnToContents(1);
    _remoteCombo = new QComboBox(this);
    _remoteCombo->setObjectName(QStringLiteral("diagnosticRemoteCombo"));
    for (int index = 0; index < _remotes.size(); ++index) {
        const auto& remote = _remotes.at(index);
        _remoteCombo->addItem(remote.name, index);
    }
    _testButton = new QPushButton(QStringLiteral("Test Selected Remote"), this);
    _testButton->setObjectName(QStringLiteral("diagnosticTestRemoteButton"));
    _testButton->setEnabled(!_remotes.isEmpty());
    auto* remoteRow = new QHBoxLayout;
    remoteRow->addWidget(_remoteCombo, 1);
    remoteRow->addWidget(_testButton);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(_tree, 1);
    layout->addLayout(remoteRow);
    layout->addWidget(buttons);
    connect(_testButton, &QPushButton::clicked, this, &GitDiagnosticsDialog::slotTestRemote);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void GitDiagnosticsDialog::slotTestRemote()
{
    const int index = _remoteCombo->currentData().toInt();
    if (index < 0 || index >= _remotes.size()) return;
    const auto& remote = _remotes.at(index);
    emit sigTestRemoteRequested(remote.fetchUrl.isEmpty()
        ? remote.pushUrl : remote.fetchUrl);
}
