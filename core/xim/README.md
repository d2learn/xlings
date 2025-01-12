# XIM | Xlings Installation Manager

一个支持**多版本共存**的包管理工具 - 不仅支持软件/工具安装、还支持**一键环境配置**

---

## 基础用法

**编程环境安装配置**

> 一键安装配置对应的开发环境(python/java/rust/...)

```bash
xim c
xim cpp
xim python
```

**软件安装**

> 一键安装工具软件(vscode/vs/devcpp/nvm...)

```bash
xim vscode
```

**卸载软件和移除配置**

```bash
xim -r vscode
```

**搜索支持的软件或配置**

> xim模块支持模糊搜索, 如查询包含`vs`字符串的软件以及所有可以安装的版本

```bash
xim -s vs
```

**查看已安装的软件**

```bash
xim -l
```

## 包索引

**更新索引**

```bash
xim --update index
```

**如何添加软件安装/环境配置文件到XIM的包索引仓库?**

> 通过添加一个XPackage包文件, 所有人就都能通过xim安装对应软件和配置功能

- 包索引仓库: [xim-pkgindex](https://github.com/d2learn/xim-pkgindex)
- 添加XPackage文档: [add-xpackage](https://github.com/d2learn/xim-pkgindex/blob/main/docs/add-xpackage.md)

> **注:** 使用`xim -h`命令, 可以获取XIM模块所有的命令行参数的使用和帮助信息