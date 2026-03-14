# Agent 认知架构升级 Spec

## 1. 概述

当前 agent 使用标准 tool calling 机制：LLM 每次调用一个 tool → 等结果 → 再调用下一个。这导致多步操作需要多次 LLM 调用，效率低且难以表达控制流。

新架构：LLM 生成 Lua 代码片段作为动作编排，系统在沙箱中执行。**代码即描述，描述即代码。**

## 2. 认知层次模型

| Layer | 名称 | 人类类比 | Agent 实现 |
|-------|------|---------|-----------|
| 0 | Primitive | 脊髓反射 | 单个 capability 调用（search_packages, remove_package...） |
| 1 | Routine | 条件反射 | 可复用的 Primitive 序列（保存的 Lua 函数），习得后可跳过 LLM |
| 2 | Decision | 大脑主动决策 | LLM 分析+推理+生成 Lua 编排代码 |
| 3 | Plan | 任务规划 | 多步 Decision/Routine 的有序组合 |

## 3. 核心分类：确定性 vs 决策

- **确定性（Deterministic）**: Primitive, Routine — 结果可预测，LLM 不需要介入执行过程
- **决策（Decision）**: 需要 LLM 分析推理，生成编排代码

LLM 始终是决策者。每次用户输入至少触发一次 LLM 调用。

## 4. Lua 作为动作编排语言

LLM 一次生成多步 Lua 代码，系统执行：

```lua
-- LLM 生成的编排代码示例
local vim = pkg.search("vim")
local nano = pkg.search("nano")
if vim.found then pkg.remove("vim") end
if nano.found then pkg.remove("nano") end
return {vim_removed = vim.found, nano_removed = nano.found}
```

对比 tool calling 需要 5 次 LLM 调用，Lua 编排只需 1-2 次。

## 5. 沙箱环境

系统暴露受限的 Lua 模块，无 io/os/require/loadfile：

```lua
sandbox_env = {
    pkg = { search, install, remove, list, info },
    sys = { status, run_command },
    ver = { use_version },
    -- 无危险函数
}
```

Lua API 文档注入 system prompt，LLM 据此生成代码。

## 6. 执行引擎抽象

```
ExecutionEngine (抽象层)
├── ToolCallingEngine (现有方式，保留兼容)
└── LuaEngine (Lua 沙箱执行)
```

同一个 CapabilitySpec 在两种引擎下有不同表现形式。能力定义共享，LLM 不感知底层区别。

## 7. 执行-观察-决策循环

```
while not done:
  LLM 看到: 上一轮执行日志 + 用户需求
  LLM 生成: Lua 代码
  系统执行: 沙箱内运行，hook 每个函数调用
  系统返回: 结构化执行日志
  LLM 分析: 满意→完成 / 需要继续→生成新代码
```

执行日志格式：
```json
{
  "status": "completed",
  "steps": [
    {"fn": "pkg.search", "args": {"query": "vim"}, "result": {...}, "ms": 120},
    {"fn": "pkg.remove", "args": {"name": "vim"}, "result": {...}, "ms": 3400}
  ],
  "return_value": {"success": true},
  "duration_ms": 3520
}
```

LLM 三种控制力：
- **事前控制** — Lua 代码中的条件逻辑
- **事后控制** — 分析执行日志决定下一步
- **实时中断** — 超时/步数限制/用户 ESC

## 8. 中断机制（三层防护）

1. **`debug.sethook`** — 指令计数器，防 CPU 死循环
2. **Worker 线程 + 超时** — Lua 在 worker 线程执行，agent 线程带超时等待（同现有 `llm_call_worker` 模式）
3. **函数级 cancel 检查** — 每个 hooked 函数执行前后检查 CancellationToken

## 9. Routine 学习机制

- LLM 多次执行相似操作序列 → 系统检测模式 → 提取为 Routine
- Routine = 保存的 Lua 函数文件，注册为新接口暴露给 LLM
- LLM 不区分 Primitive 和 Routine，统一看作可用接口

## 10. Action 数据模型

```cpp
struct Action {
    int layer;       // 0=Primitive, 1=Routine, 2=Decision, 3=Plan
    int state;       // Pending, Running, Done, Failed, Cancelled
    std::string name;
    std::string detail;
    struct Result { bool success; std::string data; };
    std::optional<Result> result;
    std::vector<Action> children;
    std::string summary;
    bool collapsed {false};
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};
};
```

Action 树由系统自动构建（hook Lua 函数调用），不需要 manage_tree 虚拟工具。

## 11. 现有基础设施

- `mcpplibs.capi.lua` — Lua 5.4 C API 的 C++23 模块绑定，已在项目中使用
- `CapabilityRegistry` + `ToolBridge` — 现有能力注册和工具桥接
- `CancellationToken` — 现有中断机制
- `llm_call_worker` — 现有 worker 线程模式
