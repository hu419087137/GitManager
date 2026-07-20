#ifndef HOSTINGCHANGESDIALOG_H
#define HOSTINGCHANGESDIALOG_H

#include "../core/GitTypes.h"
#include <QDialog>

class QListWidget;
class QPushButton;

class HostingChangesDialog : public QDialog {
    Q_OBJECT
public:
    explicit HostingChangesDialog(const Git::HostingRemoteInfo& remote,
                                  const QVector<Git::HostingChangeInfo>& changes,
                                  const QString& error = {},
                                  QWidget* parent = nullptr);
signals:
    void sigOpenRequested(const QString& url);
    void sigReviewFilesRequested(const Git::HostingRemoteInfo& remote,
                                 const Git::HostingChangeInfo& change);
private slots:
    void updateActions();
    void slotOpen();
    void slotReviewFiles();
private:
    QVector<Git::HostingChangeInfo> _changes;
    Git::HostingRemoteInfo _remote;
    QListWidget* _list {nullptr};
    QPushButton* _openButton {nullptr};
    QPushButton* _reviewButton {nullptr};
};

#endif // HOSTINGCHANGESDIALOG_H
