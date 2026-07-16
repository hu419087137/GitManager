#ifndef DIFFWIDGET_H
#define DIFFWIDGET_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

class DiffHighlighter;
class QContextMenuEvent;

/**
 * @brief 差异内容展示控件
 *
 * 使用等宽字体显示 git diff 或 git show 输出，
 * 并通过 DiffHighlighter 对增删行着色。
 */
class DiffWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    enum Action {
        NoAction = 0,
        StageAction = 1,
        UnstageAction = 2,
        DiscardAction = 4
    };

    explicit DiffWidget(QWidget* parent = nullptr);

    /** @brief 显示 diff 文本 */
    void setDiff(const QString& diffText, int actions = NoAction);

    /** @brief 清空内容 */
    void clearDiff();

signals:
    void sigHunkActionRequested(const QString& patch, int action);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    DiffHighlighter* _highlighter {nullptr};
    int _actions {NoAction};
};

// ============================================================
// 语法高亮：对 diff 输出的增删行和文件头分别着色
// ============================================================

class DiffHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit DiffHighlighter(QTextDocument* parent);

protected:
    void highlightBlock(const QString& text) override;
};

#endif // DIFFWIDGET_H
