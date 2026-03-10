# XVM | Xlings Version Manager

版本管理功能已融合到 xlings 单二进制中，不再需要独立的 xvm 程序。

---

## 基本用法

### 安装并注册版本

```bash
# 安装包后自动注册到版本数据库
xlings install gcc@15
xlings install node
```

安装完成后，版本信息自动写入 `~/.xlings/.xlings.json` 的 `versions` 段。

### 切换版本

```bash
# xlings use <target> <version>
xlings use gcc 15      # 模糊匹配: 15 → 15.1.0
xlings use gcc 14.2    # 精确匹配: 14.2 → 14.2.0
xlings use node 22
```

`use` 命令会：
1. 更新当前 subos 的 workspace（`~/.xlings/subos/<active>/.xlings.json`）
2. 在 subos 的 `bin/` 下创建 shim 硬链接

### 查看版本

```bash
# 查看某个工具的所有已注册版本
xlings use gcc           # 不带版本号时列出所有版本
xlings info gcc          # 查看包详细信息
```

### Shim 机制

版本切换后，在 subos/bin/ 中创建的 shim 会硬链接到 xlings 二进制。当通过 shim 执行时：

```bash
gcc --version       # argv[0]=gcc → shim 模式 → 查版本 → exec 真实 gcc
g++ -o main main.cpp  # argv[0]=g++ → 查 bindings → exec 真实 g++-15
```

shim 分发流程：
1. 检测 `argv[0]` 提取程序名
2. 读取 effective workspace（项目 > subos > 全局）
3. 模糊匹配版本号
4. 展开路径变量 `${XLINGS_HOME}`
5. 设置环境变量
6. `execvp` 真实程序

## 版本视图 (Subos)

每个 subos 维护独立的版本视图：

```bash
xlings subos new dev         # 创建 dev 环境
xlings subos use dev         # 切换到 dev
xlings use gcc 14            # 仅影响 dev 的版本视图
xlings subos use default     # 切回默认，gcc 版本不变
```

## 项目级配置

在项目目录创建 `.xlings.json` 可覆盖版本：

```json
{
  "workspace": {
    "gcc": "14.2.0"
  }
}
```

在此目录下执行 `gcc` 时，shim 会使用 14.2.0 版本（不影响全局设置）。

## 配置层级

```
优先级: 项目配置 > 当前 subos 配置 > 全局配置 > 硬编码默认值
```

| 文件 | 内容 |
|------|------|
| `~/.xlings/.xlings.json` | 全局 versions + lang + mirror + activeSubos |
| `~/.xlings/subos/<name>/.xlings.json` | workspace（版本视图） |
| `<project>/.xlings.json` | workspace 覆盖 + 本地 versions |

## 从旧版 xvm 迁移

| 旧 xvm 命令 | 新命令 |
|-------------|--------|
| `xvm add <target> <ver> --path <path>` | 通过 `.xlings.json` versions 段配置 |
| `xvm use <target> <ver>` | `xlings use <target> <ver>` |
| `xvm list <target>` | `xlings use <target>` (不带版本号) |
| `xvm current <target>` | `xlings use <target>` |
| `xvm remove <target> <ver>` | `xlings remove <target>` |
