#ifndef BRANCHDIALOG_H
#define BRANCHDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

class BranchDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        Create,
        Rename,
        Publish
    };

    explicit BranchDialog(Mode mode, QWidget* parent = nullptr);

    void setBranchName(const QString& name);
    void setSourceRevision(const QString& revision);
    void setRemoteName(const QString& remote);

    QString branchName() const;
    QString sourceRevision() const;
    QString remoteName() const;

private slots:
    void updateState();

private:
    Mode _mode;
    QLabel* _summaryLabel {nullptr};
    QLineEdit* _branchEdit {nullptr};
    QLineEdit* _sourceEdit {nullptr};
    QLineEdit* _remoteEdit {nullptr};
    QPushButton* _acceptButton {nullptr};
};

#endif // BRANCHDIALOG_H
