# Git Manager

Git Manager 是一个使用 Qt 6 和 C++17 编写的桌面 Git 仓库管理工具。它通过 `QProcess` 调用本机 Git 命令行，在一个窗口中展示仓库、分支、提交历史、工作区状态和差异内容。

## 运行效果

![Git Manager 主界面](docs/images/git-manager-overview.png)

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

### 当前开发环境

本项目当前已验证的 Windows 编译环境如下，便于后续快速恢复 Qt Creator Kit 或从命令行构建：

| 工具 | 版本或路径 |
| --- | --- |
| Qt | Qt 6.7.3（MSVC 2019 64-bit） |
| Qt 套件 | `D:/Qt/6.7.3/6.7.3/msvc2019_64` |
| CMake | `D:/Qt/6.7.3/Tools/CMake_64/bin/cmake.exe` |
| Ninja | `D:/Qt/6.7.3/Tools/Ninja/ninja.exe` |
| C++ 编译器 | MSVC 14.29.30133（Visual Studio 2019，x64） |
| `cl.exe` | `D:/vs/2019/IDE/VC/Tools/MSVC/14.29.30133/bin/HostX64/x64/cl.exe` |
| CMake 生成器 | Ninja |

Qt Creator 中对应的构建套件名称为 `Desktop Qt 6.7.3 MSVC2019 64bit`。如果工具或 SDK 被移动，需要同步修改 Kit 中的 Qt、CMake、编译器和调试器路径。

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

使用当前开发机的完整路径，可在已初始化 MSVC x64 环境的 PowerShell 中执行：

```powershell
& "D:/Qt/6.7.3/Tools/CMake_64/bin/cmake.exe" `
  -S . -B build/Release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/6.7.3/msvc2019_64" `
  -DCMAKE_MAKE_PROGRAM="D:/Qt/6.7.3/Tools/Ninja/ninja.exe"

& "D:/Qt/6.7.3/Tools/CMake_64/bin/cmake.exe" --build build/Release
```

从普通 PowerShell 构建前，应先运行 Visual Studio 的 `VsDevCmd.bat -arch=x64`，或直接使用 Qt Creator 的对应 Kit，以确保 MSVC 的头文件、库和 SDK 环境变量可用。

构建完成后，运行生成的 `GitManager` 可执行文件。多配置生成器的 Release 程序通常位于 `build/Release/`，单配置生成器通常位于 `build/`。

## 使用说明

1. 启动程序，点击工具栏的 **Open Repo**，选择一个已有 Git 仓库。
2. 从左侧仓库列表切换仓库，在分支列表中管理分支。
3. 在中间提交图中选择提交以查看其差异，也可以通过右键菜单管理标签。
4. 在右侧 **Status** 页暂存或取消暂存文件，选择文件可在 **Diff** 页查看差异。
5. 暂存文件后点击 **Commit** 填写提交信息；使用工具栏执行 Pull 或 Push。

Pull 使用 Git 的 `pull --rebase --autostash`，工作区不干净时由 Git 自动临时保存并恢复改动。如果恢复时发生冲突，需要在仓库中手动解决。

## 测试

配置时启用测试并完成构建后，可以执行：

```powershell
& "D:/Qt/6.7.3/Tools/CMake_64/bin/ctest.exe" `
  --test-dir build/Release --output-on-failure -C Release
```

测试覆盖 Porcelain v2 状态解析、分支分类、空仓库、detached HEAD、特殊路径、stash 冲突，以及异步命令的失败、超时和取消。

## 开发计划

后续架构调整、功能路线、逐文件任务和验收标准请参阅 [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md)。实施完成后可直接在计划文件中勾选对应任务。

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

- Git 操作通过异步队列执行，本地命令默认超时为 30 秒，网络命令可通过工具栏的 **Cancel** 主动取消。
- Push、Pull 和远程分支操作依赖仓库自身已配置的远程地址及 Git 凭据。
- 删除分支、强制删除分支等操作会直接修改当前仓库，请确认后再执行。
