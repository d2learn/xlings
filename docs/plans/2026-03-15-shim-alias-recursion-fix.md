# 修复: Shim alias 自引用导致递归

## 问题

在 subos 环境中安装带 alias 的包（如 gcc）后，shim 调用 `platform::exec()` 执行 alias 命令时，
命令中的程序名通过 PATH 解析可能解析回 shim 自身，导致递归到 depth=8。

**根因**: alias 分支使用 `std::system()` 执行命令，程序名通过 PATH 解析。
而非 alias 分支正确地使用了 `resolve_executable()` + `execvp()` 绕过了 PATH。

## 修复方案

在 alias 分支执行命令前，提取 alias 的第一个词（命令名），通过 `resolve_executable()`
解析为完整路径，替换到命令字符串中：

1. 解析成功 → 使用完整路径替换命令名，避免 PATH 查找
2. 解析失败且命令名 == program_name → 明确报错（自引用但找不到真实二进制）
3. 解析失败且命令名 != program_name → 保持原样（可能是系统命令）

## 修改文件

| 文件 | 操作 |
|------|------|
| `src/core/xvm/shim.cppm` | alias 分支添加路径解析 |
| `tests/unit/test_main.cpp` | 新增 2 个 XvmShimTest 用例 |
| `tests/e2e/shim_alias_recursion_test.sh` | 新增 E2E 测试 |

## 验证

1. `rm -rf build && xmake build` — 编译通过
2. `xmake run xlings-test` — 单元测试通过
3. `bash tests/e2e/shim_alias_recursion_test.sh` — E2E 测试通过
