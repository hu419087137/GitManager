#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QPlainTextEdit>

class QContextMenuEvent;

/**
 * @brief 底部 Git 输出面板（只读）
 *
 * 以仿终端样式显示每条被执行的 git 命令及其输出。
 * 写操作（pull/push/commit 等）展示完整输出；
 * 只读查询（log/status 等）仅显示命令行。
 */
class TerminalWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget* parent = nullptr);

public slots:
    void beginCommand(const QString& command);
    void appendOutput(const QString& output, bool standardError);
    /**
     * @brief 追加一条 git 命令记录（由 GitManager::sigCommandRun 驱动）
     * @param command  完整命令字符串，如 "git pull --rebase"
     * @param output   命令输出（空字符串则不显示输出块）
     * @param success  是否成功（影响输出着色）
     */
    void appendCommand(const QString& command, const QString& output, bool success);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
};

#endif // TERMINALWIDGET_H
