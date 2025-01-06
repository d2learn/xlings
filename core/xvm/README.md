# XVM | Xlings Version Manager

一个简单且通用的版本管理工具 - 支持多版本管理、支持工作空间和环境隔离、支持多版本的命令别名

---

## 下载安装

**使用xim进行安装**

```bash
xim --update index
xim -i xvm # 或 xlings install xvm
```

**直接下载**

> 直接下载压缩包&解压出可执行程序

[release地址](https://github.com/d2learn/xlings/releases/tag/xvm-dev)


注: 使用时需要把xvm程序放到一个已经加入PATH的目录里

## 基本用法

**xvm add | 添加程序**

> 支持添加不同版本, 也支持创建命令别名

```bash
# xvm add [target] [version] --path [bin-path] --alias [command/bin-file]
xvm add python 2.7.18 --path python2_dir --alias python2
xvm add python 3.12.3 --path python3_dir --alias python3.12
xvm add pv 0.0.1 --alias "python --version"
```

- `target`: 生效时可以使用的命令
- `version`: 这个是标识target的版本号, 建议和应用版本保持一致。如果是别名可以自定义
- `--path`: 程序所在的实际路径(可选)
- `--alias`: 在"所设置的路径"下实际执行的命令(可选), 默认为target

> 注: 默认第一次添加的版本作为全局工作空间的默认版本

**xvm list | 查询程序的所有版本**

> 支持模糊查询

```bash
# xvm list [target]
xvm list python
xvm list p
```

**xvm use | 切换版本**

> 可以切换到对应的版本, 版本号支持模糊匹配

```bash
# xvm use [target] [version]
xvm use python 3.12.3
xvm use python 2   # -> 2.7.18
```

**xvm current | 查询当前版本**

> 支持模糊匹配

```bash
# xvm current [target]
xvm current p
```

**xvm remove | 移除之前被添加的程序**

> 这里的移除只是从xvm的版本数据库中删除记录

```bash
# xvm remove [target] [version]
xvm remove pv 0.0.1
```

## 工作空间和环境隔离

> 目前xvm支持基于目录的工作空间, 可以和全局工作空间进行隔离(默认有一个全局工作空间)

### 基础命令

**创建工作空间**

```bash
# xvm workspace [target]
xvm workspace test
```

> 注: 工作空间, 默认时激活和继承全局工作空间版本情况的

**设置当前工作空间激活状态**

> 当工作空间未激活时就会使用默认的全局空间, 激活时会覆盖全局空间

```bash
# xvm workspace [target]
xvm workspace test --active false
xvm workspace test --active true
```

**设置当前工作空间是否继承全局空间**

```bash
# xvm workspace [target]
xvm workspace test --inherit false
xvm workspace test --inherit true
```

### 使用场景

> 在指定目录创建工作空间, 使用特定的版本

```bash
# 假设全局空间中python使用的版本是3
xvm workspace test # 创建工作空间
xvm use python 2   # 切换python版本
xvm current python # 查询当前版本
python --version   # 验证 - python2
cd ..
python --version   # 返回上级目录/版本会自动切换到全局版本python3
```

## 其他

**临时关闭xvm版本管理功能**

> 通过设置全局workspace的状态, 可以控制xvm的版本管理是否生效

```bash
xvm workspace global --active false
xvm workspace global --active true
```