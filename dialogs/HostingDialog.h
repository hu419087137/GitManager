#ifndef HOSTINGDIALOG_H
#define HOSTINGDIALOG_H

#include "../core/GitTypes.h"
#include <QDialog>
#include <QHash>

class QListWidget;
class QLineEdit;
class QCheckBox;
class QPushButton;

class HostingDialog : public QDialog {
    Q_OBJECT
public:
    explicit HostingDialog(const QVector<Git::HostingRemoteInfo>& remotes,
                           const QString& error = {},
                           const QHash<int, QString>& savedTokens = {},
                           QWidget* parent = nullptr);

signals:
    void sigOpenUrlRequested(const QString& url);
    void sigLoadChangesRequested(const Git::HostingRemoteInfo& remote,
                                 const QString& token);
    void sigLoadIssuesRequested(const Git::HostingRemoteInfo& remote,
                                const QString& token);
    void sigStoreTokenRequested(Git::HostingProvider provider,
                                const QString& token);
    void sigForgetTokenRequested(Git::HostingProvider provider);

private slots:
    void updateActions();
    void slotOpenRepository();
    void slotOpenCommit();
    void slotCreateChange();
    void slotOpenChanges();
    void slotOpenIssues();
    void slotLoadChanges();
    void slotLoadIssues();
    void slotForgetToken();

private:
    const Git::HostingRemoteInfo* selected() const;
    void open(const QString& url);

    QVector<Git::HostingRemoteInfo> _remotes;
    QHash<int, QString> _savedTokens;
    int _currentProvider {-1};
    QListWidget* _list {nullptr};
    QLineEdit* _tokenEdit {nullptr};
    QCheckBox* _rememberToken {nullptr};
    QPushButton* _repositoryButton {nullptr};
    QPushButton* _commitButton {nullptr};
    QPushButton* _changeButton {nullptr};
    QPushButton* _changesButton {nullptr};
    QPushButton* _issuesButton {nullptr};
    QPushButton* _loadChangesButton {nullptr};
    QPushButton* _loadIssuesButton {nullptr};
    QPushButton* _forgetTokenButton {nullptr};
};

#endif // HOSTINGDIALOG_H
