# Git Manager 开发计划

本文档用于将 Git Manager 从当前原型逐步建设为可稳定日常使用、接近 VS Code Git 核心体验，并最终向 SmartGit 的专业能力靠拢。

实施时按阶段顺序推进。每完成一个任务，同时补充测试、更新本文档中的复选框，并尽量保持一次提交只包含一个完整改动。

## 总体原则

- 优先保证 Git 状态和操作的正确性，再扩展功能。
- 所有耗时 Git 命令异步执行，禁止阻塞 UI 线程。
- Git 命令执行、输出解析、仓库状态和 UI 展示相互分离。
- 危险操作必须显示影响范围并二次确认。
- 新增解析器或 Git 操作时必须同时增加测试。
- 仓库切换后，旧仓库的异步结果不得更新当前界面。
- 核心 Git 能力统一使用内置 libgit2，不依赖外部 `git.exe`。
- Windows OpenSSL 使用仓库内预编译包；所有平台的项目构建过程均不再编译 OpenSSL 源码。

## 目标架构

```text
MainWindow / Widgets
        ↓
RepositoryController
        ↓
GitService / RepositoryState
        ↓
LibGit2Backend
```

建议逐步调整为以下目录：

```text
core/
├── GitManager.*             # QtConcurrent 异步队列、取消和结果隔离
├── LibGit2Backend.*         # 仓库读写、网络和历史操作
├── RepositoryState.*        # 当前仓库状态快照
├── DiffParser.*             # Diff hunk 结构解析
└── GitTypes.h
controllers/
└── RepositoryController.*   # UI 与 Git 服务之间的业务编排
tests/
├── unit/
└── integration/
```

---

## 第一阶段：可靠的 Git 基础层

目标：消除界面阻塞和关键解析错误，为后续功能提供稳定基础。

### 1.1 异步命令执行器

迁移说明：本阶段最初以 `GitCommandRunner` 和 `QProcess` 实现，第二阶段完成后已由 `GitManager` 的 QtConcurrent 队列及 `LibGit2Backend` 替代，旧执行器和命令输出解析器已删除。

新增文件：

- [x] `core/GitResult.h`（历史实现，已由 libgit2 结构化结果替代）
  - 保存命令、标准输出、标准错误、退出码、取消状态和错误类型。
  - 区分“命令失败”和 `git diff` 的“发现差异”退出码。
- [x] `core/GitCommandRunner.h`（历史实现，已删除）
- [x] `core/GitCommandRunner.cpp`（历史实现，已删除）
  - 使用 QtConcurrent 任务队列异步执行 libgit2 操作。
  - 支持本地命令、网络命令使用不同超时策略。
  - 支持取消、实时输出、执行状态和仓库工作目录。
  - 对命令参数进行安全的日志格式化，不把凭据写入日志。

修改文件：

- [x] `core/GitManager.h`
- [x] `core/GitManager.cpp`
  - 移除同步 `waitForFinished()` 和 `const_cast` 发信号方式。
  - 将查询与写操作逐步改为异步接口。
- [x] `widgets/TerminalWidget.h`
- [x] `widgets/TerminalWidget.cpp`
  - 改名或显示标题为“Git 输出”。
  - 支持实时追加 stdout/stderr、复制和清空日志。
- [x] `MainWindow.h`
- [x] `MainWindow.cpp`
  - 命令执行时显示进度并禁用冲突操作。
  - 删除 `QApplication::processEvents()`。
  - 支持取消长时间运行的操作。
- [x] `CMakeLists.txt`
  - 加入新增源码。

验收标准：

- Pull、Push、刷新和加载大型提交时窗口仍可拖动和响应。
- 用户可以取消网络操作。
- 仓库切换后，旧命令结果不会覆盖新仓库界面。
- 错误详情包含命令、退出码和 stderr。

### 1.2 可靠的状态解析

迁移说明：状态和分支现在直接从 libgit2 结构体构造，原命令文本解析器及其单元测试已由后端集成测试取代。

新增文件：

- [x] `core/parsers/StatusParser.h`（历史实现，已删除）
- [x] `core/parsers/StatusParser.cpp`（历史实现，已删除）
  - 使用 `git_status_list` 直接读取仓库状态。
  - 支持普通修改、重命名、复制、未跟踪文件、冲突和子模块。

修改文件：

- [x] `core/GitTypes.h`
  - 扩展 `File` 类型，分别保存 index、worktree、conflict、tracked 和 submodule 状态。
  - 增加分支 ahead/behind、upstream 和 detached HEAD 信息。
- [x] `core/GitManager.cpp`
  - 使用 NUL 分隔输出，不再用字符串拆分 `old -> new`。
- [x] `widgets/StatusWidget.h`
- [x] `widgets/StatusWidget.cpp`
  - 分为 `Merge Changes`、`Changes`、`Staged Changes` 三组。
  - 同一个 `MM` 文件分别出现在工作区和暂存区列表中。
  - 显示准确的状态、路径和提示信息。
- [x] `MainWindow.cpp`
  - 点击不同分组的文件时加载对应 diff。

验收标准：

- 正确处理空格、中文、引号、反斜杠和换行相关路径。
- `MM` 文件可以分别查看暂存和未暂存 diff。
- 合并冲突文件进入独立分组。
- 未跟踪文件可以预览内容。

### 1.3 分支解析和仓库状态

新增文件：

- [x] `core/parsers/BranchParser.h`（历史实现，已删除）
- [x] `core/parsers/BranchParser.cpp`（历史实现，已删除）
- [x] `core/RepositoryState.h`
- [x] `core/RepositoryState.cpp`

修改文件：

- [x] `core/GitManager.cpp`
  - 根据 `refs/heads/` 和 `refs/remotes/` 判断分支类型。
  - 不再使用名称中是否包含 `/` 判断远程分支。
  - 支持 detached HEAD、空仓库和 unborn branch。
- [x] `widgets/BranchListWidget.cpp`
  - 显示 ahead/behind、upstream 和当前 HEAD 状态。
- [x] `MainWindow.cpp`
  - 状态栏显示当前分支及同步状态。

验收标准：

- 本地 `feature/login` 不会被识别成远程分支。
- detached HEAD、无提交仓库可以正常打开。
- 分支同步状态与命令行结果一致。

### 1.4 第一批自动化测试

新增文件：

- [x] `tests/CMakeLists.txt`
- [x] `tests/unit/TestStatusParser.cpp`（历史测试，已由 libgit2 后端测试取代）
- [x] `tests/unit/TestBranchParser.cpp`（历史测试，已由 libgit2 后端测试取代）
- [x] `tests/integration/TestRepository.cpp`

修改文件：

- [x] `CMakeLists.txt`
  - 引入 `CTest` 和 Qt Test，提供 `BUILD_TESTING` 开关。

覆盖场景：

- [x] 空仓库、detached HEAD、普通分支和远程分支。
- [x] staged、unstaged、`MM`、rename、delete、untracked。
- [x] 中文、空格、引号和特殊字符路径。
- [x] 合并冲突和 stash 冲突。
- [x] libgit2 操作失败、网络取消和仓库结果隔离。

验收标准：

- `ctest --test-dir build` 可重复通过。
- 测试使用临时仓库，不依赖开发机上的现有仓库。

---

## 第二阶段：达到 VS Code Git 核心体验

目标：覆盖开发者最常用的完整工作流。

### 2.1 仓库创建和远程操作

状态：本阶段核心范围已完成。当前采用主窗口轻量输入流程，支持会话级 HTTPS 凭据；持久凭据保险库和独立对话框留作后续增强。

修改文件：

- [x] `core/GitManager.h/.cpp`
  - 增加 Init、Clone、Fetch、添加/删除 remote。
  - Push 首次分支时支持设置 upstream。
  - Push 遵循已配置 upstream 的远程目标分支。
- [x] `MainWindow.h/.cpp`
  - 增加 Clone、Init、Fetch 入口和进度展示。
- [ ] `widgets/RepoListWidget.h/.cpp`
  - 欢迎页支持 Clone 和打开最近仓库。

建议新增：

- [ ] `dialogs/CloneDialog.h/.cpp`
- [ ] `dialogs/RemoteDialog.h/.cpp`
- [ ] 持久化系统凭据保险库和代理诊断。

验收标准：

- 可以从 URL Clone，并显示实时进度。
- 新分支首次 Push 可以选择 remote 并设置 upstream。
- 认证失败、代理错误和远程不存在时提供可理解的提示。

### 2.2 完整的工作区操作

状态：已完成。支持安全确认后的恢复修改和删除未跟踪文件。

修改文件：

- [x] `core/GitManager.h/.cpp`
  - 增加丢弃文件修改、删除未跟踪文件、恢复删除文件。
  - unborn HEAD 时使用兼容的取消暂存策略。
- [x] `widgets/StatusWidget.h/.cpp`
  - 增加文件级暂存、取消暂存和丢弃。
  - 危险操作展示文件范围并确认。
- [ ] `widgets/StatusWidget.h/.cpp`
  - 增加打开文件和打开所在目录。
- [x] `MainWindow.h/.cpp`
  - 统一处理操作结果和局部刷新。

验收标准：

- 所有文件操作在普通仓库和空仓库中都有效。
- 丢弃操作不会因选错 staged/worktree 状态而损坏另一份修改。

### 2.3 交互式 Diff 和区块暂存

状态：已完成核心 hunk 操作。结构化解析由 `core/DiffParser.*` 实现，现有 `DiffWidget` 提供右键操作；双栏视图和行级操作留作后续体验增强。

建议新增：

- [x] `core/DiffParser.h/.cpp`
- [ ] `core/PatchBuilder.h/.cpp`
- [ ] `widgets/DiffView.h/.cpp`
- [ ] `widgets/DiffFileModel.h/.cpp`

替换或修改：

- [x] `widgets/DiffWidget.h/.cpp`
  - 使用结构化 hunk 提取，按 staged/unstaged 来源限制右键操作。
  - 保留语法高亮和纯文本展示。
- [ ] `widgets/DiffWidget.h/.cpp`
  - 支持行号、同步滚动、行内/双栏切换。
  - 支持上一处/下一处修改。
- [x] `core/GitManager.h/.cpp`
  - 使用 `git apply --cached` 等方式实现 hunk 暂存和取消暂存。

验收标准：

- 可以暂存、取消暂存和丢弃单个 hunk。
- 补丁包含空格、中文路径和无尾随换行时仍可正确应用。
- 大 Diff 不会明显阻塞界面。

### 2.4 Stash 管理

状态：本阶段核心范围已完成。支持列表、创建、应用、弹出和删除，冲突后立即刷新统一冲突状态。

建议新增：

- [ ] `core/parsers/StashParser.h/.cpp`
- [ ] `widgets/StashListWidget.h/.cpp`
- [ ] `dialogs/StashDialog.h/.cpp`

修改文件：

- [x] `core/GitManager.h/.cpp`
  - 增加 list、push、apply、pop 和 drop。
- [x] `MainWindow.cpp`
  - 提供 Stash 创建和管理入口。
- [ ] `core/GitManager.h/.cpp`
  - 增加 show 和 branch。
- [ ] `MainWindow.cpp`
  - 增加可配置的 Pull 自动 stash 策略。

验收标准：

- 用户能看到自动 stash，避免误以为修改丢失。
- apply/pop 冲突后进入冲突解决流程。

### 2.5 提交体验

状态：已完成本阶段核心范围。支持多行消息、字符计数、Amend 和 Sign-off；签名配置留作专业设置功能。

修改文件：

- [x] `dialogs/CommitDialog.h/.cpp`
  - 支持多行消息、字符计数、Amend 和 Sign-off。
- [ ] `dialogs/CommitDialog.h/.cpp`
  - 支持提交模板、历史消息和 GPG/SSH 签名选项。
- [x] `core/GitManager.h/.cpp`
  - 增加 amend 和 signoff；Continue cherry-pick 时保留原作者。
- [ ] `core/GitManager.h/.cpp`
  - 增加指定 author 和签名参数。
- [ ] `widgets/CommitGraphWidget.cpp`
  - 增加复制 Hash、Revert 和 Cherry-pick。

验收标准：

- 可修改最近一次提交。
- 多行提交消息不会因参数或编码问题损坏。

### 2.6 冲突解决器

状态：已完成基础流程。支持选择 current/incoming，并继续或终止 merge、rebase、cherry-pick；三栏合并编辑器留作后续增强。

建议新增：

- [ ] `core/ConflictService.h/.cpp`
- [ ] `widgets/MergeEditorWidget.h/.cpp`
- [ ] `dialogs/ConflictDialog.h/.cpp`

修改文件：

- [x] `widgets/StatusWidget.cpp`
  - 冲突文件提供“接受当前/传入”。
- [x] `MainWindow.cpp`
  - 提供 Merge/Rebase/Cherry-pick 的继续和安全确认终止入口。
- [ ] `widgets/StatusWidget.cpp`
  - 增加接受全部和手动合并。
- [ ] `MainWindow.cpp`
  - 根据仓库状态只显示有效操作，并增加 Skip。

验收标准：

- 支持常见双向文本冲突。
- 用户可以继续或终止 merge/rebase/cherry-pick。
- 不支持的复杂冲突会明确提示使用外部工具，而不是静默失败。

---

## 第三阶段：提交图和高级历史操作

### 3.1 提交图性能与查询

状态：已完成。提交历史已从仓库状态快照中拆分，默认每页加载 200 条；引用发生变化时会自动放弃旧分页并重新加载，普通工作区状态刷新不会重置提交图。

修改文件：

- [x] `core/GitManager.cpp`
  - 日志改为分页或增量加载，不再固定一次读取 1000 条。
- [x] `widgets/CommitGraphWidget.h/.cpp`
  - 增加搜索、作者/日期/分支/路径过滤。
  - 支持继续加载历史、复制 Hash、查看父子提交。
  - 保存列宽、排序和图形显示设置。

实现说明：

- 历史查询直接使用 libgit2 结构化对象，不新增命令日志解析器。
- 提交详情继续复用现有 Diff 面板，不额外增加重复的详情组件。
- 测试覆盖超过 1000 条提交的分页、过滤、引用变化、空仓库、快速查询切换和仓库切换隔离。

### 3.2 历史修改操作

修改文件：

- [ ] `core/GitManager.h/.cpp`
  - 增加 Merge、Rebase、Cherry-pick、Revert 和 Reset。
- [ ] `widgets/CommitGraphWidget.cpp`
  - 为提交和分支增加上下文菜单。

建议新增：

- [ ] `dialogs/ResetDialog.h/.cpp`
- [ ] `dialogs/RebaseDialog.h/.cpp`
- [ ] `widgets/InteractiveRebaseWidget.h/.cpp`

验收标准：

- Reset 明确解释 soft、mixed、hard 的影响。
- Interactive Rebase 支持 pick、reword、edit、squash、fixup 和 drop。
- 改写已推送历史前提供醒目警告。

### 3.3 分支、标签和比较

修改文件：

- [ ] `widgets/BranchListWidget.h/.cpp`
  - 增加重命名、Merge、Rebase、发布和取消跟踪。
- [ ] `widgets/CommitGraphWidget.h/.cpp`
  - 支持比较两个提交或分支。
- [ ] `core/GitManager.h/.cpp`
  - 支持远程标签 Push/Delete 和 prune。

建议新增：

- [ ] `widgets/CompareWidget.h/.cpp`
- [ ] `dialogs/BranchDialog.h/.cpp`

---

## 第四阶段：专业仓库能力

以下能力在核心流程稳定后按实际需求推进：

- [ ] Submodule 管理：新增 `core/SubmoduleService.*` 和对应列表组件。
- [ ] Git LFS：检测安装状态、文件跟踪规则、锁管理。
- [ ] Worktree：创建、打开、锁定、移动和删除。
- [ ] GitHub/GitLab/Azure DevOps：账户、PR/MR、Issue 和代码评审。
- [ ] SSH、凭据、代理诊断页面。
- [ ] Git hooks 执行结果和失败提示。
- [ ] Force-with-lease，禁止默认使用不安全的 force push。
- [ ] 可配置外部 Diff/Merge 工具。

---

## 横向工程任务

这些任务应伴随各阶段持续实施。

### 自动刷新与缓存

建议新增：

- [ ] `core/RepositoryWatcher.h/.cpp`
- [ ] `core/RepositoryCache.h/.cpp`

要求：

- 重点监听 `.git/index`、`HEAD` 和 refs，避免逐文件监听大型仓库。
- 使用防抖刷新。
- 窗口重新获得焦点时检查仓库变化。
- Git 操作完成后只刷新受影响的区域。

### 设置与产品体验

建议新增：

- [ ] `core/AppSettings.h/.cpp`
- [ ] `dialogs/SettingsDialog.h/.cpp`
- [ ] `widgets/NotificationWidget.h/.cpp`
- [ ] `widgets/WelcomeWidget.h/.cpp`

修改文件：

- [ ] `MainWindow.cpp`
  - 保存窗口、splitter、列宽和最后活动面板。
  - 增加快捷键、菜单和命令入口。
- [ ] 所有 UI 文件
  - 使用 `tr()` 为国际化做准备。
  - 支持系统深浅主题、高 DPI、键盘操作和无障碍名称。

### CI、打包和发布

建议新增：

- [ ] `.github/workflows/build.yml`
- [ ] `cmake/DeployQt.cmake`
- [ ] `LICENSE`
- [ ] `CHANGELOG.md`

修改文件：

- [ ] `CMakeLists.txt`
  - 安装规则、版本信息、测试选项和打包配置。
- [ ] `README.md`
  - 补充安装包、测试、贡献和发布说明。

验收标准：

- 每次提交自动执行 Windows Release 构建和测试。
- 使用 `windeployqt` 生成可在无 Qt 开发环境机器运行的目录或安装包。

---

## 建议提交顺序

每个编号建议作为一个或多个独立提交，避免将架构调整与大量 UI 改动混在一起：

1. `refactor: 引入异步 Git 命令执行器`
2. `refactor: 使用 porcelain v2 解析仓库状态`
3. `fix: 正确区分本地和远程分支`
4. `test: 增加 Git 解析器和临时仓库测试`
5. `feat: 拆分工作区与暂存区文件列表`
6. `feat: 支持克隆初始化和远程仓库管理`
7. `feat: 支持文件与区块级暂存操作`
8. `feat: 增加 stash 管理`
9. `feat: 增强提交与历史操作`
10. `feat: 增加冲突解决流程`

## 当前进度

- [x] 多仓库列表和分组管理
- [x] 基础提交图和分支展示
- [x] 基础工作区/暂存区操作
- [x] 文件和提交 Diff 文本展示
- [x] Commit、Pull、Push 和标签基础操作
- [x] Git 命令输出面板
- [x] README、构建环境和运行截图
- [x] 第一阶段：可靠的 Git 基础层
- [x] 第二阶段核心范围：VS Code Git 常用工作流（未勾选增强项继续保留）
- [x] 依赖部署：预编译 OpenSSL、运行库和 CA 根证书自动部署
- [ ] 第三阶段：高级历史操作
- [ ] 第四阶段：专业仓库能力
