# Agent 模块简化方案

## 1. 现状分析

19 个 .cppm 文件, 4499 行:

```
核心路径 (用户输入 → LLM → 工具 → 结果):
  cli.cppm → loop.cppm → behavior_tree.cppm
                       → tool_bridge.cppm
                       → llm_config.cppm

TUI 渲染:
  ftxui_tui.cppm → tui.cppm → behavior_tree.cppm

断路/冗余:
  context_manager.cppm  (606行, 3级缓存, 完全断路)
  prompt_builder.cppm   (221行, build_scoped 在 loop.cppm 中重写了)
  resource_cache.cppm   (143行, 未被核心路径使用)
  resource_tools.cppm   (77行, 未被核心路径使用)
  package_tools.cppm    (91行, 未被核心路径使用)
  mcp_client.cppm       (92行, 未被核心路径使用)
  output_buffer.cppm    (65行, 未被核心路径使用)
```

## 2. 保留/删除/合并

### 保留 (核心)

| 模块 | 行数 | 理由 |
|------|------|------|
| **loop.cppm** | 1034 | 递归引擎, 不可替代 |
| **behavior_tree.cppm** | 218 | 节点定义 + ABehaviorTree, 不可替代 |
| **tool_bridge.cppm** | 132 | 工具执行桥梁, 不可替代 |
| **llm_config.cppm** | 219 | LLM 配置, 不可替代 |
| **ftxui_tui.cppm** | 468 | TUI 渲染, 不可替代 |
| **tui.cppm** | 211 | TUI 状态, 不可替代 |
| **session.cppm** | 158 | Session 持久化, 不可替代 |
| **approval.cppm** | 57 | 安全审批, 简单但必要 |
| **lua_engine.cppm** | 551 | Lua 沙箱, 功能性模块 |
| **commands.cppm** | 61 | 斜杠命令, 功能性模块 |

### 删除

| 模块 | 行数 | 理由 |
|------|------|------|
| **context_manager.cppm** | 606 | 完全断路, L2/L3 缓存从未生效 |
| **prompt_builder.cppm** | 221 | build_scoped 已在 loop.cppm 中, build() 未使用 |
| **resource_cache.cppm** | 143 | 非核心路径, 可以后续按需加回 |
| **resource_tools.cppm** | 77 | 同上 |
| **package_tools.cppm** | 91 | 同上 |
| **mcp_client.cppm** | 92 | 同上 |
| **output_buffer.cppm** | 65 | 同上 |
| **token_tracker.cppm** | 75 | 合并到 loop.cppm |

**删除合计: 1370 行, 8 个文件**

### 结果: 11 个文件, ~3100 行

```
src/agent/
├── agent.cppm           (20)   聚合 re-export
├── behavior_tree.cppm   (218)  节点 + 树
├── loop.cppm            (~1100) 递归引擎 + token tracking + prompt 构建
├── tool_bridge.cppm     (132)  工具执行
├── llm_config.cppm      (219)  LLM 配置
├── approval.cppm        (57)   审批策略
├── lua_engine.cppm      (551)  Lua 沙箱
├── session.cppm         (158)  Session 持久化
├── commands.cppm        (61)   斜杠命令
├── tui.cppm             (211)  TUI 状态
└── ftxui_tui.cppm       (468)  TUI 渲染
```

## 3. Context 管理重新设计

删除 ContextManager 3 级缓存后, 用更简单的方案:

### 3.1 Conversation 自动压缩

```
conversation (跨 turn):
  ├─ 最近 N 轮: 完整 user + assistant 消息
  ├─ 更早的轮: 压缩为 1 条 system 消息 "Turn 3: 用户问X, 回答Y"
  └─ 触发条件: conversation.messages.size() > MAX_HISTORY_MESSAGES
```

直接在 `run_task_tree` 开头做, 不需要单独模块:

```cpp
// 在 run_task_tree 开头
if (tc.conversation && tc.conversation->messages.size() > 20) {
    compact_conversation(*tc.conversation, /*keep_recent=*/6);
}
```

### 3.2 Sibling Context 压缩

Atom 的 result_summary 是 raw JSON, 对 LLM 不友好. 压缩策略:

```cpp
// 在 Atom 完成时, 只保留关键信息
if (result.content.size() > 300) {
    // 提取 exitCode + 前 200 字符
    node.result_summary = "exitCode=" + ... + ", " + content.substr(0, 200);
}
```

### 3.3 Token 追踪简化

合并 TokenTracker 到 loop.cppm:

```cpp
struct TreeResult {
    std::string reply;
    int input_tokens {0};
    int output_tokens {0};
    int cache_read_tokens {0};
    int cache_write_tokens {0};
};
// TreeResult.input_tokens 就是 context_used 的最佳近似
```

## 4. loop.cppm 重构

当前 loop.cppm 1034 行, 承担太多职责. 合并 token_tracker 和 prompt 构建后会更大. 建议拆分:

```
loop.cppm (重构后)
├── 导出: run_task_tree, TreeConfig, TreeResult, TurnConfig (compat)
├── 内部: process_node, ask_decision, create_children,
│         execute_pending_children, re-plan loop
├── 内部: build_scoped_prompt, synthesize_children
├── 内部: do_llm_call, llm_call_worker
└── 内部: compact_conversation (从独立函数移入)
```

不拆文件, 但按职责分区:

```cpp
// ─── Section 1: Types & Config ───
// TreeConfig, TreeResult, Decision, NodeContext, SubtaskDef

// ─── Section 2: LLM Infrastructure ───
// do_llm_call, llm_call_worker, decide_tool_def

// ─── Section 3: Prompt Construction ───
// build_scoped_prompt, build_system_prompt

// ─── Section 4: Tree Operations ───
// create_children, execute_pending_children,
// has_failed_children, build_completed_results, synthesize_children

// ─── Section 5: Core Engine ───
// process_node, ask_decision, run_task_tree

// ─── Section 6: Compat ───
// run_one_turn, compact_conversation
```

## 5. 简化后的架构图

```
┌──────────────────────────────────────────────────────┐
│                    cli.cppm                           │
│  用户输入 → TreeConfig 构建 → run_task_tree()         │
│  Session 管理, TUI 回调, EventStream 消费             │
└───────────────────┬──────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
┌──────────┐ ┌──────────┐ ┌──────────┐
│ loop     │ │ tui      │ │ session  │
│          │ │          │ │          │
│ 递归引擎  │ │ TUI 状态  │ │ 持久化   │
│ LLM 调用  │ │ TurnNode │ │ conv.json│
│ prompt   │ │ AgentTui │ │ meta.json│
│ token    │ │ State    │ │          │
└────┬─────┘ └────┬─────┘ └──────────┘
     │            │
     │            ▼
     │     ┌──────────┐
     │     │ ftxui_tui│
     │     │          │
     │     │ 渲染引擎  │
     │     │ 图标/颜色 │
     │     └──────────┘
     │
     ├──────────┬──────────┬──────────┐
     ▼          ▼          ▼          ▼
┌─────────┐┌─────────┐┌─────────┐┌─────────┐
│behavior ││tool     ││llm      ││approval │
│_tree    ││_bridge  ││_config  ││         │
│         ││         ││         ││         │
│BehavNode││execute()││provider ││policy   │
│ABeTree  ││tool_info││model    ││check()  │
│IdAlloc  ││events   ││api_key  ││         │
└─────────┘└─────────┘└─────────┘└─────────┘

可选扩展 (按需加回):
  lua_engine  — Lua 沙箱
  commands    — 斜杠命令
```

## 6. 实施步骤

### Phase 1: 删除断路模块
- [ ] 删除 context_manager.cppm
- [ ] 删除 prompt_builder.cppm
- [ ] 删除 resource_cache.cppm, resource_tools.cppm, package_tools.cppm
- [ ] 删除 mcp_client.cppm, output_buffer.cppm
- [ ] 合并 token_tracker 到 loop.cppm (TurnResult 已有 token 字段)
- [ ] 更新 agent.cppm re-export 列表
- [ ] 更新 cli.cppm 移除 ctx_mgr / resource 相关引用
- [ ] 编译通过 + 209 tests

### Phase 2: Context 简化
- [ ] run_task_tree 开头加 conversation auto-compact
- [ ] Atom result_summary 压缩 (raw JSON → 关键信息)
- [ ] cli.cppm 移除 ctx_mgr.record_turn() 等断路调用

### Phase 3: 清理
- [ ] loop.cppm 按职责分区 (Section 1-6)
- [ ] 删除未使用的回调类型 (AutoCompactCallback)
- [ ] 更新文档
