# Change Log | [xlings论坛](https://forum.d2learn.org/category/9/xlings)

## 2026

### 2026-02

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