#include "StatusParser.h"

namespace Git {
namespace {

File::Status statusFromChar(char value)
{
    switch (value) {
    case 'M': return File::Status::E_Modified;
    case 'A': return File::Status::E_Added;
    case 'D': return File::Status::E_Deleted;
    case 'R': return File::Status::E_Renamed;
    case 'C': return File::Status::E_Copied;
    case 'T': return File::Status::E_TypeChanged;
    case 'U': return File::Status::E_Unmerged;
    case '?': return File::Status::E_Untracked;
    case '!': return File::Status::E_Ignored;
    default:  return File::Status::E_Unmodified;
    }
}

QString utf8(const QByteArray& value) { return QString::fromUtf8(value); }

} // namespace

StatusSummary StatusParser::parse(const QByteArray& output)
{
    StatusSummary summary;
    const QList<QByteArray> records = output.split('\0');

    for (int i = 0; i < records.size(); ++i) {
        QByteArray record = records[i];
        if (record.isEmpty())
            continue;

        // -z 模式只用 NUL 分隔文件记录，branch headers 仍以换行分隔。
        while (record.startsWith("# ")) {
            const int newline = record.indexOf('\n');
            const QByteArray header = newline < 0 ? record : record.left(newline);
            record = newline < 0 ? QByteArray() : record.mid(newline + 1);
            if (header.startsWith("# branch.head ")) {
                summary.headName = utf8(header.mid(14));
                summary.detached = summary.headName == QStringLiteral("(detached)");
            } else if (header.startsWith("# branch.oid ")) {
                const QByteArray oid = header.mid(13);
                summary.unborn = oid == "(initial)";
                if (!summary.unborn) summary.headHash = utf8(oid);
            } else if (header.startsWith("# branch.upstream ")) {
                summary.upstream = utf8(header.mid(18));
            } else if (header.startsWith("# branch.ab ")) {
                for (const QByteArray& field : header.mid(12).split(' ')) {
                    if (field.startsWith('+')) summary.ahead = field.mid(1).toInt();
                    if (field.startsWith('-')) summary.behind = field.mid(1).toInt();
                }
            }
        }
        if (record.isEmpty()) continue;

        if (record.startsWith("? ") || record.startsWith("! ")) {
            File file;
            file.path = utf8(record.mid(2));
            file.tracked = false;
            file.workStatus = record[0] == '?' ? File::Status::E_Untracked : File::Status::E_Ignored;
            summary.files.append(file);
        } else if (record.startsWith("1 ") || record.startsWith("2 ") || record.startsWith("u ")) {
            const char kind = record[0];
            const QList<QByteArray> fields = record.split(' ');
            const int pathField = kind == '1' ? 8 : (kind == '2' ? 9 : 10);
            if (fields.size() <= pathField)
                continue;

            File file;
            const QByteArray xy = fields[1];
            if (xy.size() >= 2) {
                file.indexStatus = statusFromChar(xy[0]);
                file.workStatus = statusFromChar(xy[1]);
            }
            file.submoduleState = utf8(fields[2]);
            file.path = utf8(fields.mid(pathField).join(' '));
            file.conflicted = kind == 'u';
            if (file.conflicted) {
                file.indexStatus = File::Status::E_Unmerged;
                file.workStatus = File::Status::E_Unmerged;
            }
            if (kind == '2' && i + 1 < records.size())
                file.originalPath = utf8(records[++i]);
            summary.files.append(file);
        }
    }
    return summary;
}

} // namespace Git
