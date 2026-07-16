#ifndef DIFFWIDGET_H
#define DIFFWIDGET_H

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

class DiffHighlighter;

/**
 * @brief 差异内容展示控件
 *
 * 使用等宽字体显示 git diff 或 git show 输出，
 * 并通过 DiffHighlighter 对增删行着色。
 */
class DiffWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit DiffWidget(QWidget* parent = nullptr);

    /** @brief 显示 diff 文本 */
    void setDiff(const QString& diffText);

    /** @brief 清空内容 */
    void clearDiff();

private:
    DiffHighlighter* _highlighter {nullptr};
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
