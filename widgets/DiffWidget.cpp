#include "DiffWidget.h"
#include <QTextCharFormat>
#include <QTextDocument>
#include <QFont>
#include <QContextMenuEvent>
#include <QMenu>
#include "../core/DiffParser.h"

// ============================================================
// DiffWidget
// ============================================================

DiffWidget::DiffWidget(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    QFont mono(QStringLiteral("Consolas"), 9);
    mono.setStyleHint(QFont::Monospace);
    setFont(mono);

    _highlighter = new DiffHighlighter(document());
}

void DiffWidget::setDiff(const QString& diffText, int actions)
{
    _actions = actions;
    setPlainText(diffText);
    moveCursor(QTextCursor::Start);
}

void DiffWidget::clearDiff()
{
    _actions = NoAction;
    clear();
}

void DiffWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createStandardContextMenu();
    const QString patch = Git::DiffParser::hunkAtLine(toPlainText(), cursorForPosition(event->pos()).blockNumber());
    if (!patch.isEmpty()) {
        menu->addSeparator();
        if (_actions & StageAction)
            menu->addAction(QStringLiteral("Stage Hunk"), [this, patch] { emit sigHunkActionRequested(patch, 0); });
        if (_actions & UnstageAction)
            menu->addAction(QStringLiteral("Unstage Hunk"), [this, patch] { emit sigHunkActionRequested(patch, 1); });
        if (_actions & DiscardAction)
            menu->addAction(QStringLiteral("Discard Hunk"), [this, patch] { emit sigHunkActionRequested(patch, 2); });
    }
    menu->exec(event->globalPos());
    delete menu;
}

// ============================================================
// DiffHighlighter
// ============================================================

DiffHighlighter::DiffHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{}

void DiffHighlighter::highlightBlock(const QString& text)
{
    if (text.isEmpty())
        return;

    QTextCharFormat fmt;
    const QChar first = text[0];

    if (first == '+' && !text.startsWith("+++")) {
        fmt.setForeground(QColor("#3fb950")); // 新增行：绿色
        fmt.setBackground(QColor("#0d1f0d"));
        setFormat(0, text.length(), fmt);
    } else if (first == '-' && !text.startsWith("---")) {
        fmt.setForeground(QColor("#f85149")); // 删除行：红色
        fmt.setBackground(QColor("#1f0d0d"));
        setFormat(0, text.length(), fmt);
    } else if (first == '@') {
        fmt.setForeground(QColor("#79c0ff")); // 块头：蓝色
        setFormat(0, text.length(), fmt);
    } else if (text.startsWith("diff ") || text.startsWith("index ")
               || text.startsWith("---") || text.startsWith("+++")) {
        fmt.setForeground(QColor("#8b949e")); // 文件头：灰色
        setFormat(0, text.length(), fmt);
    } else if (text.startsWith("commit ") || text.startsWith("Author:")
               || text.startsWith("Date:")) {
        fmt.setForeground(QColor("#ffa657")); // 提交信息头：橙色
        setFormat(0, text.length(), fmt);
    }
}
