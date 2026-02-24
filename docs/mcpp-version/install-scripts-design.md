# xlings 安装脚本方案与设计

> **版本**: 0.2.0+  
> **更新时间**: 2026-02-24  
> **涉及文件**:
> - `tools/install-from-release.sh` / `tools/install-from-release.ps1` — release 包内安装脚本
> - `tools/other/quick_install.sh` / `tools/other/quick_install.ps1` — 一键在线安装脚本

---

## 1. 总体架构

安装流程分为两层，职责分离：

```
用户
 │
 ├─ 方式 A: 一键在线安装 (quick_install)
 │   │
 │   │  curl/irm 下载脚本 → 查询 GitHub API 获取最新版本
 │   │  → 下载对应平台 release 包 → 解压 → 调用 install.sh/install.ps1
 │   │
 │   └─────────────┐
 │                  ▼
 └─ 方式 B: 手动下载 release 包 → 解压 → 运行 install.sh / install.ps1
                    │
                    ▼
            install-from-release (包内安装器)
                    │
                    ├─ 校验包完整性 (bin/, subos/, xim/)
                    ├─ 拷贝到 XLINGS_HOME
                    ├─ 重建 subos/current 链接
                    ├─ 写入 shell profile / 注册表 PATH
                    └─ 验证安装结果
```

---

## 2. Release 包命名规范

| 平台 | 架构 | 包名格式 | 示例 |
|------|------|----------|------|
| Linux | x86_64 | `xlings-{ver}-linux-x86_64.tar.gz` | `xlings-0.2.0-linux-x86_64.tar.gz` |
| macOS | arm64 | `xlings-{ver}-macosx-arm64.tar.gz` | `xlings-0.2.0-macosx-arm64.tar.gz` |
| Windows | x86_64 | `xlings-{ver}-windows-x86_64.zip` | `xlings-0.2.0-windows-x86_64.zip` |

> **注意**: macOS 平台标识为 `macosx`（与 xmake 保持一致），不是 `macos`。

---

## 3. install-from-release — 包内安装脚本

### 3.1 定位

打包时由 release 脚本复制到包根目录（`install.sh` / `install.ps1`），用户解压后在包目录内直接运行。

### 3.2 Unix 版 (`install-from-release.sh`)

**入口**: `./install.sh`

**流程**:

```
1. 校验: 检查 bin/, subos/, xim/ 目录是否存在 → 确认在合法 release 包内
2. 检测 OS: Linux → /home/xlings, macOS → /Users/xlings (默认 XLINGS_HOME)
3. 目标目录: XLINGS_HOME 环境变量 > 默认值
4. 创建目录: 普通用户若无权限自动 sudo mkdir + chown
5. 拷贝内容: rsync (优先) 或 cp -a 到 XLINGS_HOME
6. 权限修复: chmod +x bin/* 和 subos/default/bin/*
7. 符号链接: 重建 subos/current → default
8. 写 PATH: 检测 .bashrc/.zshrc/.profile, 追加 export PATH
9. 验证: 运行 xlings -h 确认可执行
```

**关键设计点**:

- 支持 `XLINGS_HOME` 环境变量自定义安装路径
- 如已在 `XLINGS_HOME` 内运行则跳过拷贝
- PATH 写入是幂等的（先检查是否已存在）
- 仅写入第一个找到的 profile 文件，避免重复

### 3.3 Windows 版 (`install-from-release.ps1`)

**入口**: `powershell -ExecutionPolicy Bypass -File install.ps1`

**流程**:

```
1. 校验: 检查 bin\, subos\, xim\ 目录
2. 目标目录: XLINGS_HOME 环境变量 > C:\Users\Public\xlings
3. 拷贝内容: Copy-Item 递归拷贝到 XLINGS_HOME
4. Junction: 重建 subos\current → subos\default (NTFS junction)
5. 写 PATH: 通过 [System.Environment]::SetEnvironmentVariable 写入用户 PATH
6. 验证: 运行 xlings.exe -h
```

---

## 4. quick_install — 一键在线安装脚本

### 4.1 定位

面向终端用户的零前提安装入口，一行命令完成安装：

```bash
# Linux / macOS
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash

# Windows (PowerShell)
powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.ps1 | iex"
```

### 4.2 Unix 版 (`quick_install.sh`)

**前置依赖**: `curl`, `tar`

**流程**:

```
1. 检测平台:
   - OS: uname -s → linux / macos
   - Arch: uname -m → x86_64 / arm64
   - 映射: macos → macosx (release 命名规范)

2. 查询最新版本:
   - GitHub API: GET /repos/d2learn/xlings/releases/latest
   - 解析 tag_name 获取版本号

3. 构造下载 URL:
   - 格式: https://github.com/d2learn/xlings/releases/download/{tag}/xlings-{ver}-{platform}-{arch}.tar.gz
   - 支持 XLINGS_GITHUB_MIRROR 环境变量指定镜像前缀

4. 下载 & 解压:
   - mktemp 创建隔离临时目录
   - curl 下载 → tar 解压
   - trap EXIT 自动清理

5. 运行包内安装器:
   - cd 到解压目录
   - bash install.sh
```

**环境变量支持**:

| 变量 | 作用 | 示例 |
|------|------|------|
| `XLINGS_GITHUB_MIRROR` | 替换 GitHub 下载前缀 | `https://mirror.ghproxy.com/https://github.com` |
| `XLINGS_HOME` | 透传到 install-from-release.sh | `/opt/xlings` |

### 4.3 Windows 版 (`quick_install.ps1`)

**前置依赖**: PowerShell 5+

**流程**:

```
1. 检测架构:
   - 64-bit → x86_64
   - ARM64 → arm64

2. 查询最新版本:
   - Invoke-RestMethod GitHub API
   - 解析 tag_name

3. 构造下载 URL:
   - 格式: .../{tag}/xlings-{ver}-windows-{arch}.zip
   - 支持 XLINGS_GITHUB_MIRROR

4. 下载 & 解压:
   - 临时目录 (GUID 命名, 避免冲突)
   - Invoke-WebRequest 下载 → Expand-Archive 解压
   - finally 块自动清理

5. 运行包内安装器:
   - powershell -ExecutionPolicy Bypass -File install.ps1
```

---

## 5. 与旧版安装流程对比

### 旧版流程 (v0.0.4 及之前)

```
quick_install → 下载 main 分支 zip → 解压源码 → install.unix.sh / install.win.bat
                                                    │
                                                    ├─ 下载 xmake 二进制
                                                    ├─ 运行 xmake xlings --project=. unused self enforce-install
                                                    ├─ 创建系统用户 /home/xlings
                                                    └─ 从源码构建安装
```

**问题**:
- 依赖 `xmake` 构建系统（需额外下载）
- 依赖已废弃的 `task("xlings")` xmake 任务和 `self enforce-install` 子命令
- 需要从源码编译，耗时且需要编译工具链
- 硬编码路径（`/home/xlings`），需要 root 权限创建系统用户

### 新版流程 (v0.2.0+)

```
quick_install → GitHub API 查最新版 → 下载预编译 release 包 → install-from-release
                                                                  │
                                                                  ├─ 拷贝预编译文件到 XLINGS_HOME
                                                                  ├─ 重建 subos/current 链接
                                                                  └─ 配置 PATH
```

**改进**:
- 零编译依赖，下载即用
- 无需 xmake / git / 编译器
- 支持自定义安装路径（`XLINGS_HOME` 环境变量）
- 支持 GitHub 镜像加速（`XLINGS_GITHUB_MIRROR`）
- 自动检测 OS 和 CPU 架构，下载对应包
- 自动查询最新版本，无需硬编码版本号

---

## 6. 文件关系总览

```
tools/
├── install-from-release.sh      ← Unix 包内安装器 (打包时复制为 install.sh)
├── install-from-release.ps1     ← Windows 包内安装器 (打包时复制为 install.ps1)
├── linux_release.sh             ← Linux 打包脚本 (调用 install-from-release.sh)
├── macos_release.sh             ← macOS 打包脚本 (调用 install-from-release.sh)
├── windows_release.ps1          ← Windows 打包脚本 (调用 install-from-release.ps1)
└── other/
    ├── quick_install.sh         ← Unix 一键在线安装 (下载 release → 调用 install.sh)
    └── quick_install.ps1        ← Windows 一键在线安装 (下载 release → 调用 install.ps1)
```

**调用链**:

```
quick_install.sh ──下载──→ xlings-{ver}-{platform}-{arch}.tar.gz
                               │
                               └─ 内含 install.sh (= install-from-release.sh 的副本)
                                       │
                                       └─ 安装到 XLINGS_HOME

linux_release.sh ──构建──→ xlings-{ver}-linux-x86_64/
macos_release.sh ──构建──→ xlings-{ver}-macosx-arm64/
                               │
                               └─ cp install-from-release.sh → install.sh (打入包内)
```
