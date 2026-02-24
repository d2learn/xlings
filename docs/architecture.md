# xlings 项目架构

## 目录结构

```
xlings/
├── core/                   # 核心代码 (C++ + xim + xvm)
│   ├── main.cpp            # C++ 入口
│   ├── *.cppm              # C++23 模块 (config, log, platform, ...)
│   ├── platform/           # 平台实现 (linux, macos, windows)
│   ├── xim/                # 包管理器 (Lua, 自包含)
│   │   ├── xim.lua         # 入口
│   │   ├── CmdProcessor.lua
│   │   ├── platform.lua, common.lua
│   │   ├── base/, config/, pm/, index/, libxpkg/
│   │   └── pkgindex/       # 包索引 (git submodule)
│   └── xvm/                # 版本管理器 (Rust)
│       ├── src/, shim/, xvmlib/
│       └── config/
├── config/                 # 模板与 i18n（实际配置见 .xlings.json）
│   ├── i18n/, shell/
│   └── xlings.json         # 默认配置模板（合并到 .xlings.json 使用）
├── tools/                  # 安装脚本、模板、release 脚本
├── docs/
├── xmake.lua
└── README.md
```

## 模块说明

### C++23 主程序 (`core/*.cppm`)

主程序负责:
- CLI 参数解析和命令分发
- 路径配置管理 (`XLINGS_HOME`, `XLINGS_DATA`)
- 调用 xim (通过 `xmake xim -P $XLINGS_HOME`) 和 xvm (PATH 中二进制)

构建: `xmake build`

### xvm - 版本管理器 (`core/xvm/`)

Rust 实现的版本切换工具，支持多版本并存和快速切换。

路径解析优先级:
1. `XLINGS_DATA` 环境变量
2. `XLINGS_HOME` 环境变量 → `$XLINGS_HOME/data`
3. 默认 `$HOME/.xlings/data`

### xim - 包管理器 (`core/xim/`)

Lua 实现的跨平台包安装管理器，基于 xmake 的 Lua 运行环境。

xim 是自包含模块，所有依赖都在 `xim/` 目录内。  
调用方式: `xmake xim -P $XLINGS_HOME [args]`（由主程序根据 XLINGS_HOME 发起）

## 路径约定

| 路径 | 默认值 | 说明 |
|------|--------|------|
| `XLINGS_HOME` | `$HOME/.xlings` | xlings 安装目录 |
| `XLINGS_DATA` | `$XLINGS_HOME/data` | 运行数据目录 |
| `$XLINGS_DATA/bin` | | 可执行文件目录 |
| `$XLINGS_DATA/lib` | | 库文件目录 |
| `$XLINGS_DATA/xvm` | | xvm 数据目录 |

所有路径可通过环境变量或 **`.xlings.json`**（位于 XLINGS_HOME）自定义；该文件同时包含 data 路径、镜像与 xim 配置（由原 `config/xlings.json` 合并而来）。

## 构建

```bash
# 配置 (需指定 GCC SDK 路径，若使用 import std)
xmake f -m release --sdk=/path/to/gcc-15

# 构建 C++23 主程序
xmake build

# 构建 xvm (Rust)
cd core/xvm && cargo build --release
```

## 发布与本地测试 (release 脚本)

`tools/linux_release.sh` 会构建 C++ 与 xvm，组装 Linux 可用的 xlings 目录（默认 `build/xlings/`），并用 `install d2x -y` 做验证。包内包含：

- `bin/xlings`：入口脚本（设置 XLINGS_HOME 后执行主程序，解压即用）
- `bin/.xlings.real`：C++ 主程序
- `xim/`：Lua 包管理器
- `config/`：i18n（自包含）
- `xmake.lua`：供 `xmake xim -P` 使用的精简配置
- `data/`：xvm 及 xim 安装内容；内含预置 `data/xim/xim-pkgindex`（捆绑 pkgindex，无需首轮拉取）
- `.xlings.json`：**唯一配置文件**（data 路径 + 镜像/xim/repo 等，由原 config/xlings.json 合并）

```bash
./tools/linux_release.sh              # 输出到 build/xlings/ 并验证 install d2x -y
./tools/linux_release.sh /path/out   # 输出到指定目录

# 验证内容：bin/xlings、bin/.xlings.real 可执行；data/xim/xpkgs/d2x 存在；data/bin/d2x 存在。

# 解压后直接运行（无需设置环境变量）
cd build/xlings
./bin/xlings config
./bin/xlings install d2x -y
```
