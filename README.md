# Git Manager

Git Manager 是一个使用 Qt 6、C++17 和 libgit2 编写的桌面 Git 仓库管理工具。它直接读写 Git 仓库，在一个窗口中展示仓库、分支、提交历史、工作区状态和差异内容。

## 运行效果

![Git Manager 主界面](docs/images/git-manager-overview.png)

## 主要功能

- 管理多个本地仓库，并支持按分组整理仓库列表
- 绘制提交历史与分支轨迹，展示分支和标签引用
- 查看本地、远程分支，支持切换、创建和删除分支
- 查看工作区及暂存区状态，支持暂存、取消暂存和批量操作
- 查看文件差异及指定提交的完整差异
- 创建提交，执行 Pull 和 Push
- 初始化、克隆仓库，Fetch 远程并发布新分支
- 管理 Stash，支持创建、应用、弹出和删除
- 按 Diff hunk 暂存、取消暂存或丢弃修改
- 识别冲突并选择当前/传入版本，继续或终止 Git 操作
- 创建、删除标签
- 将未跟踪文件加入 `.gitignore`
- 在底部 Git 输出面板中查看操作和进度日志

## 环境要求

- CMake 3.20 或更高版本
- 支持 C++17 的编译器
- Qt 6，包含 Core、Concurrent、Widgets 和 Test（仅测试）模块

本项目的 Windows 构建基线固定为 Qt 6.7.3 `msvc2019_64` + Visual Studio 2019，不要混用 Qt 的 `mingw_64` 套件；两套 Qt 库的 ABI 不兼容。项目外层使用 Ninja 生成器，编译器仍是 VS2019 的 MSVC。

项目将 `third_party/libgit2-1.9.4` 直接作为源码子项目构建，运行时不依赖 `git.exe`。HTTPS 默认使用 Windows WinHTTP、macOS SecureTransport 和 Ubuntu OpenSSL 动态加载后端。仓库同时保存 `third_party/OpenSSLv3.0.16` 源码；配置 `-DGITMANAGER_BUILD_BUNDLED_OPENSSL=ON` 后，顶层 CMake 会通过该目录内的包装 `CMakeLists.txt` 执行 `add_subdirectory()`，调用 OpenSSL 官方 `Configure` 流程并静态链接生成的库。可选模式需要完整 Perl 环境；Windows 主环境的 MSVC 子构建使用 `nmake`，MinGW 兼容性验证才使用 `mingw32-make`，Unix/macOS 使用 Make。

SSH 远程当前使用 libgit2 的 OpenSSH exec 后端，因此需要系统可用的 `ssh`；HTTPS 操作不需要系统 Git。

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

### 已验证构建

| 配置 | 结果 |
| --- | --- |
| MSVC 2019 x64 + 默认 WinHTTP + 内置 libgit2 | GitManager 构建通过，CTest 3/3 通过 |
| MSVC 2019 x64 + 内置 OpenSSL 3.0.16 + 内置 libgit2 | 已确认自动选择 `VC-WIN64A + nmake`；当前机器缺少原生 Windows Perl，尚未完成全量实编译 |

本项目 Windows 主环境始终是 Qt 6.7.3 `msvc2019_64`。在该环境启用内置 OpenSSL 时，外层继续使用 Ninja，OpenSSL 官方子构建会自动选择 VS2019 的 `nmake` 和 `VC-WIN64A`。它要求原生 Windows Perl（推荐 Strawberry Perl）；MSYS/Cygwin Perl 会被配置期明确拒绝。默认 MSVC + WinHTTP 不受影响。

另外曾使用 Qt 6.7.3 `mingw_64` 做过一次附加的跨工具链验证，因此对应的临时审计构建目录会显示 `mingw32-make`；这不是项目的 Windows 主构建链，也不用于替代 VS2019。该附加配置的 OpenSSL/libgit2/GitManager 全量源码构建和 CTest 3/3 已通过。

macOS 的 SecureTransport 和 Ubuntu 的 OpenSSL 动态后端已写入 CMake 平台选择逻辑，但当前开发机无法代替对应系统做实际编译验证。

Qt Creator 中对应的构建套件名称为 `Desktop Qt 6.7.3 MSVC2019 64bit`。如果工具或 SDK 被移动，需要同步修改 Kit 中的 Qt、CMake、编译器和调试器路径。

MSVC 与 MinGW 必须使用不同的构建目录，不能复用 `CMakeCache.txt`。如果某个旧目录显示 `mingw32-make`，应切换到 `build/Desktop_Qt_6_7_3_MSVC2019_64bit-Release` 或为 MSVC 新建构建目录。

## 构建

在已配置 Qt 环境的终端中执行：

```bash
cmake -S . -B build
cmake --build build --config Release
```

默认 HTTPS 后端按平台自动选择；如需强制构建仓库内 OpenSSL 源码，可在配置命令末尾加入：

```bash
-DGITMANAGER_BUILD_BUNDLED_OPENSSL=ON
```

如果 CMake 无法自动找到 Qt，请指定 Qt 安装目录：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/6.7.3/msvc2019_64"
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

Pull 由 libgit2 执行 Fetch 后快进或 Rebase。工作区存在未提交修改时会拒绝不安全的覆盖；可先使用 Stash 保存修改，完成 Pull 后再恢复。

## 测试

配置时启用测试并完成构建后，可以执行：

```powershell
& "D:/Qt/6.7.3/Tools/CMake_64/bin/ctest.exe" `
  --test-dir build/Release --output-on-failure -C Release
```

测试直接使用临时仓库和内置 libgit2，不调用系统 Git。当前覆盖 Diff hunk 解析、状态读取、中文/空格/特殊字符路径、无尾随换行、提交与分支、重命名暂存、补丁正反向应用、Stash、丢弃、Abort 安全边界和 Push upstream 目标。

## 开发计划

后续架构调整、功能路线、逐文件任务和验收标准请参阅 [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md)。实施完成后可直接在计划文件中勾选对应任务。

## 项目结构

```text
GitManager/
├── core/       # libgit2 后端、异步调度和数据类型
├── dialogs/    # 提交等对话框
├── widgets/    # 提交图、分支、状态、差异和终端组件
├── MainWindow.*
├── main.cpp
└── CMakeLists.txt
```

## 注意事项

- Git 操作通过异步队列执行；网络传输可通过工具栏的 **Cancel** 主动取消，仓库切换后旧任务结果不会写入新界面。
- Push、Pull 和远程分支操作依赖仓库自身已配置的远程地址。Remote 菜单可设置仅保存在进程内存中的 HTTPS 用户名与密码/Token，也支持系统默认凭据；持久化凭据保险库仍在后续计划中。
- 删除分支、强制删除分支等操作会直接修改当前仓库，请确认后再执行。
- SSH URL 需要系统 OpenSSH；项目不依赖 `git.exe`。
