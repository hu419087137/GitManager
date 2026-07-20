#include "HostingDialog.h"
#include "../core/HostingService.h"

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

HostingDialog::HostingDialog(const QVector<Git::HostingRemoteInfo>& remotes,
                             const QString& error,
                             const QHash<int, QString>& savedTokens,
                             QWidget* parent)
    : QDialog(parent), _remotes(remotes), _savedTokens(savedTokens)
{
    setWindowTitle(QStringLiteral("Repository Hosting"));
    resize(820, 460);

    auto* message = new QLabel(this);
    message->setWordWrap(true);
    message->setText(error.isEmpty()
        ? QStringLiteral("Open this repository in its hosting provider.") : error);
    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("hostingRemoteList"));
    for (int index = 0; index < _remotes.size(); ++index) {
        const auto& remote = _remotes.at(index);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 — %2\n%3")
                .arg(remote.remoteName,
                     Git::HostingService::providerName(remote.provider),
                     remote.webUrl), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(remote.sourceUrl);
    }
    if (_remotes.isEmpty() && error.isEmpty())
        message->setText(QStringLiteral(
            "No GitHub, GitLab, or Azure DevOps remote was recognized."));

    _repositoryButton = new QPushButton(QStringLiteral("Repository"), this);
    _repositoryButton->setObjectName(QStringLiteral("hostingRepositoryButton"));
    _commitButton = new QPushButton(QStringLiteral("Current Commit"), this);
    _commitButton->setObjectName(QStringLiteral("hostingCommitButton"));
    _changeButton = new QPushButton(QStringLiteral("Create PR / MR"), this);
    _changeButton->setObjectName(QStringLiteral("hostingChangeButton"));
    _changesButton = new QPushButton(QStringLiteral("PR / MR Reviews"), this);
    _changesButton->setObjectName(QStringLiteral("hostingChangesButton"));
    _issuesButton = new QPushButton(QStringLiteral("Issues / Work Items"), this);
    _issuesButton->setObjectName(QStringLiteral("hostingIssuesButton"));
    _tokenEdit = new QLineEdit(this);
    _tokenEdit->setObjectName(QStringLiteral("hostingTokenEdit"));
    _tokenEdit->setEchoMode(QLineEdit::Password);
    _tokenEdit->setPlaceholderText(QStringLiteral("Session access token (not saved)"));
    _rememberToken = new QCheckBox(QStringLiteral("Remember in Windows Credential Manager"), this);
    _rememberToken->setObjectName(QStringLiteral("hostingRememberTokenCheck"));
    _forgetTokenButton = new QPushButton(QStringLiteral("Forget Saved Token"), this);
    _forgetTokenButton->setObjectName(QStringLiteral("hostingForgetTokenButton"));
    _loadChangesButton = new QPushButton(QStringLiteral("Load API Reviews"), this);
    _loadChangesButton->setObjectName(QStringLiteral("hostingLoadChangesButton"));
    _loadIssuesButton = new QPushButton(QStringLiteral("Load API Issues"), this);
    _loadIssuesButton->setObjectName(QStringLiteral("hostingLoadIssuesButton"));

    auto* actions = new QGridLayout;
    actions->addWidget(_repositoryButton, 0, 0);
    actions->addWidget(_commitButton, 0, 1);
    actions->addWidget(_changesButton, 0, 2);
    actions->addWidget(_issuesButton, 0, 3);
    actions->addWidget(_changeButton, 1, 0);
    actions->addWidget(_loadChangesButton, 1, 1);
    actions->addWidget(_loadIssuesButton, 1, 2);
    actions->setColumnStretch(3, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(_tokenEdit);
    auto* credentialRow = new QHBoxLayout;
    credentialRow->addWidget(_rememberToken);
    credentialRow->addWidget(_forgetTokenButton);
    layout->addLayout(credentialRow);
    layout->addWidget(_list, 1);
    layout->addLayout(actions);
    layout->addWidget(buttons);

    connect(_list, &QListWidget::currentRowChanged, this, &HostingDialog::updateActions);
    connect(_tokenEdit, &QLineEdit::textChanged, this, &HostingDialog::updateActions);
    connect(_repositoryButton, &QPushButton::clicked, this, &HostingDialog::slotOpenRepository);
    connect(_commitButton, &QPushButton::clicked, this, &HostingDialog::slotOpenCommit);
    connect(_changeButton, &QPushButton::clicked, this, &HostingDialog::slotCreateChange);
    connect(_changesButton, &QPushButton::clicked, this, &HostingDialog::slotOpenChanges);
    connect(_issuesButton, &QPushButton::clicked, this, &HostingDialog::slotOpenIssues);
    connect(_loadChangesButton, &QPushButton::clicked, this, &HostingDialog::slotLoadChanges);
    connect(_loadIssuesButton, &QPushButton::clicked, this, &HostingDialog::slotLoadIssues);
    connect(_forgetTokenButton, &QPushButton::clicked, this, &HostingDialog::slotForgetToken);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateActions();
}

const Git::HostingRemoteInfo* HostingDialog::selected() const
{
    const auto* item = _list->currentItem();
    if (!item) return nullptr;
    const int index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < _remotes.size() ? &_remotes.at(index) : nullptr;
}

void HostingDialog::updateActions()
{
    const auto* remote = selected();
    const int provider = remote ? static_cast<int>(remote->provider) : -1;
    if (provider != _currentProvider) {
        _currentProvider = provider;
        _tokenEdit->setText(_savedTokens.value(provider));
        _rememberToken->setChecked(_savedTokens.contains(provider));
    }
    _repositoryButton->setEnabled(remote && !remote->webUrl.isEmpty());
    _commitButton->setEnabled(remote && !remote->commitUrl.isEmpty());
    _changeButton->setEnabled(remote && !remote->createChangeUrl.isEmpty());
    _changesButton->setEnabled(remote && !remote->changesUrl.isEmpty());
    _issuesButton->setEnabled(remote && !remote->issuesUrl.isEmpty());
    _loadChangesButton->setEnabled(remote && !remote->webUrl.isEmpty()
                                   && !_tokenEdit->text().isEmpty());
    _loadIssuesButton->setEnabled(remote && !remote->webUrl.isEmpty()
                                  && !_tokenEdit->text().isEmpty());
    _forgetTokenButton->setEnabled(remote && _savedTokens.contains(provider));
}

void HostingDialog::open(const QString& url) { if (!url.isEmpty()) emit sigOpenUrlRequested(url); }
void HostingDialog::slotOpenRepository() { if (selected()) open(selected()->webUrl); }
void HostingDialog::slotOpenCommit() { if (selected()) open(selected()->commitUrl); }
void HostingDialog::slotCreateChange() { if (selected()) open(selected()->createChangeUrl); }
void HostingDialog::slotOpenChanges() { if (selected()) open(selected()->changesUrl); }
void HostingDialog::slotOpenIssues() { if (selected()) open(selected()->issuesUrl); }
void HostingDialog::slotLoadChanges()
{
    if (selected()) {
        if (_rememberToken->isChecked())
            emit sigStoreTokenRequested(selected()->provider,
                                        _tokenEdit->text());
        emit sigLoadChangesRequested(*selected(), _tokenEdit->text());
    }
}
void HostingDialog::slotLoadIssues()
{
    if (selected()) {
        if (_rememberToken->isChecked())
            emit sigStoreTokenRequested(selected()->provider,
                                        _tokenEdit->text());
        emit sigLoadIssuesRequested(*selected(), _tokenEdit->text());
    }
}
void HostingDialog::slotForgetToken()
{
    if (!selected()) return;
    const int provider = static_cast<int>(selected()->provider);
    _savedTokens.remove(provider);
    _tokenEdit->clear();
    _rememberToken->setChecked(false);
    emit sigForgetTokenRequested(selected()->provider);
    updateActions();
}
