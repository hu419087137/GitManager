#ifndef GITTYPES_H
#define GITTYPES_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMetaType>
#include <QVector>
#include <QColor>

namespace Git {

enum class ResetMode {
    Soft,
    Mixed,
    Hard
};

enum class RebaseAction {
    Pick,
    Reword,
    Edit,
    Squash,
    Fixup,
    Drop
};

enum class RepositoryOperation {
    None,
    Merge,
    Rebase,
    CherryPick,
    Revert,
    Unknown
};

enum class HistoryOperationStatus {
    Completed,
    UpToDate,
    Conflicts,
    PausedForEdit
};

struct HistoryOperationResult {
    HistoryOperationStatus status {HistoryOperationStatus::Completed};
    QString message;
};

struct HistoryRewritePreview {
    QString revision;
    QString targetHash;
    QString expectedHead;
    QString currentBranch;
    QString upstream;
    int affectedCount {0};
    int publishedCount {0};
    bool dirty {false};
    RepositoryOperation activeOperation {RepositoryOperation::None};
};

struct RebasePlanItem {
    QString hash;
    QString subject;
    QString message;
    RebaseAction action {RebaseAction::Pick};
    QString rewrittenMessage;
    bool published {false};
    int parentCount {0};
};

struct RebasePlan {
    HistoryRewritePreview preview;
    QVector<RebasePlanItem> items;
};

struct WorktreeInfo {
    QString name;
    QString path;
    QString headBranch;
    QString headHash;
    bool detached {false};
    bool locked {false};
    QString lockReason;
    bool valid {true};
    bool current {false};
};

struct SubmoduleInfo {
    QString name;
    QString path;
    QString url;
    QString branch;
    QString indexHash;
    QString workdirHash;
    bool initialized {false};
    bool dirty {false};
};

struct LfsLockInfo {
    QString id;
    QString path;
    QString owner;
    QString lockedAt;
    bool ownedByCurrentUser {false};
};

struct LfsState {
    bool installed {false};
    QString version;
    QStringList trackedPatterns;
    QVector<LfsLockInfo> locks;
    QString locksError;
};

enum class HostingProvider {
    Unknown,
    GitHub,
    GitLab,
    AzureDevOps
};

struct RemoteInfo {
    QString name;
    QString fetchUrl;
    QString pushUrl;
};

struct HostingRemoteInfo {
    QString remoteName;
    QString sourceUrl;
    QString webUrl;
    QString commitUrl;
    QString changesUrl;
    QString createChangeUrl;
    QString issuesUrl;
    HostingProvider provider {HostingProvider::Unknown};
};

struct HostingChangeInfo {
    QString id;
    QString title;
    QString author;
    QString state;
    QString webUrl;
    QString headSha;
    bool draft {false};
};

struct HostingIssueInfo {
    QString id;
    QString title;
    QString author;
    QString state;
    QString webUrl;
};

struct HostingReviewFile {
    QString path;
    QString previousPath;
    QString status;
    QString patch;
    QString webUrl;
    QString baseSha;
    QString startSha;
    QString headSha;
};

struct DiagnosticItem {
    QString category;
    QString name;
    QString value;
    bool warning {false};
};

struct GitDiagnosticReport {
    QVector<DiagnosticItem> items;
    QVector<RemoteInfo> remotes;
};

struct HookInfo {
    QString name;
    QString path;
    bool executable {false};
};

struct HookResult {
    QString name;
    QString output;
    int exitCode {0};
    bool success {true};
    bool timedOut {false};
};

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
Q_DECLARE_METATYPE(Git::ResetMode)
Q_DECLARE_METATYPE(Git::RebaseAction)
Q_DECLARE_METATYPE(Git::RepositoryOperation)
Q_DECLARE_METATYPE(Git::HistoryOperationStatus)
Q_DECLARE_METATYPE(Git::HistoryOperationResult)
Q_DECLARE_METATYPE(Git::HistoryRewritePreview)
Q_DECLARE_METATYPE(Git::RebasePlanItem)
Q_DECLARE_METATYPE(Git::RebasePlan)
Q_DECLARE_METATYPE(Git::WorktreeInfo)
Q_DECLARE_METATYPE(Git::LfsLockInfo)
Q_DECLARE_METATYPE(Git::LfsState)
Q_DECLARE_METATYPE(Git::HostingProvider)
Q_DECLARE_METATYPE(Git::RemoteInfo)
Q_DECLARE_METATYPE(Git::HostingRemoteInfo)
Q_DECLARE_METATYPE(Git::HostingChangeInfo)
Q_DECLARE_METATYPE(Git::HostingIssueInfo)
Q_DECLARE_METATYPE(Git::HostingReviewFile)
Q_DECLARE_METATYPE(Git::DiagnosticItem)
Q_DECLARE_METATYPE(Git::GitDiagnosticReport)
Q_DECLARE_METATYPE(Git::HookInfo)
Q_DECLARE_METATYPE(Git::HookResult)

#endif // GITTYPES_H
