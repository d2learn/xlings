# XIM | Xlings Installation Manager

一个支持**多版本共存**的包管理工具 - 不仅支持软件/工具安装、还支持**一键环境配置**

---

## 架构

v0.4.0 起，xim 核心由 C++23 原生实现，不再依赖 xmake Lua 子进程：

```
xlings CLI → xim::cmd_install/remove/search/...
                  ↓
             IndexManager (libxpkg 构建索引)
                  ↓
             Resolver (DAG 依赖解析)
                  ↓
             Downloader (并行下载 + SHA256)
                  ↓
             Installer (libxpkg executor 运行 Lua hook)
```

**模块：**

| 文件 | 模块 | 职责 |
|------|------|------|
| `types.cppm` | `xlings.xim.types` | PlanNode, InstallPlan, DownloadTask 等 |
| `repo.cppm` | `xlings.xim.repo` | Git 仓库同步 |
| `index.cppm` | `xlings.xim.index` | 索引管理 (libxpkg) |
| `resolver.cppm` | `xlings.xim.resolver` | DAG 依赖解析 |
| `downloader.cppm` | `xlings.xim.downloader` | 并行下载 |
| `installer.cppm` | `xlings.xim.installer` | 安装编排 |
| `commands.cppm` | `xlings.xim.commands` | CLI 命令实现 |

## 基础用法

**安装软件**

```bash
xlings install gcc
xlings install gcc@15 nodejs pnpm
```

**卸载软件**

```bash
xlings remove gcc
```

**搜索软件**

```bash
xlings search gcc
```

**查看包信息**

```bash
xlings info gcc
```

**列出所有包**

```bash
xlings list
xlings list gcc    # 带过滤
```

**更新包索引**

```bash
xlings update
```

## 包索引

- 包索引仓库: [xim-pkgindex](https://github.com/d2learn/xim-pkgindex)
- 添加 XPackage 文档: [add-xpackage](https://github.com/d2learn/xim-pkgindex/blob/main/docs/add-xpackage.md)
