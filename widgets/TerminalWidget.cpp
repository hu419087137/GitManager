#include "TerminalWidget.h"
#include <QScrollBar>
#include <QDateTime>

TerminalWidget::TerminalWidget(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setMaximumBlockCount(5000);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    QFont mono(QStringLiteral("Consolas"), 9);
    mono.setStyleHint(QFont::Monospace);
    setFont(mono);

    setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: #0d1117;"
        "  color: #c9d1d9;"
        "  border: none;"
        "  border-top: 1px solid #30363d;"
        "}"));
}

void TerminalWidget::appendCommand(const QString& command,
                                   const QString& output,
                                   bool           success)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    // 时间戳（灰色）
    QTextCharFormat tsFmt;
    tsFmt.setForeground(QColor("#8b949e"));
    cursor.setCharFormat(tsFmt);
    cursor.insertText(QStringLiteral("[%1] ").arg(ts));

    // "$ " 提示符（绿色加粗）
    QTextCharFormat promptFmt;
    promptFmt.setForeground(QColor("#3fb950"));
    promptFmt.setFontWeight(QFont::Bold);
    cursor.setCharFormat(promptFmt);
    cursor.insertText(QStringLiteral("$ "));

    // 命令文本（亮白）
    QTextCharFormat cmdFmt;
    cmdFmt.setForeground(QColor("#e6edf3"));
    cursor.setCharFormat(cmdFmt);
    cursor.insertText(command + QStringLiteral("\n"));

    // 输出块（成功=灰色，失败=红色）
    const QString trimmed = output.trimmed();
    if (!trimmed.isEmpty()) {
        QTextCharFormat outFmt;
        outFmt.setForeground(success ? QColor("#8b949e") : QColor("#f85149"));
        cursor.setCharFormat(outFmt);
        cursor.insertText(trimmed + QStringLiteral("\n"));
    }

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}
