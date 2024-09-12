# xlings

技术教程、课程作业、labs、及类[d2x]()项目快速构建和管理工具

## 功能

- **项目模板** - 快速生成一个包括书籍、练习代码等内容的项目结构
- **自动化练习检测** - 编译器驱动开发学习
- **Markdown书籍生成** - 编写markdown文件并生成在线电子书
- **多项目管理和下载** - 多个项目管理、信息查看、下载等功能
- **AI提示引导** - 配置对应的大模型, 做为错误代码提示小助理
  - openai - dev
  - tongyi - ok
  - ...
- **常用工具下载** - 常用工具一键下载

---

## 安装

### 获取源码

> 使用git获取源码或下载对应项目的zip包再解压

```bash
git clone git@github.com:Sunrisepeak/xlings.git
```

### 安装xlings

> 在项目的根目录执行安装脚本

**linux/macos**

```bash
bash tools/install.unix.sh
```

**windows**

```bash
tools\install.win.bat
```

## 用法简介

> 使用xlings下载[d2ds](https://github.com/Sunrisepeak/d2ds)并运行dsligs
>
> (使用xlings创建新项目步骤见 -> [创建类d2x项目](docs/quick_start.md))

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

## 示例

| examples | cases | other |
| --- | --- | --- |
| [d2c-e](examples/d2c) | | |
| [d2cpp-e](examples/d2cpp) | | |
| [d2ds](examples/d2ds) | [d2ds](https://github.com/Sunrisepeak/d2ds) | |