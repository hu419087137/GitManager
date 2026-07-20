#ifndef SUBMODULEDIALOG_H
#define SUBMODULEDIALOG_H

#include "../core/GitTypes.h"
#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;

class SubmoduleDialog : public QDialog {
    Q_OBJECT

public:
    explicit SubmoduleDialog(const QVector<Git::SubmoduleInfo>& submodules,
                             QWidget* parent = nullptr);

signals:
    void sigOpenRequested(const QString& path);
    void sigAddRequested(const QString& url, const QString& path);
    void sigUpdateRequested(const QString& name);
    void sigSyncRequested(const QString& name);
    void sigBranchRequested(const QString& name, const QString& branch);
    void sigRemoveRequested(const QString& name, bool force);

private slots:
    void updateActions();
    void slotOpen();
    void slotAdd();
    void slotUpdate();
    void slotSync();
    void slotSetBranch();
    void slotRemove();

private:
    const Git::SubmoduleInfo* selectedSubmodule() const;

    QVector<Git::SubmoduleInfo> _submodules;
    QListWidget* _list {nullptr};
    QLineEdit* _urlEdit {nullptr};
    QLineEdit* _pathEdit {nullptr};
    QLineEdit* _branchEdit {nullptr};
    QPushButton* _openButton {nullptr};
    QPushButton* _updateButton {nullptr};
    QPushButton* _syncButton {nullptr};
    QPushButton* _addButton {nullptr};
    QPushButton* _branchButton {nullptr};
    QPushButton* _removeButton {nullptr};
};

#endif // SUBMODULEDIALOG_H
