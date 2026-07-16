# Git Manager

Git Manager 是一个使用 Qt 6 和 C++17 编写的桌面 Git 仓库管理工具。它通过 `QProcess` 调用本机 Git 命令行，在一个窗口中展示仓库、分支、提交历史、工作区状态和差异内容。

## 主要功能

- 管理多个本地仓库，并支持按分组整理仓库列表
- 绘制提交历史与分支轨迹，展示分支和标签引用
- 查看本地、远程分支，支持切换、创建和删除分支
- 查看工作区及暂存区状态，支持暂存、取消暂存和批量操作
- 查看文件差异及指定提交的完整差异
- 创建提交，执行 Pull 和 Push
- 创建、删除标签
- 将未跟踪文件加入 `.gitignore`
- 在底部终端面板中查看程序执行过的 Git 命令及输出

## 环境要求

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 6，包含 Core 和 Widgets 模块
- Git 命令行工具，并确保 `git` 已加入 `PATH`

在 Windows 上可使用 Qt Online Installer 安装 Qt 6、CMake 和 Ninja，并选择与 Qt 套件匹配的 MinGW 或 MSVC 编译器。

## 构建

在已配置 Qt 环境的终端中执行：

```bash
cmake -S . -B build
cmake --build build --config Release
```

如果 CMake 无法自动找到 Qt，请指定 Qt 安装目录：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

构建完成后，运行生成的 `GitManager` 可执行文件。多配置生成器的 Release 程序通常位于 `build/Release/`，单配置生成器通常位于 `build/`。

## 使用说明

1. 启动程序，点击工具栏的 **Open Repo**，选择一个已有 Git 仓库。
2. 从左侧仓库列表切换仓库，在分支列表中管理分支。
3. 在中间提交图中选择提交以查看其差异，也可以通过右键菜单管理标签。
4. 在右侧 **Status** 页暂存或取消暂存文件，选择文件可在 **Diff** 页查看差异。
5. 暂存文件后点击 **Commit** 填写提交信息；使用工具栏执行 Pull 或 Push。

Pull 操作会在工作区不干净时自动创建临时 stash，执行 `pull --rebase` 后再恢复改动。如果恢复时发生冲突，需要在仓库中手动解决。

## 项目结构

```text
GitManager/
├── core/       # Git 命令封装和数据类型
├── dialogs/    # 提交等对话框
├── widgets/    # 提交图、分支、状态、差异和终端组件
├── MainWindow.*
├── main.cpp
└── CMakeLists.txt
```

## 注意事项

- 当前 Git 操作是同步执行的，超时时间为 15 秒；大型仓库或网络较慢时操作可能暂时阻塞界面。
- Push、Pull 和远程分支操作依赖仓库自身已配置的远程地址及 Git 凭据。
- 删除分支、强制删除分支等操作会直接修改当前仓库，请确认后再执行。
