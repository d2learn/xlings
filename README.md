# xlings

一个自动化编译运行检测、教程教学类项目构建和管理工具

## 功能

- **项目模板** - 快速生成一个包括书籍、练习代码等内容的项目结构
- **自动化练习检测** - 编译器驱动开发学习
- **Markdown书籍** - 编写markdown文件生成在线电子书
- **常用工具下载** - 常用技术类工具/软件下载
- **多语言代码运行检测** - 自动识别代码文件进行(编译)运行, 并时实检测代码变化显示运行结果
  - c
  - cpp
  - python
  - ...
- **多项目管理和下载** - 多个项目管理、信息查看、下载等功能
- **AI提示引导** - 配置对应的大模型, 做为错误代码提示小助理
  - openai - dev
  - tongyi - ok
  - ...

---

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
Invoke-Expression (Invoke-Webrequest 'https://d2learn.org/xlings-install.ps1' -UseBasicParsing).Content
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

> 一键配置c语言环境

```bash
xlings install c
```

**软件安装**

> 一键安装vscode

```bash
xlings install vscode
```

### 搭建交互式教程或课程实验

> [项目搭建](https://d2learn.github.io/docs/xlings/chapter_3.html)

**注: 更多用法见社区文档 -> [xlings docs](https://d2learn.github.io/docs/xlings/chapter_0.html)**

## 示例

| examples | desc | other |
| --- | --- | --- |
| [d2ds](https://github.com/Sunrisepeak/d2ds) | 动手学数据结构项目 | |
| [d2cpp](https://github.com/d2learn/d2cpp) | 动手学C++项目 | |
| ... | ... | |

[更多示例](https://d2learn.github.io/courses)