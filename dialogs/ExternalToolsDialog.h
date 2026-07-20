#ifndef EXTERNALTOOLSDIALOG_H
#define EXTERNALTOOLSDIALOG_H

#include <QDialog>
class QLineEdit;

class ExternalToolsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExternalToolsDialog(QWidget* parent = nullptr);
private:
    QLineEdit* _diffEdit {nullptr};
    QLineEdit* _mergeEdit {nullptr};
};

#endif // EXTERNALTOOLSDIALOG_H
