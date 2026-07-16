#ifndef GITTYPES_H
#define GITTYPES_H

#include <QString>
#include <QStringList>
#include <QDateTime>
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

    // 图形布局（由 GitManager 赋值）
    int lane {0};
    QVector<QString> activeLanes; // 该行各 lane 指向的父提交 hash
};

/** @brief 分支信息 */
struct Branch {
    QString name;
    QString hash;
    bool isCurrent  {false};
    bool isRemote   {false};
    QString upstream;
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
        E_Untracked  = '?',
        E_Ignored    = '!'
    };

    QString path;
    QString originalPath;  // rename 时的旧路径
    Status  indexStatus   {Status::E_Unmodified};
    Status  workStatus    {Status::E_Unmodified};

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

#endif // GITTYPES_H
