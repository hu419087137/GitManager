#ifndef LFSDIALOG_H
#define LFSDIALOG_H

#include "../core/GitTypes.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class LfsDialog : public QDialog {
    Q_OBJECT
public:
    explicit LfsDialog(const Git::LfsState& state,
                       const QString& error = {}, QWidget* parent = nullptr);

signals:
    void sigTrackRequested(const QString& pattern);
    void sigUntrackRequested(const QString& pattern);
    void sigLockRequested(const QString& path);
    void sigUnlockRequested(const QString& path, bool force);

private slots:
    void updateActions();
    void slotTrack();
    void slotUntrack();
    void slotLock();
    void slotUnlock();

private:
    Git::LfsState _state;
    QListWidget* _patterns {nullptr};
    QListWidget* _locks {nullptr};
    QLineEdit* _patternEdit {nullptr};
    QLineEdit* _lockPathEdit {nullptr};
    QCheckBox* _forceUnlock {nullptr};
    QPushButton* _trackButton {nullptr};
    QPushButton* _untrackButton {nullptr};
    QPushButton* _lockButton {nullptr};
    QPushButton* _unlockButton {nullptr};
};

#endif // LFSDIALOG_H
