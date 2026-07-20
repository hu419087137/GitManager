#ifndef HOOKSDIALOG_H
#define HOOKSDIALOG_H
#include "../core/GitTypes.h"
#include <QDialog>
class QTableWidget;
class HooksDialog : public QDialog {
    Q_OBJECT
public:
    explicit HooksDialog(const QVector<Git::HookInfo>& hooks,
                         const QString& error = {}, QWidget* parent = nullptr);
};
#endif // HOOKSDIALOG_H
