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

### Linux

```bash
curl -fsSL https://github.com/d2learn/xlings/raw/refs/heads/main/tools/other/quick_install.sh | bash
```

or

```bash
wget https://github.com/d2learn/xlings/raw/refs/heads/main/tools/other/quick_install.sh -O - | bash
```

### Windows - PowerShell

```bash
Invoke-Expression (Invoke-Webrequest 'https://github.com/d2learn/xlings/raw/refs/heads/main/tools/other/quick_install.ps1' -UseBasicParsing).Content
```

**注: 更多安装方法和使用文档见 -> [xlings docs](https://d2learn.github.io/docs/xlings/chapter_0.html)**

## 示例

| examples | desc | other |
| --- | --- | --- |
| [d2ds](https://github.com/Sunrisepeak/d2ds) | 动手学数据结构项目 | |
| [d2cpp](https://github.com/d2learn/d2cpp) | 动手学C++项目 | |
| ... | ... | |

[更多示例](https://d2learn.github.io/courses)