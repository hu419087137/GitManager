#ifndef GITTYPES_H
#define GITTYPES_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMetaType>
#include <QVector>
#include <QColor>

namespace Git {

/** @brief 单次提交信息 */
struct Commit {
    QString hash;
    QString shortHash;
    QStringList parents;
    QString authorName;
    QDateTime date;
    QString subject;
    QStringList refs;  // 指向该提交的分支名/标签名
    QStringList remoteRefs; // refs 中属于远程分支的项目

    // 图形布局（由 libgit2 历史查询赋值）
    int lane {0};
    QVector<QString> activeLanes; // 该行各 lane 指向的父提交 hash
};

/** @brief 分支信息 */
struct Branch {
    QString name;
    QString fullName;
    QString hash;
    bool isCurrent  {false};
    bool isRemote   {false};
    QString upstream;
    int ahead {0};
    int behind {0};
};

/** @brief 提交历史查询；branch 为空表示 HEAD，"*" 表示全部分支。 */
struct CommitHistoryQuery {
    QString searchText;
    QString author;
    QString branch;
    QString path;
    QDateTime fromDate;
    QDateTime toDate;
    bool oldestFirst {false};
    int offset {0};
    int limit {200};
    QString expectedRefsVersion;

    bool sameFilter(const CommitHistoryQuery& other) const
    {
        return searchText == other.searchText
            && author == other.author
            && branch == other.branch
            && path == other.path
            && fromDate == other.fromDate
            && toDate == other.toDate
            && oldestFirst == other.oldestFirst;
    }

    bool hasFilters() const
    {
        return !searchText.isEmpty() || !author.isEmpty() || !path.isEmpty()
            || fromDate.isValid() || toDate.isValid();
    }
};

/** @brief 一页提交历史，以及用于检测分页期间引用变化的版本。 */
struct CommitHistoryPage {
    QVector<Commit> commits;
    int offset {0};
    bool hasMore {false};
    bool resetRequired {false};
    QString refsVersion;
};

/** @brief 文件状态 */
struct File {
    enum class Status : char {
        E_Unmodified = ' ',
        E_Modified   = 'M',
        E_Added      = 'A',
        E_Deleted    = 'D',
        E_Renamed    = 'R',
        E_Copied     = 'C',
        E_TypeChanged= 'T',
        E_Unmerged   = 'U',
        E_Untracked  = '?',
        E_Ignored    = '!'
    };

    QString path;
    QString originalPath;  // rename 时的旧路径
    Status  indexStatus   {Status::E_Unmodified};
    Status  workStatus    {Status::E_Unmodified};
    QString submoduleState;
    bool tracked {true};
    bool conflicted {false};

    /** @brief 返回简短的状态字符串，如 "M " "??" */
    QString statusText() const {
        return QString("%1%2")
            .arg(static_cast<char>(indexStatus))
            .arg(static_cast<char>(workStatus));
    }

    bool isStaged() const {
        return indexStatus != Status::E_Unmodified &&
               indexStatus != Status::E_Untracked;
    }

    bool isUnstaged() const {
        return workStatus != Status::E_Unmodified;
    }
};

struct StatusSummary {
    QString headName;
    QString headHash;
    QString upstream;
    int ahead {0};
    int behind {0};
    bool detached {false};
    bool unborn {false};
    QVector<File> files;
};

// 图形绘制用的 lane 颜色列表
static const QVector<QColor> kLaneColors = {
    QColor("#4e9de9"),
    QColor("#f0883e"),
    QColor("#3fb950"),
    QColor("#da3633"),
    QColor("#bc8cff"),
    QColor("#39d353"),
    QColor("#ffa657"),
    QColor("#ff7b72"),
    QColor("#79c0ff"),
    QColor("#56d364"),
};

inline QColor laneColor(int lane) {
    return kLaneColors[lane % kLaneColors.size()];
}

} // namespace Git

Q_DECLARE_METATYPE(Git::Commit)
Q_DECLARE_METATYPE(Git::CommitHistoryQuery)
Q_DECLARE_METATYPE(Git::CommitHistoryPage)

#endif // GITTYPES_H
