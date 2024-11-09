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
  -
  Other
</div>

<div align=center>一个用于编程学习、开发和课程搭建的开发者工具🛠️</div>
<div align=center>⌈软件安装、一键环境配置、项目依赖管理⌋</div>
<div align=center>⌈实时编译运行、AI代码提示、教程教学项目搭建、Demos示例集⌋</div>


---

## 最近动态

- 软件安装模块增加deps依赖配置和"递归"安装实现
- 初步xdeps项目依赖功能实现和配置文件格式初步确定
- install模块添加info功能并支持Rust安装
- 支持Dev-C++安装 - [详情](http://forum.d2learn.org/post/82)
- run命令跨存储盘(windows)使用 - [详情](http://forum.d2learn.org/post/66)
- 更多动态和讨论 -> [More](https://forum.d2learn.org/category/9/xlings)

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

### 运行代码

> xlings会自动匹配编程语言, 并实时检查代码变化

```bash
xlings run your_code.py
xlings run your_code.c
xlings run your_code.cpp
```

### 环境配置和软件安装

**环境配置**

> 一键配置c语言开发环境

```bash
xlings install c
```

> 一键配置rust开发环境

```bash
xlings install rust
```

**软件安装**

> 一键安装vscode

```bash
xlings install vscode
```

### 项目依赖管理

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

## 相关链接

- [主页](https://d2learn.org/xlings) : 工具动态和核心功能展示
- [论坛](https://forum.d2learn.org/category/9/xlings) : 问题反馈、项目开发、想法交流
- [xmake](https://github.com/xmake-io/xmake): 为xlings提供基础环境