# OpenSSL 3.0.16 预编译包说明

本目录保留 OpenSSL 3.0.16 源码，同时直接保存项目构建所需的预编译开发包。Git Manager 的 CMake 只导入这些二进制文件，不再运行 Perl、`Configure`、`nmake` 或 `mingw32-make`。

## 来源与版本

- 原始目录：`D:/coding/v3.5/Libs/ThirdLibs/OpenSSLv3.0.6`
- 原始目录名是历史名称；`VERSION.dat` 和各平台 `opensslv.h` 均确认实际版本为 OpenSSL 3.0.16。
- 许可：Apache License 2.0，见同目录的 `LICENSE.txt`。

## 已保存平台

| 目录 | 平台 | 链接方式 |
| --- | --- | --- |
| `Win_x64` | Windows x64，VS2019/MSVC 14.29，`VC-WIN64A`、`/MD` | `libssl.lib`/`libcrypto.lib` + 两个运行时 DLL |
| `MacOS_arm64` | macOS ARM64 15.0+，Clang | 静态库 `libssl.a`、`libcrypto.a` |
| `Linux_x64` | Ubuntu 22.04+ / Linux x86_64，GLIBC 2.34+ | 动态库 `libssl.so.3`、`libcrypto.so.3` |

原始 `clang_x64` 目录名不准确：归档成员实际是 ARM64，最低系统版本为 macOS 15.0；复制到本项目时已更名为 `MacOS_arm64`。Linux 动态库由 Ubuntu 22.04 的 GCC 11.4 构建，引用 `GLIBC_2.34`，不能在 Ubuntu 20.04 上加载。

这些包不包含 Windows ARM64、macOS x86_64 或 Linux ARM 二进制。预编译 OpenSSL 默认只在 Windows MSVC x64 上启用；macOS 默认使用 SecureTransport，Linux 默认使用 OpenSSL 动态加载后端。需要显式使用仓库内 macOS/Linux 包时，可配置 `-DGITMANAGER_USE_PREBUILT_OPENSSL=ON`，并确保架构和最低系统版本匹配。

Windows 包是 Release/带调试信息的构建，没有单独的 Debug OpenSSL DLL。Debug 版 Git Manager 仍会使用同一组动态库。

## CA 根证书

`certs/cacert.pem` 来自 <https://curl.se/ca/cacert.pem>，由 curl 项目从 Mozilla 根证书库提取。当前文件下载于 2026-07-16：

- 文件 SHA-256：`3FF344E30B9B1ED2971044EABB438A08F2E2245DDB5F8AB1A3AD8B63AB4EAF91`
- 文件头记录的 Mozilla 数据时间：`Thu Jul 16 03:12:01 2026 GMT`
- 文件头记录的证书数据 SHA-256：`e57912808daef7b2b0fa4df2ccf17e47aeaf26c839a38f85c76003ebafd866bd`

CMake 会把 CA 文件和所需动态库复制到每个可执行文件旁。`LibGit2Backend` 启动时通过 `GIT_OPT_SET_SSL_CERT_LOCATIONS` 将其交给 libgit2，避免依赖开发机上的 OpenSSL 或 Git 安装目录。
