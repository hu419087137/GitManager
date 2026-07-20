#ifndef WORKTREEDIALOG_H
#define WORKTREEDIALOG_H

#include "../core/GitTypes.h"
#include <QDialog>

class QListWidget;
class QLineEdit;
class QPushButton;

class WorktreeDialog : public QDialog {
    Q_OBJECT

public:
    explicit WorktreeDialog(const QVector<Git::WorktreeInfo>& worktrees,
                            QWidget* parent = nullptr);

signals:
    void sigOpenRequested(const QString& path);
    void sigCreateRequested(const QString& name, const QString& path,
                            const QString& branchName);
    void sigMoveRequested(const QString& name, const QString& path);
    void sigLockRequested(const QString& name, bool locked,
                          const QString& reason);
    void sigRemoveRequested(const QString& name, bool force);

private slots:
    void updateSelectionState();
    void slotCreate();
    void slotOpen();
    void slotMove();
    void slotToggleLock();
    void slotRemove();

private:
    QVector<Git::WorktreeInfo> _worktrees;
    QListWidget* _list {nullptr};
    QLineEdit* _nameEdit {nullptr};
    QLineEdit* _pathEdit {nullptr};
    QLineEdit* _branchEdit {nullptr};
    QLineEdit* _movePathEdit {nullptr};
    QLineEdit* _lockReasonEdit {nullptr};
    QPushButton* _openButton {nullptr};
    QPushButton* _moveButton {nullptr};
    QPushButton* _lockButton {nullptr};
    QPushButton* _removeButton {nullptr};
    QPushButton* _createButton {nullptr};
};

#endif // WORKTREEDIALOG_H
