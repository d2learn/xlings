<div align="right" style="font-family: sans-serif; font-size: 14px; padding: 10px;">
  <b>
    添加你喜欢的 [ 软件、配置组合... ] 到包索引仓库 ➤ 
    <a href="https://xlings.d2learn.org/documents/community/contribute/add-xpkg.html" target="_blank" >
      Add XPkg
    </a>
  </b>
</div>

[![Xlings Test - CI](https://github.com/d2learn/xlings/actions/workflows/gitee-sync.yml/badge.svg?branch=main)](https://github.com/d2learn/xlings/actions/workflows/gitee-sync.yml)

<div align=center><img width="500" src="https://d2learn.org/xlings/xlings-install.gif"></div>

<div align="center">
  <a href="https://forum.d2learn.org/category/9/xlings" target="_blank"><img src="https://img.shields.io/badge/Forum-xlings-blue" /></a>
  <a href="https://d2learn.org" target="_blank"><img src="https://img.shields.io/badge/License-Apache2.0-success" alt="License"></a>
  <a href="https://www.bilibili.com/video/BV1d2DZYsErF" target="_blank"><img src="https://img.shields.io/badge/Video-bilibili-teal" alt="Bilibili"></a>
  <a href="https://youtu.be/uN4amaIAkZ0?si=MpZ6GfLHQoZRmNqc" target="_blank"><img src="https://img.shields.io/badge/Video-YouTube-red" alt="YouTube"></a>
</div>

<div align="center">
  <a href="README.md" target="_blank">中文</a>
  -
  <a href="README.en.md" target="_blank">English</a>
  |
  <a href="https://d2learn.github.io/xim-pkgindex" target="_blank">包索引</a>
  |
  <a href="https://xlings.d2learn.org/documents/quick-start/one-click-install.html" target="_blank">文档</a>
  |
  <a href="https://forum.d2learn.org/category/9/xlings" target="_blank">论坛</a>
</div>

<div align=center><b>一个高度抽象的包管理器</b> - 多版本管理 + 万物皆可成包</div>
<div align=center>应用、库、项目模板、环境配置、插件、脚本、游戏Mods......</div>

---

## 最近动态

- **文档:** 初步完善文档: [快速开始](https://xlings.d2learn.org/documents/quick-start/one-click-install.html)、[常用命令](https://xlings.d2learn.org/documents/commands/install.html)、 [xpkg包](https://xlings.d2learn.org/documents/xpkg/intro.html)、[参与贡献](https://xlings.d2learn.org/documents/community/contribute/issues.html) - [PR](https://github.com/d2learn/xlings-docs/commit/122b060855e4c41cd7f95801f2656bca0a5a6fc1) - 2025/8
- **代码优化:** 修复一些bug并优化相关代码、适配macos - [commits](https://github.com/d2learn/xlings/commits/main/?since=2025-07-01&until=2025-07-31) - 2025/7
- **跨平台:** 初步支持MacOS平台、xim添加冲突解决功能(xpkg的`mutex_group`字段实现) - 2025/6
- **新功能:** 增加包索引网站、支持多语言i18n - 2025/5
- 优化命令行使用体验: 常用命令支持xlings调用, 高级功能使用子命令xim/xvm/d2x - [PR](https://github.com/d2learn/xlings/pull/86) - 2025/4/15
- d2x: 重构公开课/教程项目相关命令, 形成独立的d2x工具 - [PR](https://github.com/d2learn/xlings/pull/79) - 2025/2/19
- xim: 增加archlinux上aur的支持 - [PR](https://github.com/d2learn/xlings/pull/67) - 2025/1/10
- xvm: 增加版本管理模块 - [文章](https://forum.d2learn.org/topic/62) / [PR](https://github.com/d2learn/xlings/pull/60) - 2025/1/1
- xpkg增加自动匹配github上release的url功能 - [文章](http://forum.d2learn.org/post/208) - 2024/12/30
- 更多动态和讨论 -> [More](https://forum.d2learn.org/category/9/xlings)

[![Star History Chart](https://api.star-history.com/svg?repos=d2learn/xlings,d2learn/xim-pkgindex&type=Date)](https://star-history.com/#d2learn/xlings&d2learn/xim-pkgindex&Date)

| 平台 | 使用体验 | 构建状态 | 备注 |
| --- | --- | --- | --- |
| linux | ⭐⭐⭐ | [![xlings-ci-linux](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-linux.yml/badge.svg)](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-linux.yml) | **欢迎参与文档编写** |
| windows | ⭐⭐ | [![xlings-ci-windows](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-windows.yml/badge.svg)](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-windows.yml) | 欢迎WIN用户参与贡献 |
| macos | ⭐ | [![xlings-ci-macos](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-macos.yml/badge.svg)](https://github.com/d2learn/xlings/actions/workflows/xlings-ci-macos.yml) | 初步支持 |

| 使用场景 | 简介 |
| --- | --- |
| **通用包管理器** | 类似apt/pacman/homebrew用于安装软件, 并支持安装多个版本和切换 |
| **复杂环境配置** | 一键配置由多个软件和配置项组合的环境, 即把配置当包看待进行分发和共享 |
| **创建项目模板** | 用于生成各种类型的项目模板, 并能自动配置好所需环境 |
| **组织/公司私有化部署** | 支持自建包索引和资源服务器, 实现内部软件、环境共享和统一管理 |

## 快速安装

> 在命令行窗口执行一键安装命令

### Linux/MacOS

```bash
curl -fsSL https://d2learn.org/xlings-install.sh | bash
```

or

```bash
wget https://d2learn.org/xlings-install.sh -O - | bash
```

### Windows - PowerShell

```bash
irm https://d2learn.org/xlings-install.ps1.txt | iex
```

## 用法简介

### 多版本软件安装及管理

> 支持**多版本共存**的包管理 - 不仅支持软件/工具安装、还支持**环境配置**

```bash
# 配置环境
xlings install c
xlings install python
# 安装工具
xlings install devcpp
xlings install vscode

# 安装指定版本package@version和版本切换
xlings install vscode@1.93.1
xlings use vscode 1.93.1

# 卸载指定版本
xlings remove vscode@1.93.1
```

高级用法见 -> [xim-readme](https://github.com/d2learn/xlings/tree/main/core/xim) / [xvm-readme](https://github.com/d2learn/xlings/tree/main/core/xvm)

### 项目搭建

> 搭建交互式的公开课或教程项目, 支持环境自动配置、电子书、练习代码自动检测...
>
> 示例项目: [d2ds | 动手学数据结构](https://github.com/d2learn/d2ds) / [mcpp-standard | 动手学现代C++](https://github.com/Sunrisepeak/mcpp-standard)

```bash
# 创建项目模板 - hello教程项目
xlings new hello
cd hello
# 安装项目依赖
xlings install
# 启动自动代码检测(编译器驱动开发模式)
xlings checker
```

高级用法见 -> [d2x-readme](https://github.com/d2learn/xlings/tree/main/core/d2x)

### 项目依赖管理

> 在配置文件所在目录运行install命令安装项目依赖(`config.xlings`配置文件一般放到项目根目录)

**config.xlings配置文件示例**

```lua
xname = "ProjectName"
xim = {
    cpp = "",
    python = "3.12",
    vs = "2022",
    -- postprocess cmds
    xppcmds = {
        "echo hello xlings",
    }
}
```

**一键安装项目依赖**

```bash
xlings install
```

## 社区交流 & 参与贡献

- 交流群(Q): 167535744 / 1006282943
- [社区论坛](https://forum.d2learn.org/category/9/xlings): 相关技术、功能、问题的交流讨论
- [参与贡献](https://xlings.d2learn.org/documents/community/contribute/issues.html): 问题处理、文档编写、添加新xpkg包、项目维护

> 注: 复杂性问题(技术、环境等问题)推荐在论坛发帖, 并详细描述问题细节, 能更有效于问题的解决和复用

## 相关链接

- [主页](https://xlings.d2learn.org) : 工具动态和核心功能介绍和文档
- [xim-pkgindex](https://github.com/d2learn/xim-pkgindex) : xlings安装管理模块(XIM)的包索引仓库
- [xmake](https://github.com/xmake-io/xmake): 为xlings提供基础环境