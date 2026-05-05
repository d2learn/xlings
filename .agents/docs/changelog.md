# Change Log | [xlings论坛](https://forum.d2learn.org/category/9/xlings)

## 2026

### 2026-05 (v0.4.15)

- **下载缓存：sha256 缺失时改用 HEAD 探测，不再每次重下 (#TBD)**
  - `src/core/xim/downloader.cppm`：包索引中只声明 `url` 不声明 `sha256` 的条目（fixture 索引里约 8%；实际 `node.lua` / `nvm.lua` 之类 `_linux_url` helper 拼出来的 URL 占比更高）原来每次 `xlings install` 都会重下整包。新增 HEAD-fallback 缓存路径：`fs::file_size` 与服务端 `Content-Length` 比对 + `<destFile>.meta` sidecar 记录的 `Last-Modified` / `ETag` 与本次 HEAD 响应比对，命中则跳过下载。HEAD 失败（离线/服务端拒 HEAD）回退"文件存在即信"，airline-friendly。
  - sha256 路径在不匹配时主动 `fs::remove` 旧文件，预防将来 tinyhttps 启用 Range/resume 时拼出腐烂文件。
  - `src/libs/tinyhttps.cppm`：`query_remote_meta()` 返回完整 `RemoteFileMeta { contentLength, lastModified, etag, ... }`；老的 `query_content_length()` 改成薄包装。
  - `tests/unit/test_main.cpp::XimDownloaderTest::MetaSidecarRoundTrip` 锁定写入 + 读取 + 缺失 + 畸形行容错。

- **bump：mcpplibs-xpkg 0.0.37 → 0.0.38**
  - 带入 libxpkg 的 `os.dirs` glob 修复（POSIX 下 `ls -d "<pat>"` 双引号会让 shell 跳过 glob 展开，导致 `os.dirs("…/v*")` 静默返回空表）。

### 2026-04 (v0.4.3)

- **下载器：自动识别系统代理 (#222)**
  - `src/libs/tinyhttps.cppm` 增加 env 解析：`HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY`（含小写变体）按 libcurl 优先级生效；`NO_PROXY` 支持 exact / dotted-suffix / bare-suffix / `*` 通配。
  - 命中代理时通过 `log::debug("tinyhttps: using proxy ... for ...")` 输出，全局 `-v` 可见。
  - 复用 `mcpplibs::tinyhttps` 0.2.0 已内建的 HTTP CONNECT 隧道，无需上游改动。
  - 9 个 `TEST(Proxy, …)` 单测锁定行为矩阵。

- **TUI：Linux/macOS/Windows 图标一致 (#221)**
  - 移除 `src/ui/theme.cppm` 的 `#ifdef _WIN32` ASCII fallback：三个平台用同一组 BMP 图标 `○ ↓ ▾ ⊕ ✓ ✗ › ▸ ◆`，把 `⟐` `⚙` 这种缺字形的偏门字符换掉。
  - 新增 `tests/unit/test_main.cpp::ThemeIcons` 4 个单测：逐字节锁定 + 防 ASCII fallback + 强制 3-byte BMP UTF-8 + 渲染流抓 stdout 验字节。
  - 配套 `tests/e2e/tui_utf8_test.sh` 在 Linux/macOS CI 跑端到端编码验证。
  - macOS / 文档加 Windows 字体推荐段。

- **架构：移除 agent 子系统 (#220)**
  - 删除 `src/agent/` 整目录（14 个 .cppm，~3000 行）+ `src/libs/` 中 8 个 agent-only 模块。
  - 删除 `mcpplibs-llmapi` 外部依赖；`mcpplibs-tinyhttps` 保留（xim 下载器使用）。
  - 51 files changed, +119 / -16768 行；xlings 回归到纯包管理器 + xvm 运行时定位。

- **修复：`xlings remove <pkg>` 不再误删整包版本表 (#219)**
  - `src/core/xim/installer.cppm` 修复：当 uninstall hook 发出无版本号的 `xvm.remove(name)` 时，旧路径 `versions_mut().erase(name)` 把整个包条目擦掉（其它已装版本变孤儿）；现在改为用外层 resolved 的 `detachVersion` 兜底，`xvm::remove_version` 精确删一个；删后若有剩余版本且被删的是 active，自动按 semver 降序切到最高。
  - 新增 `xvm::pick_highest_version()`。
  - `tests/e2e/remove_multi_version_test.sh` 端到端回归（hermetic、私有 fixture index、Linux + macOS CI 都跑）。

- **CI：macOS bootstrap xlings v0.3.2 → v0.4.2**
  - `.github/workflows/xlings-ci-macos.yml` 和 `release.yml`：旧 v0.3.2 在 GitHub Actions 非 TTY 下载 LLVM 时 progress 输出被吞，导致 7+ 分钟无日志看似卡死；v0.4.2 进度行正常输出，CI 不再误判。

### 2026-03 (v0.4.0)

- **xvm C++ 集成：消除 Rust xvm 依赖**
  - Lua xvm 模块从 shell-out 调用 Rust `xvm` 二进制改为收集 `_XVM_OPS` 操作表
  - C++ 侧在 hook 执行后通过 `PackageExecutor::xvm_operations()` 读取并统一处理
  - 新增 `xvm.setup()` / `xvm.teardown()` 高层 API（一次调用注册程序/库/头文件）
  - VData 扩展 `includedir` / `libdir` 字段，支持头文件和库的 symlink 追踪
  - 头文件安装改为 symlink 方式（`install_headers()`），版本切换时自动切换
  - 卸载流程增强：自动清理 VersionDB 条目和 workspace 引用
  - 修复 shim.cppm 跨平台 PATH 分隔符问题（使用 `platform::PATH_SEPARATOR`）
  - 修复 commands.cppm 跨平台 symlink 问题（Windows 使用 junction/hardlink 回退）
  - 移除 Rust xvm 源码（core/xvm/Cargo.toml, src/, shim/, xvmlib/）
  - 移除 CI/release 脚本中所有 Rust 构建步骤和 xvm 二进制验证
  - 新增 7 个单元测试（VData 新字段 + 头文件 symlink 操作），总计 81 个测试

- **xim 核心模块 C++ 重写**
  - 将 xim 包管理器核心从 Lua/xmake 子进程架构迁移到原生 C++23 模块实现
  - 消除 `xmake xim -P ...` 子进程调用，install/remove/search/list/info/update 命令全部在 C++ 内完成
  - 新增 7 个 C++23 模块：`xlings.xim.types`、`xlings.xim.repo`、`xlings.xim.index`、`xlings.xim.resolver`、`xlings.xim.downloader`、`xlings.xim.installer`、`xlings.xim.commands`
  - 基于 libxpkg (C++ 库) 实现包索引构建、搜索、版本匹配和包加载
  - 新增 DAG 依赖解析器：DFS 拓扑排序、循环检测、已安装跳过
  - 新增并行下载器：`std::jthread` 并发控制、SHA256 校验、镜像支持
  - 安装编排器通过 libxpkg `PackageExecutor` 运行 Lua hook（install/config/uninstall）
  - CLI 直接调用 `xim::cmd_*` 函数，不再依赖 xmake 运行时
  - 51 个单元测试覆盖所有模块（类型、索引、解析器、下载器、安装器、命令）
  - 解决多个 GCC 15.1.0 C++23 模块 bug（ICE、链接符号缺失、运行时格式错误）

### 2026-02

- **Bug fixes: xim task / xvm path quoting / elfpatch tool detection**
  - Fix `xlings install`/`search` failing with "invalid task: xim" on Windows when `find_xim_project_dir` falls back to source tree root (which lacks `task("xim")` definition). Now falls through to `~/.xlings` installed layout automatically.
  - Fix `xvm add --path` breaking when path contains spaces (e.g. macOS `/Applications/My App/`). The `--path` argument is now properly quoted, consistent with `--alias`, `--type`, etc.
  - Replace `find_tool()` in elfpatch with direct execution probe (`_try_probe_tool`). Tools like `install_name_tool`/`otool` on macOS and `patchelf`/`readelf` on Linux are now detected by actually running them, with `try{}` catching failures gracefully and printing actionable hints.
  - Added `tests/e2e/bugfix_regression_test.sh` covering all three fixes.

- **xlings self install 安装逻辑优化**
  - data/subos 保留策略改为「直接不删除、选择性不覆盖」，不再使用备份/恢复
  - 升级时 data/subos 完全保留不合并，避免 subos 损坏
  - 移除「保留缓存数据」交互提示，data/subos 自动保留
  - 优化用户提示与打印布局

## 2025

### 2025-08

- **发布xvm-0.0.5 + xpkg/xscript复用机制**
  - 增加了库类型的多版本管理机制, 以及`xvm info`详情查询 - [PR](https://github.com/d2learn/xlings/pull/108) - 2025/8/16
  - xpkg/xscript 即是包也是程序(脚本)的复用机制 (示例: [musl-cross-make](https://github.com/d2learn/xim-pkgindex/blob/main/pkgs/m/musl-cross-make.lua)) - [PR](https://github.com/d2learn/xlings/pull/109) - 2025/8/14
- **文档:** 初步完善文档: [快速开始](https://xlings.d2learn.org/documents/quick-start/one-click-install.html)、[常用命令](https://xlings.d2learn.org/documents/commands/install.html)、 [xpkg包](https://xlings.d2learn.org/documents/xpkg/intro.html)、[参与贡献](https://xlings.d2learn.org/documents/community/contribute/issues.html) - [PR](https://github.com/d2learn/xlings-docs/commit/122b060855e4c41cd7f95801f2656bca0a5a6fc1) - 2025/8/9


### 2025-07

- **代码优化:** 修复一些bug并优化相关代码、适配macos - [commits](https://github.com/d2learn/xlings/commits/main/?since=2025-07-01&until=2025-07-31) - 2025/7

### 2025-06

- 跨平台: 初步支持MacOS平台、xim添加冲突解决功能(xpkg的`mutex_group`字段实现) - 2025/6

### 2025-05

- 新功能: 增加包索引网站、支持多语言i18n - 2025/5

### 2025-02

- d2x: 重构公开课/教程项目相关命令, 形成独立的d2x工具 - [PR](https://github.com/d2learn/xlings/pull/79) - 2025/2/19

### 2025-01

- xim: 增加archlinux上aur的支持 - [PR](https://github.com/d2learn/xlings/pull/67) - 2025/1/10
- xvm: 增加版本管理模块 - [文章](https://forum.d2learn.org/topic/62) / [PR](https://github.com/d2learn/xlings/pull/60) - 2025/1/1

## 2024

- xpkg增加自动匹配github上release的url功能 - [文章](http://forum.d2learn.org/post/208) - 2024/12/30
- xlings跨平台短命令 - [视频](https://www.bilibili.com/video/BV1dH6sYKEdB) - 2024/12/29
- xinstall模块: 重构&分离框架代码和包文件 - [包索引仓库](https://github.com/d2learn/xim-pkgindex) / [PR](https://github.com/d2learn/xlings/pull/49) -- 2024/12/16
- xinstall功能更新介绍 - [文章](https://forum.d2learn.org/topic/48) / [视频](https://www.bilibili.com/video/BV1ejzvY4Eg7/?share_source=copy_web&vd_source=2ab9f3bdf795fb473263ee1fc1d268d0)
- 增加DotNet/C#和java/jdk8环境的支持
- 增加windows模块和安装器自动加载功能, 以及WSL和ProjectGraph的安装支持 - [详情](http://forum.d2learn.org/post/96)
- 软件安装模块增加deps依赖配置和"递归"安装实现
- 初步xdeps项目依赖功能实现和配置文件格式初步确定
- install模块添加info功能并支持Rust安装