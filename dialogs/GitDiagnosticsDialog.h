#ifndef GITDIAGNOSTICSDIALOG_H
#define GITDIAGNOSTICSDIALOG_H
#include "../core/GitTypes.h"
#include <QDialog>
class QComboBox;
class QTreeWidget;
class QPushButton;
class GitDiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    explicit GitDiagnosticsDialog(const Git::GitDiagnosticReport& report,
                                  const QString& error = {}, QWidget* parent = nullptr);
signals:
    void sigTestRemoteRequested(const QString& url);
private slots:
    void slotTestRemote();
private:
    QVector<Git::RemoteInfo> _remotes;
    QTreeWidget* _tree {nullptr};
    QComboBox* _remoteCombo {nullptr};
    QPushButton* _testButton {nullptr};
};
#endif // GITDIAGNOSTICSDIALOG_H
