#ifndef HOSTINGREVIEWDIALOG_H
#define HOSTINGREVIEWDIALOG_H
#include "../core/GitTypes.h"
#include <QDialog>
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QLineEdit;
class HostingReviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit HostingReviewDialog(const Git::HostingRemoteInfo& remote,
                                 const Git::HostingChangeInfo& change,
                                 const QVector<Git::HostingReviewFile>& files,
                                 const QString& error = {}, QWidget* parent = nullptr);
    static bool patchContainsNewLine(const QString& patch, int target);
signals:
    void sigOpenFileRequested(const QString& url);
    void sigCommentRequested(const Git::HostingRemoteInfo& remote,
                             const Git::HostingChangeInfo& change,
                             const Git::HostingReviewFile& file,
                             int line, const QString& body);
private slots:
    void updateFile();
    void slotOpenFile();
    void slotSubmitComment();
private:
    QVector<Git::HostingReviewFile> _files;
    Git::HostingRemoteInfo _remote;
    Git::HostingChangeInfo _change;
    QListWidget* _list {nullptr};
    QPlainTextEdit* _patch {nullptr};
    QPushButton* _openButton {nullptr};
    QLineEdit* _lineEdit {nullptr};
    QPlainTextEdit* _commentEdit {nullptr};
    QPushButton* _commentButton {nullptr};
};
#endif // HOSTINGREVIEWDIALOG_H
