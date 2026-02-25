# 混合视图子方案：库路径优先级保证与 xpkg 规范

> 父文档: [xpkgs-subos-hybrid-design.md](xpkgs-subos-hybrid-design.md)
> 关联任务: [tasks/T23-hybrid-view-impl.md](tasks/T23-hybrid-view-impl.md)

## 1. 子方案目标

将父方案中的库解析优先级变成可执行约束，而不是文档约定：

1. 程序专属闭包路径
2. 程序二进制内 RUNPATH/RPATH
3. `subos/lib` 默认聚合路径
4. 系统默认搜索路径

本子方案解决三个问题：

- 如何在运行时稳定保证该顺序
- 如何避免 xpkg 包定义绕过顺序
- 如何用 CI 和测试防回归

## 2. 设计原则

- 单一真相：最终 `LD_LIBRARY_PATH` 只能由 shim 组装
- 元数据与结果分离：xpkg 只声明闭包输入，不直接写最终路径
- 默认兼容：未声明闭包时仍可回退到 `subos/lib`
- 可观测：最终路径可打印、可测试、可审计

## 3. 运行时保证机制（实现约束）

### 3.1 单入口组装

在 `core/xvm/xvmlib/shims.rs` 增加单一组装函数（示意）：

```rust
fn compose_ld_library_path(program_closure: &[String], workspace_lib: &str) -> String
```

组装规则固定为：

- 先拼 `program_closure`
- 再拼 `workspace_lib`（即 `subos/lib`）
- 最后拼当前环境继承值（若有）
- 全流程去重并保持稳定顺序

### 3.2 禁止旁路写入

禁止其他代码路径直接写最终 `LD_LIBRARY_PATH` 字符串。  
如必须兼容历史逻辑，统一改为写入“输入字段”，由组装函数消费。

### 3.3 去重与稳定排序

需要提供路径归一化与去重逻辑：

- 规范化绝对路径
- 去重保序（first-win）
- 空路径过滤

这能避免同一路径重复拼接导致的不可预期行为。

### 3.4 闭包生成时机（关键约束）

闭包路径不得在发布阶段写死为机器绝对路径。应采用：

- 发布产物仅携带依赖规则（deps/版本/占位符）
- 在当前环境中动态生成最终路径（运行时或激活时）
- 支持变量占位（如 `${XLINGS_DATA}`、`${XLINGS_SUBOS}`）并在 shim 侧展开

这样可保证 `XLINGS_HOME` 变更后仍能得到正确路径，不受构建机目录影响。

## 4. xpkg 规范（输入约束）

### 4.1 字段定义（建议）

建议在 xpkg config 元数据层使用以下输入字段：

- `XLINGS_PROGRAM_LIBPATH`
  - 含义：程序专属闭包路径（通常由框架根据 deps 自动生成）
  - 特性：最高优先级输入，面向“隔离运行”
- `XLINGS_EXTRA_LIBPATH`
  - 含义：包作者的补充库路径（可选）
  - 特性：仅作为补充输入，不直接覆盖闭包路径

`LD_LIBRARY_PATH` 不作为 xpkg 直写字段（不允许在包定义中直接设置最终值）。

### 4.2 规范要求（红线）

- xpkg 包定义层禁止直接设置 `LD_LIBRARY_PATH`
- 包作者只能声明“输入”，不能声明“最终值”
- 最终 `LD_LIBRARY_PATH` 必须由 shim 统一组装并去重
- 组装顺序固定：`PROGRAM_LIBPATH -> EXTRA_LIBPATH -> subos/lib -> inherited`

### 4.3 最简单示例

#### 旧写法（不推荐）

```lua
xvm.add("demo-tool", {
    envs = {
        LD_LIBRARY_PATH = "/abs/a:/abs/b"
    }
})
```

问题：包作者直接写最终路径，无法保证全局顺序，也容易和其他包冲突。

#### 新写法（推荐）

```lua
xvm.add("demo-tool", {
    envs = {
        XLINGS_PROGRAM_LIBPATH = "/data/xpkgs/libA/1.0/lib64:/data/xpkgs/libB/2.1/lib64",
        XLINGS_EXTRA_LIBPATH = "/opt/vendor/lib"
    }
})
```

运行时由 shim 统一合成（示意）：

```text
LD_LIBRARY_PATH=
/data/xpkgs/libA/1.0/lib64:
/data/xpkgs/libB/2.1/lib64:
/opt/vendor/lib:
${XLINGS_SUBOS}/lib:
${INHERITED_LD_LIBRARY_PATH}
```

如果需要跨机器可移植，推荐在元数据层保存变量形式而非硬编码绝对路径，例如：

```text
${XLINGS_DATA}/xpkgs/libA/1.0/lib64
${XLINGS_DATA}/xpkgs/libB/2.1/lib64
```

### 4.4 项目内真实示例（建议对照）

- `xim-pkgindex/pkgs/d/d2x.lua`
  - Linux 依赖 `glibc` + `openssl@3.1.5`
  - 作为“程序闭包输入由 deps 推导”的典型示例
- `xim-pkgindex/pkgs/g/glibc.lua`
  - 主要注册 libc/libm/loader 等基础库，不直写 `LD_LIBRARY_PATH`
  - 作为“基础运行时库通过视图层暴露”的示例
- `xim-pkgindex/pkgs/o/openssl.lua`
  - 注册 `libssl.so.3`/`libcrypto.so.3`，不直写 `LD_LIBRARY_PATH`
  - 作为“共享库包按 deps + 视图工作”的示例
- `xim-pkgindex/pkgs/m/musl-gcc.lua`
  - `musl-ldd` / `musl-loader` 使用 `XLINGS_EXTRA_LIBPATH`
  - 作为“历史 LD_LIBRARY_PATH 写法迁移到输入字段”的示例

### 4.5 过渡策略

对历史包分三阶段兼容：

1. **兼容阶段**：若发现旧字段 `LD_LIBRARY_PATH`，自动迁移为 `XLINGS_EXTRA_LIBPATH`
2. **告警阶段**：输出 warning，并在 CI 标记为“待修复”
3. **收敛阶段**：升级为 hard error，阻止新包继续直写

## 5. 校验与防回归

### 5.1 静态检查

在 CI 增加规则：

- 扫描 xpkg 文件，若发现直接写 `LD_LIBRARY_PATH`，标记违规
- 扫描关键可执行文件，检查 `RUNPATH/INTERP` 不含构建机私有路径

### 5.2 运行时集成测试

最小回归用例：

- A 依赖 `b@0.0.1`，C 依赖 `b@0.0.2`
- 同一 subos 下启动 A/C，验证各自命中不同闭包路径
- 不启用闭包的旧程序仍可通过 `subos/lib` 正常运行

### 5.3 诊断开关

增加调试输出（例如 `XLINGS_DEBUG_LIBPATH=1`）打印：

- program closure paths
- aggregate fallback path
- final LD_LIBRARY_PATH
- 去重后顺序

## 6. 与 T23 的映射

- T23-A：实现 shim 单入口组装 + 去重逻辑
- T23-B：引入/迁移 xpkg 输入字段，禁直写 `LD_LIBRARY_PATH`
- T23-C：补齐 CI 静态检查与多版本并存集成测试

## 7. 验收标准

- 文档中的 4 级优先级可由代码路径唯一推导
- 任意程序的最终库路径可观测、可复现
- xpkg 直写 `LD_LIBRARY_PATH` 在 CI 被拦截
- 多版本并存测试稳定通过

