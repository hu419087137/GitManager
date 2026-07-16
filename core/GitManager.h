#ifndef GITMANAGER_H
#define GITMANAGER_H

#include "GitTypes.h"
#include <QObject>

namespace Git {

/**
 * @brief Git 仓库操作封装，通过 QProcess 调用 git 命令行
 *
 * 所有读取操作均为同步阻塞式；写入操作（commit/checkout 等）
 * 成功时返回 true，失败时发射 sigError() 并返回 false。
 */
class GitManager : public QObject {
    Q_OBJECT

public:
    explicit GitManager(QObject* parent = nullptr);

    /** @brief 打开指定路径的 git 仓库，成功返回 true */
    bool openRepository(const QString& path);

    QString repositoryPath() const { return _repoPath; }
    bool    isValid() const        { return _isValid; }

    // -------- 数据查询 --------

    /** @brief 获取提交历史，含 lane 布局信息（--topo-order）*/
    QVector<Commit>  fetchLog(int maxCount = 1000);

    /** @brief 获取本地及远端分支列表 */
    QVector<Branch>  fetchBranches();

    /** @brief 获取工作区文件状态（git status --porcelain=v1）*/
    QVector<File>    fetchStatus();

    /** @brief 获取单个文件的 diff（staged=true 则为暂存区 diff）*/
    QString          fetchFileDiff(const QString& filePath, bool staged = false);

    /** @brief 获取某次提交的完整 diff（git show）*/
    QString          fetchCommitDiff(const QString& commitHash);

    /** @brief 获取当前分支名 */
    QString          currentBranch();

    // -------- Git 操作 --------

    bool stageFile(const QString& filePath);
    bool unstageFile(const QString& filePath);
    bool stageAll();
    bool unstageAll();

    /**
     * @brief 将文件路径追加到仓库根目录的 .gitignore
     * @return 文件写入成功返回 true
     */
    bool addToGitIgnore(const QString& filePath);

    bool commit(const QString& message);

    bool checkoutBranch(const QString& branchName);
    bool createBranch(const QString& branchName, const QString& from = QString());
    bool deleteBranch(const QString& branchName, bool force = false);

    bool pull();
    bool push();

    /**
     * @brief 在指定提交处创建标签
     * @param tagName     标签名
     * @param commitHash  目标提交 hash（空则指向 HEAD）
     * @param message     附注消息；非空则创建附注标签（-a），否则创建轻量标签
     */
    bool createTag(const QString& tagName,
                   const QString& commitHash,
                   const QString& message = QString());

    /**
     * @brief 删除本地标签
     * @param tagName  要删除的标签名
     */
    bool deleteTag(const QString& tagName);

signals:
    /** @brief git 命令执行出错时发射，包含 stderr 内容 */
    void sigError(const QString& message);

    /** @brief 操作进度提示（非错误），可直接显示在状态栏 */
    void sigInfo(const QString& message);

    /**
     * @brief 每条 git 命令执行完毕后发射
     * @param command  完整命令字符串，如 "git pull --rebase"
     * @param output   命令输出（只读查询为空）
     * @param success  是否以 exit code 0 结束
     */
    void sigCommandRun(const QString& command, const QString& output, bool success);

private:
    /** @brief 同步执行 git 命令，返回 stdout；失败时通过 ok 输出 false */
    QString runGit(const QStringList& args, bool* ok = nullptr) const;

    /** @brief 为提交列表计算 lane（列）布局，用于图形绘制 */
    static void assignLanes(QVector<Commit>& commits);

    QString _repoPath;
    bool    _isValid {false};
};

} // namespace Git

#endif // GITMANAGER_H
