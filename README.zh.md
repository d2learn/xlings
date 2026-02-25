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
[包索引]: https://d2learn.github.io/xim-pkgindex
[XPKG包]: https://xlings.d2learn.org/documents/xpkg/intro.html
[贡献]: https://xlings.d2learn.org/documents/community/contribute/add-xpkg.html
[论坛]: https://forum.d2learn.org/category/9/xlings

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
curl -fsSL https://github.com/d2learn/xlings/blob/main/tools/other/quick_install.sh | bash
```

#### Windows - PowerShell

```bash
irm https://github.com/d2learn/xlings/blob/main/tools/other/quick_install.ps1 | iex
```


### 使用

**`类型1: 安装软件/工具`**

```bash
xlings install vscode
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

> [!CAUTION]
> xlings 正在从 Lua 迁移到 MC++ 并进行模块化重构，部分包在迁移期间可能存在不稳定的情况。如遇问题，请通过 [Issues] 或 [论坛] 反馈。

[Issues]: https://github.com/d2learn/xlings/issues

## 社区

- 交流群 (Q): 167535744 / 1006282943
- [论坛](https://forum.d2learn.org/category/9/xlings): 相关技术、功能、问题的交流讨论

## 参与贡献

- [问题处理和Bug修复](https://xlings.d2learn.org/documents/community/contribute/issues.html)
- [增加新的xpkg包](https://xlings.d2learn.org/documents/community/contribute/add-xpkg.html)
- [编写文档](https://xlings.d2learn.org/documents/community/contribute/documentation.html)

**👥贡献者**

[![Star History Chart](https://api.star-history.com/svg?repos=d2learn/xlings,d2learn/xim-pkgindex&type=Date)](https://star-history.com/#d2learn/xlings&d2learn/xim-pkgindex&Date)

<a href="https://github.com/d2learn/xlings/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=d2learn/xlings" />
</a>
