# Agent 执行流程 (代码级)

## 完整调用链

```
用户输入 "卸载 d2x 和 mdbook 然后安装第二新版本"
  │
  ▼
┌─────────────────────────────────────────────────────────────────┐
│ cli.cppm — Agent Worker Thread                                  │
│                                                                 │
│  1. behavior_tree.reset()                                       │
│  2. TUI: 创建 TurnNode, 设置 active_turn                        │
│  3. id_alloc.reset(), cancel_token.reset()                      │
│  4. 构建 TreeConfig {                                           │
│       user_input, base_system_prompt, tools,                    │
│       bridge, stream, cfg,                                      │
│       conversation,     ← 共享对话历史指针                       │
│       policy, confirm_cb, cancel,                               │
│       tree (ABehaviorTree), id_alloc,                           │
│       tracker, ctx_mgr,  ← ⚠ ctx_mgr 传入但未使用              │
│       lua_sandbox,                                              │
│       on_stream_chunk,   ← 流式输出到 TUI ◆ 区域                │
│       on_tool_call,      ← TUI 状态 "executing xxx..."          │
│       on_tool_result,    ← TUI 状态 "thinking..."               │
│       on_token_update    ← TUI token 计数更新                    │
│     }                                                           │
│  5. turn_result = run_task_tree(tc)                             │
│  6. behavior_tree.finalize()                                    │
│  7. TUI: 设置 reply, 更新 tracker, 清理状态                     │
│  8. session_mgr.save_conversation()                             │
└─────────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────────┐
│ loop.cppm — run_task_tree(tc)                                   │
│                                                                 │
│  root = BehaviorNode{TypePlan, name=user_input}                 │
│  tree->set_root(root.id, root.name)                             │
│                                                                 │
│  process_node(root, tc, depth=0, accum, root_ctx)  ──────────┐  │
│                                                               │  │
│  ┌──── 最终 summary LLM 调用 ─────────────────────────────┐  │  │
│  │ prompt = base_prompt + 所有子节点结果                    │  │  │
│  │ do_llm_call(on_stream_chunk) → 流式输出到 TUI ◆          │  │  │
│  │ result.reply = response.text()                           │  │  │
│  └──────────────────────────────────────────────────────────┘  │  │
│                                                               │  │
│  conversation->push(user, reply)  ← 追加到共享对话历史        │  │
│  return TreeResult{reply, tokens}                             │  │
└───────────────────────────────────────────────────────────────┘  │
                                                                   │
  ┌────────────────────────────────────────────────────────────────┘
  ▼
┌─────────────────────────────────────────────────────────────────┐
│ loop.cppm — process_node(node, tc, depth, accum, ctx)           │
│                                                                 │
│  cancel 检查 → PausedException / CancelledException             │
│  tree->set_state(Running), tree->set_active(node.id)            │
│                                                                 │
│  ┌─ node.is_atom()? ────────────────────────────────────────┐   │
│  │                                                          │   │
│  │  YES → ATOM 执行路径                                     │   │
│  │  │                                                       │   │
│  │  │  on_tool_call(id, tool, args_preview)                 │   │
│  │  │       → TUI: "executing remove_package..."            │   │
│  │  │                                                       │   │
│  │  │  ┌─ approval 检查 ──────────────────────────────┐     │   │
│  │  │  │ policy->check(spec, args)                    │     │   │
│  │  │  │ Denied → Failed, return                      │     │   │
│  │  │  │ NeedConfirm → confirm_cb() → 用户 Y/N       │     │   │
│  │  │  └──────────────────────────────────────────────┘     │   │
│  │  │                                                       │   │
│  │  │  ┌─ tool == "execute_lua"? ─────────────────────┐     │   │
│  │  │  │ YES: parse code from tool_args               │     │   │
│  │  │  │      lua_sandbox->execute(code, action)      │     │   │
│  │  │  │      status=="completed" → Done, else Failed │     │   │
│  │  │  │                                              │     │   │
│  │  │  │ NO:  bridge.execute(tool, tool_args,         │     │   │
│  │  │  │                     stream, cancel)          │     │   │
│  │  │  │      isError → Failed, else Done             │     │   │
│  │  │  └──────────────────────────────────────────────┘     │   │
│  │  │                                                       │   │
│  │  │  on_tool_result(id, tool, is_error)                   │   │
│  │  │       → TUI: "thinking..."                            │   │
│  │  │  tree->set_state(Done/Failed)                         │   │
│  │  │  tree->set_result(result_summary)                     │   │
│  │  │  return                                               │   │
│  │  │                                                       │   │
│  └──┴───────────────────────────────────────────────────────┘   │
│                                                                 │
│  NO → PLAN 决策路径                                              │
│  │                                                              │
│  │  on_tool_call(id, "decide", node.name)                       │
│  │       → TUI: "executing decide..."                           │
│  │                                                              │
│  ▼                                                              │
│  ┌─ ask_decision(node, tc, depth, ctx, accum) ──────────────┐   │
│  │                                                          │   │
│  │  prompt = build_scoped_prompt(                           │   │
│  │    node, ctx, base_prompt, tools, is_replan)             │   │
│  │                                                          │   │
│  │  prompt 内容:                                            │   │
│  │  ├─ base_system_prompt (身份+规则+Lua说明+记忆)           │   │
│  │  ├─ ## Task Context                                      │   │
│  │  │   User request: {root_name}                           │   │
│  │  │   Task path: A > B > C                                │   │
│  │  ├─ ## Completed Sibling Tasks  ← 含继承的父层结果        │   │
│  │  │   - search_packages: {结果}                           │   │
│  │  │   - [parent] root: {父节点摘要}                       │   │
│  │  ├─ ## Completed Subtasks (re-plan 时)                   │   │
│  │  │   - [OK] remove_package: exitCode=0                   │   │
│  │  │   - [FAILED] install: exitCode=1                      │   │
│  │  ├─ ## Current Task                                      │   │
│  │  ├─ ## Available Tools (with schemas)                    │   │
│  │  │   - remove_package {"target": string}                 │   │
│  │  │   - install_packages {"targets": array}               │   │
│  │  └─ 决策指令 (decompose/done)                            │   │
│  │                                                          │   │
│  │  conv = [system(prompt)]                                 │   │
│  │  if depth==0: inject conversation->messages  ← 跨turn    │   │
│  │  conv.push(user("Task: ..."))                            │   │
│  │                                                          │   │
│  │  tools = [decide_tool_def]  ← 只有 decide 工具           │   │
│  │                                                          │   │
│  │  response = do_llm_call(                                 │   │
│  │    msgs, params, cfg,                                    │   │
│  │    on_stream_chunk,  ← 流式到 TUI ◆ 区域                 │   │
│  │    cancel)                                               │   │
│  │                                                          │   │
│  │  accum += response.usage.tokens                          │   │
│  │                                                          │   │
│  │  解析 decide({thinking, action, summary, subtasks})      │   │
│  │  └─ thinking → decision.reasoning                        │   │
│  │  └─ subtask.tool 验证: bridge.tool_info != "unknown"     │   │
│  │  └─ depth>=MAX_DEPTH → 丢弃无 tool+args 的 subtask       │   │
│  │                                                          │   │
│  │  return Decision{action, summary, reasoning, subtasks}   │   │
│  └──────────────────────────────────────────────────────────┘   │
│  │                                                              │
│  │  tree->clear_streaming()  ← 清理 LLM 流式缓存               │
│  │  node.detail = decision.reasoning                            │
│  │                                                              │
│  │  on_tool_result(id, "decide", false)                         │
│  │       → TUI: "thinking..."                                  │
│  │                                                              │
│  ├─ action == "done"                                            │
│  │  node.state = Done                                           │
│  │  node.result_summary = summary                               │
│  │  return                                                      │
│  │                                                              │
│  └─ action == "decompose"                                       │
│     │                                                           │
│     │  ┌─ 创建 thinking 子节点 ◇ ───────────────────────────┐   │
│     │  │ BehaviorNode{TypePlan, name=reasoning,              │   │
│     │  │              detail="__thinking__", state=Done}      │   │
│     │  │ tree->add_child(node.id, thinking)                  │   │
│     │  └─────────────────────────────────────────────────────┘   │
│     │                                                           │
│     │  create_children(node, subtasks, tc)                       │
│     │  ├─ subtask 有 tool+args → TypeAtom 子节点                │
│     │  └─ subtask 无 tool+args → TypePlan 子节点                │
│     │  tree->add_child × N                                      │
│     │                                                           │
│     ▼                                                           │
│  ┌─ RE-PLAN 循环 (max MAX_REPLAN=3) ────────────────────────┐   │
│  │                                                          │   │
│  │  for replan = 0..3:                                      │   │
│  │                                                          │   │
│  │  ┌─ execute_pending_children ─────────────────────────┐  │   │
│  │  │                                                    │  │   │
│  │  │  for each Pending child:                           │  │   │
│  │  │    child_ctx = {                                   │  │   │
│  │  │      root_name,                                    │  │   │
│  │  │      ancestor_path + [node.name],                  │  │   │
│  │  │      ctx.sibling_results   ← 继承父层!             │  │   │
│  │  │        + [parent] node summary                     │  │   │
│  │  │        + 已完成兄弟的 {name, result_summary}       │  │   │
│  │  │        (过滤 __thinking__ 节点)                    │  │   │
│  │  │    }                                               │  │   │
│  │  │    process_node(child, depth+1, child_ctx)         │  │   │
│  │  │         ↑ 递归! Atom 直接执行, Plan 再次决策       │  │   │
│  │  └────────────────────────────────────────────────────┘  │   │
│  │                                                          │   │
│  │  全部 Done? → YES → node.state = Done                    │   │
│  │                      node.result_summary = synthesize()  │   │
│  │                      return                              │   │
│  │                                                          │   │
│  │  有 Failed? → ask_decision(is_replan=true)               │   │
│  │              ↑ 同上流程, prompt 含已完成子任务结果        │   │
│  │                                                          │   │
│  │  ├─ "done" → 创建 re-plan marker 节点                    │   │
│  │  │           node.state = Done                           │   │
│  │  │           node.result_summary = summary               │   │
│  │  │           return                                      │   │
│  │  │                                                       │   │
│  │  └─ "decompose" → 创建 "re-plan #N" Plan 子节点          │   │
│  │                    子任务挂在 re-plan 子节点下             │   │
│  │                    execute_pending_children(re-plan子节点) │   │
│  │                    → 回到循环顶部                         │   │
│  │                                                          │   │
│  │  MAX_REPLAN 耗尽 → node.state = Failed                    │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Context 数据流

```
┌─────────────────────────────────────────────────────────────┐
│                     Context 来源与流向                        │
│                                                             │
│  ┌─ 持久层 ──────────────────────────────────────────────┐  │
│  │                                                       │  │
│  │  conversation.json ──load──► Conversation 对象          │  │
│  │  memory_store     ──load──► MemorySummary[]            │  │
│  │  context_cache/   ──load──► ContextManager L2/L3       │  │
│  │                              ⚠ 断路: 未被 process_node │  │
│  │                                       调用             │  │
│  └───────────────────────────────────────────────────────┘  │
│                    │                                        │
│                    ▼                                        │
│  ┌─ 构建阶段 (cli.cppm) ────────────────────────────────┐  │
│  │                                                       │  │
│  │  base_system_prompt = build_system_prompt(             │  │
│  │    bridge,        ← 工具列表                           │  │
│  │    mem_summaries  ← [category] content...              │  │
│  │  )                                                    │  │
│  │  输出: 身份 + 规则 + Lua说明 + 记忆摘要                │  │
│  │                                                       │  │
│  │  tools = to_llmapi_tools(bridge)                      │  │
│  │  输出: [{name, description, inputSchema}]  ← 纯真实工具│  │
│  └───────────────────────────────────────────────────────┘  │
│                    │                                        │
│                    ▼                                        │
│  ┌─ 每次 ask_decision 构建的 scoped prompt ──────────────┐  │
│  │                                                       │  │
│  │  build_scoped_prompt(node, ctx, base, tools, replan)  │  │
│  │                                                       │  │
│  │  组装顺序:                                            │  │
│  │  ┌───────────────────────────────────────────┐        │  │
│  │  │ 1. base_system_prompt                     │ 固定    │  │
│  │  │    (身份+规则+Lua+记忆)                    │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 2. ## Task Context                        │ 动态    │  │
│  │  │    User request: {root_name}              │        │  │
│  │  │    Task path: A > B > C                   │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 3. ## Completed Sibling Tasks             │ 动态    │  │
│  │  │    - [parent] root: ...                   │ 继承    │  │
│  │  │    - search_packages: found...            │ 本层    │  │
│  │  │    - package_info: versions...            │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 4. ## Completed Subtasks (re-plan only)   │ 条件    │  │
│  │  │    - [OK] remove: exitCode=0              │        │  │
│  │  │    - [FAILED] install: exitCode=1         │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 5. ## Current Task                        │ 动态    │  │
│  │  │    {node.name} + {node.detail}            │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 6. ## Available Tools (with schemas)      │ 固定    │  │
│  │  │    - remove_package {schema}              │        │  │
│  │  │    - install_packages {schema}            │        │  │
│  │  ├───────────────────────────────────────────┤        │  │
│  │  │ 7. 决策指令                               │ 固定    │  │
│  │  │    decompose / done 说明                  │        │  │
│  │  └───────────────────────────────────────────┘        │  │
│  │                                                       │  │
│  │  然后:                                                │  │
│  │  conv = [system(prompt)]                              │  │
│  │  if depth==0:                                         │  │
│  │    for msg in conversation->messages:                  │  │
│  │      conv.push(msg)  ← 跨turn历史全量注入              │  │
│  │  conv.push(user("Task: ..."))                         │  │
│  │                                                       │  │
│  │  LLM 看到的完整消息序列:                               │  │
│  │  [system] scoped prompt                               │  │
│  │  [user]   turn 1 question        ← depth==0 时才有     │  │
│  │  [asst]   turn 1 reply           ← depth==0 时才有     │  │
│  │  [user]   turn 2 question        ← depth==0 时才有     │  │
│  │  [asst]   turn 2 reply           ← depth==0 时才有     │  │
│  │  [user]   "Task: 当前任务描述"                         │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ 结果流向 ────────────────────────────────────────────┐  │
│  │                                                       │  │
│  │  Atom 完成:                                           │  │
│  │    result_summary = bridge.execute 返回的 content      │  │
│  │    (可能是 raw JSON: {"exitCode":0,"events":[...]})    │  │
│  │    → 进入父节点的 synthesize_children                  │  │
│  │    → 进入兄弟节点的 sibling_results                    │  │
│  │    → 进入子节点继承的 parent context                   │  │
│  │                                                       │  │
│  │  Plan 完成:                                           │  │
│  │    result_summary = decision.summary (done)           │  │
│  │                   | synthesize_children (decompose)    │  │
│  │    → 同上流向                                         │  │
│  │                                                       │  │
│  │  Root 完成:                                           │  │
│  │    → final summary LLM call → result.reply            │  │
│  │    → conversation.push(user, reply)                   │  │
│  │    → session_mgr.save_conversation()                  │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## TUI 更新时序

```
Worker Thread                           Main Thread (TUI)
    │                                        │
    │  tree->set_root()                      │
    │  ─────────────────────────────────►    │  render: ⏵ user message
    │                                        │
    │  on_tool_call("decide")                │
    │  ─────────────────────────────────►    │  status: "executing decide..."
    │                                        │
    │  on_stream_chunk("需要先...")           │
    │  tree->append_streaming(...)           │
    │  ─────────────────────────────────►    │  ◆ 需要先卸载两个包...  (实时)
    │                                        │
    │  tree->clear_streaming()               │
    │  on_tool_result("decide")              │
    │  ─────────────────────────────────►    │  ◆ (清空)
    │                                        │  status: "thinking..."
    │                                        │
    │  tree->add_child(thinking ◇)           │
    │  tree->add_child(Atom ⚙)              │
    │  ─────────────────────────────────►    │  render:
    │                                        │  ├─ ◇ 先卸载再搜索版本...
    │                                        │  ├─ ○ remove_package (Pending)
    │                                        │
    │  tree->set_state(Atom, Running)        │
    │  on_tool_call("remove_package")        │
    │  ─────────────────────────────────►    │  ├─ ⚙ remove_package (Running)
    │                                        │  status: "executing remove..."
    │                                        │
    │  bridge.execute(...)                   │
    │                                        │
    │  tree->set_state(Atom, Done)           │
    │  on_tool_result("remove_package")      │
    │  ─────────────────────────────────►    │  ├─ ✓ remove_package  4ms
    │                                        │
    │  ... 下一个节点 ...                     │
    │                                        │
    │  (tree 完成)                            │
    │  final summary LLM call                │
    │  on_stream_chunk("已成功...")           │
    │  ─────────────────────────────────►    │  ◆ 已成功卸载并重新安装... (实时)
    │                                        │
    │  tree->finalize()                      │
    │  ─────────────────────────────────►    │  render: 所有节点最终状态
    │                                        │
    │  return TreeResult                     │
    │  ─────────────────────────────────►    │  active_turn->reply = reply
    │                                        │  active_turn->root = snapshot()
    │                                        │  active_turn = nullptr
```

## ⚠ 断路/未使用的组件

| 组件 | 位置 | 状态 | 说明 |
|------|------|------|------|
| `ContextManager.maybe_auto_compact` | context_manager.cppm | **断路** | TreeConfig 有 ctx_mgr 指针但 process_node 从未调用 |
| `ContextManager.build_context_prefix` | context_manager.cppm | **断路** | L2 summary 从未注入到 prompt |
| `ContextManager.retrieve_relevant` | context_manager.cppm | **断路** | L3 关键词检索从未被调用 |
| `ContextManager.compact` | context_manager.cppm | **断路** | 手动 compact 未在新架构使用 |
| `ctx_mgr.record_turn()` | cli.cppm:1544 | **调用但无效** | 在 turn 完成后调用，但 ContextManager 内部数据未被消费 |
| `compact_conversation()` | loop.cppm | **未调用** | 导出函数但无调用者 |
| `TokenTracker.context_used()` | token_tracker.cppm | **过期数据** | 返回上一轮 record() 的 input_tokens, 非当前 |
