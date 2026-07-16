#ifndef COMMITDIALOG_H
#define COMMITDIALOG_H

#include <QDialog>

class QTextEdit;
class QPushButton;
class QLabel;
class QCheckBox;

/**
 * @brief 提交信息输入对话框
 *
 * 显示已暂存文件摘要，供用户输入提交消息并执行提交。
 */
class CommitDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommitDialog(const QStringList& stagedFiles, QWidget* parent = nullptr);

    /** @brief 返回用户输入的提交消息 */
    QString commitMessage() const;
    bool amend() const;
    bool signoff() const;

private slots:
    void slotTextChanged();

private:
    QTextEdit*   _messageEdit  {nullptr};
    QPushButton* _commitBtn    {nullptr};
    QLabel*      _filesLabel   {nullptr};
    QLabel*      _countLabel   {nullptr};
    QCheckBox*   _amendCheck   {nullptr};
    QCheckBox*   _signoffCheck {nullptr};
};

#endif // COMMITDIALOG_H
