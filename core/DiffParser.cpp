#include "DiffParser.h"
#include <QStringList>

namespace Git {

QString DiffParser::hunkAtLine(const QString& diff, int lineNumber)
{
    const QStringList lines = diff.split('\n');
    if (lineNumber < 0 || lineNumber >= lines.size()) return {};
    int fileStart = lineNumber;
    while (fileStart >= 0 && !lines[fileStart].startsWith(QStringLiteral("diff --git "))) --fileStart;
    if (fileStart < 0) return {};
    int hunkStart = lineNumber;
    while (hunkStart >= fileStart && !lines[hunkStart].startsWith(QStringLiteral("@@ "))) --hunkStart;
    if (hunkStart < fileStart) return {};
    int hunkEnd = hunkStart + 1;
    while (hunkEnd < lines.size() && !lines[hunkEnd].startsWith(QStringLiteral("@@ "))
           && !lines[hunkEnd].startsWith(QStringLiteral("diff --git "))) ++hunkEnd;
    QStringList patch;
    for (int i = fileStart; i < hunkStart; ++i) patch << lines[i];
    for (int i = hunkStart; i < hunkEnd; ++i) patch << lines[i];
    return patch.join('\n') + '\n';
}

} // namespace Git
