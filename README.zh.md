<div align=right>

  [中文] | [English] | [Todo]
</div>

<div align=center>
  <img width="120" src="https://xlings.d2learn.org/imgs/xlings-logo.png">

  <em>Xlings | 高度抽象的 [ 包管理器 ] - <b>"多版本管理 + 万物皆可成包"</b></em>

  <b> [官网] | [快速开始] | [包索引] | [XPKG包] | [贡献] | [论坛] </b>
</div>

[中文]: README.zh.md
[繁體中文]: README.zh.hant.md
[English]: README.md
[Todo]: README.md

[官网]: https://xlings.d2learn.org
[快速开始]: https://xlings.d2learn.org/documents/quick-start/one-click-install.html
[包索引]: https://openxlings.github.io/xim-pkgindex
[XPKG包]: https://xlings.d2learn.org/documents/xpkg/intro.html
[贡献]: https://xlings.d2learn.org/documents/community/contribute/add-xpkg.html
[论坛]: https://forum.d2learn.org/category/9/xlings


> [!CAUTION]
> xlings 正在从 Lua 迁移到 MC++ 并进行模块化重构，部分包在迁移期间可能存在不稳定的情况。如遇问题，请通过 [Issues] 或 [论坛] 反馈。

[Issues]: https://github.com/openxlings/xlings/issues

## 快速开始

### 安装 (Github)


<details>
  <summary>点击查看xlings安装命令 (旧)</summary>

---

#### Linux/MacOS

```bash
curl -fsSL https://d2learn.org/xlings-install.sh | bash
```

#### Windows - PowerShell

```bash
irm https://d2learn.org/xlings-install.ps1.txt | iex
```

> tips: xlings -> [details](https://xlings.d2learn.org)

---

</details>

#### Linux/MacOS

```bash
curl -fsSL https://raw.githubusercontent.com/openxlings/xlings/refs/heads/main/tools/other/quick_install.sh | bash
```

#### Windows - PowerShell

```bash
irm https://raw.githubusercontent.com/openxlings/xlings/refs/heads/main/tools/other/quick_install.ps1 | iex
```


### 使用

**`类型1: 安装软件/工具`**

```bash
xlings install code
xlings install devcpp
xlings install gcc@15
```

**`类型2: 配置环境`**

```bash
xlings install config:rust-crates-mirror
xlings install config:xxx
```

**`类型3: 安装教程` - [交互式C++教程](https://github.com/Sunrisepeak/mcpp-standard)**

```bash
xlings install d2x:mcpp-standard
```

👉 [更多细节...](https://xlings.d2learn.org/documents/quick-start/install-and-version.html)

### SubOS - 环境隔离模式

> xlings支持通过subos命令, 创建隔离的工作空间 

**全局隔离环境**

> 创建一个隔离的环境, 并安装node

```
# 0.创建一个 subos 环境
xlings subos new my-subos

# 1.查看所有的 subos 环境
xlings subos list

# 2.切换环境到 my-subos
xlings subos use my-subos

# 3.在隔离环境中安装 node (不影响系统/host环境/其他subos)
xlings install node@24 -y
node --version

# 4.删除隔离空间
xlings subos remove my-subos
```

**项目隔离环境**

> xlings 支持通过`.xlings.json`配置文件, 配置项目的隔离环境, 并能一键安装配置项目的环境. 下面是真实 [d2x](https://github.com/d2learn/d2x) 项目的配置文件:

```
{
  "workspace": {
    "xmake": "3.0.7",
    "gcc": { "linux": "15.1.0" },
    "openssl": { "linux": "3.1.5" },
    "llvm": { "macos": "20" }
  }
}
```

一键安装&配置项目环境

> 在项目目录运行下面的一键安装命令

```
xlings install
```

**注:** subos机制, 使用的是 `[ 版本视图 + 引用计数机制 ]` 避免了大量包文件的重复下载

## 社区

- 交流群 (Q): 167535744 / 1006282943
- [论坛](https://forum.d2learn.org/category/9/xlings): 相关技术、功能、问题的交流讨论

## 参与贡献

- [问题处理和Bug修复](https://xlings.d2learn.org/documents/community/contribute/issues.html)
- [增加新的xpkg包](https://xlings.d2learn.org/documents/community/contribute/add-xpkg.html)
- [编写文档](https://xlings.d2learn.org/documents/community/contribute/documentation.html)

**👥贡献者**

[![Star History Chart](https://api.star-history.com/svg?repos=openxlings/xlings,openxlings/xim-pkgindex&type=Date)](https://star-history.com/#openxlings/xlings&openxlings/xim-pkgindex&Date)

<a href="https://github.com/openxlings/xlings/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=openxlings/xlings" />
</a>
