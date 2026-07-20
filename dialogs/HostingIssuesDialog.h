#ifndef HOSTINGISSUESDIALOG_H
#define HOSTINGISSUESDIALOG_H
#include "../core/GitTypes.h"
#include <QDialog>
class QListWidget;
class QPushButton;
class HostingIssuesDialog : public QDialog {
    Q_OBJECT
public:
    explicit HostingIssuesDialog(const Git::HostingRemoteInfo& remote,
                                 const QVector<Git::HostingIssueInfo>& issues,
                                 const QString& error = {}, QWidget* parent = nullptr);
signals:
    void sigOpenRequested(const QString& url);
private slots:
    void updateActions();
    void slotOpen();
private:
    QVector<Git::HostingIssueInfo> _issues;
    QListWidget* _list {nullptr};
    QPushButton* _openButton {nullptr};
};
#endif // HOSTINGISSUESDIALOG_H
