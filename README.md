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

## 安装

### 方式一: 命令行安装

**使用git获取源码**

```bash
git clone git@github.com:Sunrisepeak/xlings.git
```

**安装xlings**

> 在项目的根目录执行安装脚本

**linux/macos**

```bash
bash tools/install.unix.sh
```

**windows**

```bash
tools\install.win.bat
```

### 方式二: 下载zip包安装

**下载压缩包** -> [xlings.zip](https://github.com/d2learn/xlings/archive/refs/heads/main.zip)

**解压zip并运行安装脚本**

- windows: 双击解压后 tools目录中的`install.win.bat`进行安装
- linux: 鼠标右键点击 tools目录中的`install.unix.sh`选择**运行程序选项**进行安装

## 用法简介

> 以xlings下载[d2ds](https://github.com/Sunrisepeak/d2ds)并运行dslings练习代码为例

### 下载项目

> 下载d2ds项目到当前目录

```bash
xlings drepo d2ds
```

注: `xlings drepo`可以查看项目信息

### 运行项目练习

> 运行d2ds项目中的dslings代码练习

```bash
cd d2ds
xlings dslings
```

### 打开项目书籍

```bash
xlings book
```

### 更多常用命令

```bash
xlings version: pre-v0.0.1

Usage: $ xlings [command] [target]

Commands:
	 run,      	 easy to run target - sourcecode file
	 install,  	 install software/env(target)
	 drepo,    	 print drepo info or download drepo(target)
	 update,   	 update xlings to the latest version
	 uninstall,	 uninstall xlings
	 help,     	 help info

Project Commands: (need config.xlings)
	 init,     	 init project by config.xlings
	 book,     	 open project's book in default browser
	 checker,  	 start project's auto-exercises from target

repo: https://github.com/d2learn/xlings
```

> 注: 使用xlings创建新项目步骤见 -> [创建类d2x项目](docs/quick_start.md)

## 示例

| examples | desc | other |
| --- | --- | --- |
| [d2ds](https://github.com/Sunrisepeak/d2ds) | 动手学数据结构项目 | |
| [d2cpp](https://github.com/d2learn/d2cpp) | 动手学C++项目 | |
| ... | ... | |