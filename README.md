# xlings

**强调实践、课程实验等技术学习类项目**的快速构建和管理工具。支持**项目模板、自动练习检测、markdown书籍、项目管理**等功能

> 项目管理功能待完善

---

## 安装

### 获取源码

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

## 快速使用

> 使用xlings快速创建一个项目(以clings为例)

### 项目配置

> 在一个空目录中新建一个config.xlings文件, 填写描述项目的基础信息

```lua
xlings_name = "clings" -- 项目名
xlings_lang = "c" -- 项目中使用的编程语言
```

### 生成项目结构

> 在config.xlings同目录执行项目初始化

```bash
xlings init
```

### 运行自动练习检测

> 使用`xlings [项目名]`开启自动练习检测

```bash
xlings clings # clings 是配置文件中配置的项目名
```

或

```bash
xlings checker
```

### 更多xlings命令

```bash
xlings version: pre-v0.0.1

Usage: $ xlings [command] [target]

Commands:
         init,           init projects by config.xlings
         checker,        start auto-exercises from target
         book,           open book in your default browser
         update,         update xlings to the latest version
         uninstall,      uninstall xlings
         help,           help info

repo: https://github.com/Sunrisepeak/xlings
```

## 示例

| examples | cases | other |
| --- | --- | --- |
| [d2c-e](examples/d2c) | | |
| [d2cpp-e](examples/d2cpp) | | |
| [d2ds](examples/d2ds) | [d2ds](https://github.com/Sunrisepeak/d2ds) | |