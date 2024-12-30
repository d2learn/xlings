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
  <a href="https://github.com/d2learn/xim-pkgindex" target="_blank">Package Index</a>
  |
  <a href="https://github.com/orgs/d2learn/projects/5" target="_blank">开发看板</a>
</div>

<div align=center>一个用于编程学习、开发和课程搭建的开发者工具🛠️</div>
<div align=center>⌈软件安装、一键环境配置、项目依赖管理、跨平台跨语言的包管理(初步)⌋</div>
<div align=center>⌈实时编译运行、AI代码提示、教程教学项目搭建、练习代码自动检测、Demos示例集⌋</div>

---

## 最近动态

- xpkg增加自动匹配github上release的url功能 - [文章](http://forum.d2learn.org/post/208) - 2024/12/30
- xlings跨平台短命令 - [视频](https://www.bilibili.com/video/BV1dH6sYKEdB) - 2024/12/29
- xinstall模块: 重构&分离框架代码和包文件 - [包索引仓库](https://github.com/d2learn/xim-pkgindex) / [PR](https://github.com/d2learn/xlings/pull/49) -- 2024/12/16
- xinstall功能更新介绍 - [文章](https://forum.d2learn.org/topic/48) / [视频](https://www.bilibili.com/video/BV1ejzvY4Eg7/?share_source=copy_web&vd_source=2ab9f3bdf795fb473263ee1fc1d268d0)
- 增加DotNet/C#和java/jdk8环境的支持
- 增加windows模块和安装器自动加载功能, 以及WSL和ProjectGraph的安装支持 - [详情](http://forum.d2learn.org/post/96)
- 软件安装模块增加deps依赖配置和"递归"安装实现
- 初步xdeps项目依赖功能实现和配置文件格式初步确定
- install模块添加info功能并支持Rust安装
- 更多动态和讨论 -> [More](https://forum.d2learn.org/category/9/xlings)

[![Star History Chart](https://api.star-history.com/svg?repos=d2learn/xlings,d2learn/xim-pkgindex&type=Date)](https://star-history.com/#d2learn/xlings&d2learn/xim-pkgindex&Date)

## 快速安装

> 在命令行窗口执行一键安装命令

### Linux

```bash
curl -fsSL https://d2learn.org/xlings-install.sh | bash
```

or

```bash
wget https://d2learn.org/xlings-install.sh -O - | bash
```

### Windows - PowerShell

```bash
Invoke-Expression (Invoke-Webrequest 'https://d2learn.org/xlings-install.ps1.txt' -UseBasicParsing).Content
```

> **注: 更多安装方法 -> [xlings安装](https://d2learn.github.io/docs/xlings/chapter_1.html)**

## 用法简介

> - `xlings install`命令缩写: `xinstall`, `xim`
> - `xlings run`命令缩写: `xrun`

### XIM | 软件安装和环境自动配置

> XIM(Xlings Installation Manager)是xlings的安装管理模块,可以使用`xinstall`进行软件的安装和环境的配置

**编程环境安装配置**

> 一键安装配置对应的开发环境(python/java/rust/...)

```bash
xinstall c
xinstall cpp
xinstall python
```

**软件安装**

> 一键安装工具软件(vscode/vs/devcpp/nvm...)

```bash
xinstall vscode
```

**卸载软件和移除配置**

```bash
xinstall -r vscode
```

**搜索支持的软件或配置**

> xinstall模块支持模糊搜索, 如查询包含`vs`字符串的软件以及所有可以安装的版本

```bash
xinstall -s vs
```

**如何添加软件安装/环境配置文件到XIM的包索引仓库?**

> 通过添加一个XPackage包文件, 所有人就都能通过xinstall安装对应软件和配置功能

- 包索引仓库: [xim-pkgindex](https://github.com/d2learn/xim-pkgindex)
- 添加XPackage文档: [add-xpackage](https://github.com/d2learn/xim-pkgindex/blob/main/docs/add-xpackage.md)

> **注:** 使用`xinstall -h`命令, 可以获取XIM模块所有的命令行参数的使用和帮助信息

### XRUN | 运行代码

> 使用`xrun`可以运行代码。xlings会自动匹配编程语言, 并实时检查代码变化

```bash
xlings run your_code.py
xrun your_code.c
xrun your_code.cpp
```

### XDEPS | 项目依赖管理

> 在配置文件所在目录运行install命令安装项目依赖(`config.xlings`配置文件一般放到项目根目录)

**config.xlings配置文件示例**

```lua
xname = "ProjectName"
xdeps = {
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

### 搭建交互式教程或课程实验

- [项目搭建](https://d2learn.github.io/docs/xlings/chapter_3.html)
- [d2ds项目示例](https://github.com/d2learn/d2ds)
- [更多文档](https://d2learn.org/docs/xlings/chapter_0.html)

## 社区 & 交流

- 社区论坛: [d2learn-xlings](https://forum.d2learn.org/category/9/xlings)
- 交流群(Q): 1006282943

## 相关链接

- [主页](https://d2learn.org/xlings) : 工具动态和核心功能展示
- [xim-pkgindex](https://github.com/d2learn/xim-pkgindex) : xlings安装管理模块(XIM)的包索引仓库
- [xmake](https://github.com/xmake-io/xmake): 为xlings提供基础环境