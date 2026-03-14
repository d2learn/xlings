# Two-Type Node + Re-plan Design

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan.

**Goal:** Simplify agent task tree to two node types (Atom + Plan) with dynamic re-plan capability, replacing the current 6-type system.

**Architecture:** Every node is either an Atom (system direct tool call, zero LLM) or a Plan (LLM decides via ask_decision, decomposes into children). Plan nodes support re-plan after failures — LLM can accept results, add new subtasks, or mark as failed. No tool-use loop (run_tool_loop/run_execute removed).

**Tech Stack:** C++23 modules, nlohmann::json, mcpplibs::llmapi

---

## 1. Current vs Proposed Architecture

### Current: 6 node types

```
TypeRoot(0)       — synthetic root
TypeDecompose(1)  — split into subtasks
TypeExecute(2)    — LLM tool-use loop (run_tool_loop, N LLM calls)
TypeDirectExec(3) — system direct tool call
TypeToolCall(4)   — tool call record (child of Execute)
TypeResponse(5)   — LLM text record (child of Execute)
```

### Proposed: 2 node types

```
TypeAtom(0)  — has tool+args, system direct execute, zero LLM
TypePlan(1)  — needs LLM, ask_decision → decompose into Atom/Plan children
```

### What gets deleted

| Deleted | Replacement |
|---------|------------|
| TypeRoot | TypePlan (root is just a Plan node) |
| TypeDecompose | TypePlan (same thing) |
| TypeExecute | TypePlan decompose into Atom children |
| TypeDirectExec | TypeAtom (renamed) |
| TypeToolCall | Atom child (tool call IS a node, not a record) |
| TypeResponse | Gone (no tool-use loop = no streaming records) |
| `run_execute()` | Deleted |
| `run_tool_loop()` | Deleted |
| `ToolLoopConfig` | Deleted |
| `NodeResult` | Deleted |
| verify node | Replaced by re-plan in process_node |

## 2. Core Model

### 2.1 BehaviorNode

```cpp
export struct BehaviorNode {
    int id {0};

    // Only 2 types
    inline static constexpr int TypeAtom = 0;  // direct tool call
    inline static constexpr int TypePlan = 1;  // LLM decision node
    int type {TypePlan};

    // State (unchanged)
    inline static constexpr int Pending  = 0;
    inline static constexpr int Running  = 1;
    inline static constexpr int Done     = 2;
    inline static constexpr int Failed   = 3;
    inline static constexpr int Skipped  = 4;
    int state {Pending};

    // Content
    std::string name;            // task title / tool name
    std::string detail;          // description / args summary
    std::string result_summary;  // result after completion

    // Atom: tool + args (non-empty = Atom)
    std::string tool;
    std::string tool_args;

    // Time
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};

    // Tree
    std::vector<BehaviorNode> children;

    auto is_terminal() const -> bool {
        return state == Done || state == Failed || state == Skipped;
    }
    auto is_atom() const -> bool { return !tool.empty(); }
};
```

### 2.2 decide Protocol

ask_decision returns one of:

```json
{
  "action": "decompose",
  "subtasks": [
    { "title": "搜索mdbook", "tool": "search_packages", "args": {"keyword":"mdbook"} },
    { "title": "确定版本并安装" }
  ]
}
```

```json
{
  "action": "done",
  "summary": "d2x未安装，无需卸载"
}
```

| action | meaning | when |
|--------|---------|------|
| decompose | create Atom/Plan children | initial decision or re-plan retry |
| done | accept current state, mark Done | re-plan: failures are acceptable |

## 3. process_node Flow

```
process_node(node, depth)
│
├─ node.is_atom()?
│  ├─ YES → bridge.execute(tool, args) → Done/Failed → return
│  └─ NO  ↓
│
├─ ask_decision(node, depth) ← 1 LLM call
│  │
│  ├─ action: done
│  │  → node.state = Done
│  │  → node.result_summary = summary
│  │  → return
│  │
│  └─ action: decompose
│     → create children (Atom + Plan mix)
│     → DFS execute all children
│     ↓
│
├─ All children Done?
│  ├─ YES → node.state = Done → return
│  └─ NO  → has failures ↓
│
├─ re-plan: ask_decision again ← 1 LLM call (with results context)
│  │  (max MAX_REPLAN rounds)
│  │
│  ├─ action: done
│  │  → accept results, node Done → return
│  │
│  └─ action: decompose
│     → append new children
│     → DFS execute new children only
│     → loop back to "All children Done?" ↑
│
└─ MAX_REPLAN exceeded → node.state = Failed → return
```

### Pseudocode

```cpp
inline constexpr int MAX_DEPTH = 5;
inline constexpr int MAX_REPLAN = 3;

void process_node(BehaviorNode& node, TreeConfig& tc, int depth,
                  TreeResult& accum, const NodeContext& ctx) {
    // ── Atom: direct execute ──
    if (node.is_atom()) {
        node.type = BehaviorNode::TypeAtom;
        auto result = tc.bridge.execute(node.tool, node.tool_args, ...);
        node.state = result.isError ? Failed : Done;
        node.result_summary = truncate(result.content, 200);
        return;
    }

    // ── Plan: LLM decision ──
    node.type = BehaviorNode::TypePlan;
    auto decision = ask_decision(node, tc, depth, ctx, accum);

    if (decision.action == "done") {
        node.state = Done;
        node.result_summary = decision.summary;
        return;
    }

    // decompose → create children
    create_children(node, decision.subtasks, tc);

    // Execute + re-plan loop
    for (int replan = 0; replan <= MAX_REPLAN; ++replan) {
        // DFS execute pending children
        execute_pending_children(node, tc, depth, accum, ctx);

        // Check results
        if (!has_failed_children(node)) {
            node.state = Done;
            node.result_summary = synthesize(node);
            return;
        }

        if (replan == MAX_REPLAN) break;  // no more retries

        // Re-plan: ask_decision with results context
        auto replan_decision = ask_decision_replan(node, tc, depth, ctx, accum);

        if (replan_decision.action == "done") {
            node.state = Done;
            node.result_summary = replan_decision.summary;
            return;
        }

        // Append new children from re-plan
        create_children(node, replan_decision.subtasks, tc);
        // Loop continues → execute new children
    }

    node.state = Failed;
    node.result_summary = "max re-plan attempts exceeded";
}
```

## 4. ask_decision Details

### 4.1 Initial Decision (first call)

Prompt includes:
- Base system prompt (L1 + L4)
- Conversation history (depth=0 only)
- Task context (ancestor_path, sibling_results)
- Available tool names
- Current task name + detail

```
## Available Tools
- search_packages
- install_packages
- remove_package
- package_info
- ...

## Current Task
安装 mdbook 第二新版本

Decide: decompose this task into subtasks.
- For subtasks with known tool+args, specify them (system executes directly, zero cost)
- For subtasks needing reasoning or depending on previous results, just give title
- You CAN mix both types freely

Call decide with your decision.
```

### 4.2 Re-plan Decision (after failures)

Same prompt structure but adds results:

```
## Completed Subtasks
- [OK] search_packages("mdbook"): found 0.4.40, 0.4.43, 0.4.44
- [FAILED] install_packages("mdbook@0.4.43"): network timeout

Some subtasks failed. You can:
- "done": accept the current results (if failures are expected/harmless)
- "decompose": add new subtasks to retry or take alternative approach

Call decide with your decision.
```

### 4.3 Depth Limit

At `depth >= MAX_DEPTH`:
- All subtasks MUST have tool+args (forced Atom)
- Plan subtasks (no tool+args) are dropped with warning
- If LLM returns no valid subtasks → node Failed

## 5. Example Traces

### 5.1 Simple: "search vim"

```
Plan: "search vim"
│ ask_decision → decompose
└─ Atom: ⚙ search_packages("vim") → Done

LLM calls: 1 (decide)
```

### 5.2 Medium: "卸载 d2x 和 mdbook"

```
Plan: "卸载 d2x 和 mdbook"
│ ask_decision → decompose
├─ Atom: ⚙ remove_package("d2x")    → Failed (not installed)
├─ Atom: ⚙ remove_package("mdbook") → Done
│
│ has failures → re-plan
│ ask_decision → done "d2x was not installed, mdbook removed successfully"
└─ Done

LLM calls: 2 (decide + re-plan)
```

### 5.3 Complex: "安装 mdbook 第二新版本"

```
Plan: "安装 mdbook 第二新版本"
│ ask_decision → decompose
├─ Atom: ⚙ search_packages("mdbook")  → Done "0.4.40, 0.4.43, 0.4.44"
├─ Atom: ⚙ package_info("mdbook")     → Done "versions: [0.4.40, 0.4.43, 0.4.44]"
└─ Plan: "根据版本信息安装第二新版本"
   │ ask_decision → decompose (sees siblings: version list)
   │ LLM reasons: 第二新 = 0.4.43
   ├─ Atom: ⚙ install_packages("xim:mdbook@0.4.43") → Done
   └─ Atom: ⚙ list_packages("mdbook")               → Done "0.4.43 installed"

LLM calls: 2 (root decide + child decide)
```

### 5.4 Failure + Re-plan: install fails, retry

```
Plan: "安装 mdbook 第二新版本"
│ ask_decision → decompose
├─ Atom: ⚙ search_packages("mdbook")  → Done
├─ Plan: "安装第二新版本"
│  │ ask_decision → decompose
│  ├─ Atom: ⚙ install_packages("mdbook@0.4.43") → Failed (network)
│  │
│  │ re-plan: ask_decision → decompose (retry)
│  ├─ Atom: ⚙ install_packages("mdbook@0.4.43") → Done
│  └─ Done

LLM calls: 3 (root decide + child decide + child re-plan)
```

### 5.5 Skip: package not installed

```
Plan: "卸载 d2x"
│ ask_decision → decompose
├─ Atom: ⚙ package_info("d2x") → Done "installed: no"
├─ Atom: ⚙ remove_package("d2x") → Failed (exitCode=1)
│
│ re-plan: sees "not installed" + remove failed
│ ask_decision → done "d2x is not installed, nothing to remove"
└─ Done

LLM calls: 2
```

## 6. Comparison with Current Architecture

### LLM Call Efficiency

| Scenario | Current (6-type) | Proposed (2-type) |
|----------|------------------|-------------------|
| search vim | 1 decide + 1 tool-loop = 2 | 1 decide = **1** |
| 卸载 d2x 和 mdbook | 1 decide + N tool-loop + 1 verify = ~6 | 1 decide + 1 replan = **2** |
| 安装 mdbook 第二新版本 | 1 decide + N tool-loop = ~5 | 2 decide = **2** |
| 安装失败重试 | 1 decide + N tool-loop(含重试) = ~8 | 2 decide + 1 replan = **3** |

### Pros

| Advantage | Detail |
|-----------|--------|
| **更少 LLM 调用** | 无 tool-use loop，每个 LLM 调用都是 focused decide |
| **树结构即执行记录** | 每个 tool call 是 Atom 子节点，不需要 TypeToolCall 记录节点 |
| **Context 天然受控** | 每个 decide 只看 scoped prompt + sibling_results，无累积对话 |
| **动态恢复** | re-plan 可以追加重试节点、接受失败、改变策略 |
| **代码更简单** | 删除 run_tool_loop/run_execute/ToolLoopConfig/NodeResult/verify |
| **TUI 更清晰** | 只有 ⚙ Atom 和 ○ Plan 两种图标，所有操作可见 |

### Cons / Risks

| Risk | Mitigation |
|------|------------|
| **LLM 必须精确给出 tool+args** | Available Tools 列表 + 工具名验证 + re-plan 重试 |
| **深度嵌套（>5层）** | MAX_DEPTH 限制，深层强制全 Atom |
| **简单任务多一次 decide 开销** | "search vim" 现在是 1 decide vs 之前 1 decide + 1 loop，但 loop 内也要 1 LLM call，所以实际一样 |
| **推理形式变化** | 从扁平对话变为树形 decide→Atom 交替，本质等价（动态增加节点 = 流式推理），但 LLM 需适应 scoped context 模式 |
| **re-plan 死循环** | MAX_REPLAN=3 硬限制 |

### Key Insight: 树形流式推理

Plan + re-plan 本质上就是流式推理的树形表达：
- tool-use loop: 扁平对话，LLM 看累积 context 决定下一步
- Plan + re-plan: 树状结构，LLM 看 scoped sibling_results 决定下一步

两者能力等价，但树形结构额外获得：
- 每一步都在树中可见（不是隐藏在对话里的记录节点）
- 每一步的 context 都是 scoped 的（不累积膨胀）
- 系统可以在任何节点介入（re-plan、skip、重试）
- 动态增加节点 = 流式推理，只是组织形式不同

## 7. File Changes

| File | Change | Lines |
|------|--------|-------|
| `behavior_tree.cppm` | **重写** BehaviorNode (2 types), ABehaviorTree (简化API) | ~150 |
| `loop.cppm` | **大改** 删除 run_tool_loop/run_execute/ToolLoopConfig, 重写 process_node + ask_decision, 新增 re-plan | ~400 |
| `ftxui_tui.cppm` | **小改** 2-type icon/color mapping | ~20 |
| `tui.cppm` | **小改** re-export 更新 | ~5 |
| `cli.cppm` | **小改** 删除 tool-loop 相关回调 | ~30 |
| `prompt_builder.cppm` | **不变** build_scoped 仍适用 | 0 |

## 8. Implementation Steps

### Phase 1: BehaviorNode 简化
- [ ] BehaviorNode 改为 TypeAtom(0) + TypePlan(1)
- [ ] ABehaviorTree 删除 begin_tool/end_tool/flush_as_response（无 tool-use loop）
- [ ] 保留 set_root/add_child/set_state/set_result/set_active
- [ ] 编译通过

### Phase 2: process_node 重写
- [ ] 删除 run_tool_loop, run_execute, ToolLoopConfig, NodeResult
- [ ] process_node: Atom 分支 + Plan 分支 (ask_decision + DFS + re-plan)
- [ ] ask_decision: 扩展 decide 协议支持 done action
- [ ] ask_decision_replan: 带 results context 的 re-plan 版本
- [ ] execute_pending_children: 只执行 Pending 状态的子节点
- [ ] 编译通过

### Phase 3: TUI + CLI 适配
- [ ] ftxui_tui.cppm: TypeAtom ⚙ + TypePlan ○/⟳/✓/✗
- [ ] cli.cppm: 删除 on_tool_call/on_tool_result 中的 tool-loop 逻辑
- [ ] 编译通过 + 测试

### Phase 4: 清理
- [ ] 删除 execute_lua_tool_def（execute_lua 作为 Atom 直接调用）
- [ ] 删除旧 build_system_prompt 中的 Lua/manage_tree 说明
- [ ] 209 tests pass
