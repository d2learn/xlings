# Agent 执行流

## 入口：`cli.cppm` (~line 1024)

```
用户输入 → Screen::loop (tinytui raw mode)
         → LineEditor 捕获输入
         → 构建上下文：registry, bridge, tools, system_prompt, lua_sandbox
         → 进入 run_one_turn()
```

## 核心循环：`loop.cppm` `run_one_turn()` (~line 508)

```
user_input → conversation.push(user msg)
           → for (i = 0; i < 50; ++i):
               │
               ├─ [1] 前置检查
               │   ├─ cancel check (PausedException / CancelledException)
               │   ├─ auto-compact check (ctx_mgr + tracker → 75% 阈值压缩)
               │   └─ context budget check (>92% → 停止)
               │
               ├─ [2] LLM 调用 (worker 线程)
               │   ├─ llm_call_worker<Anthropic|OpenAI>(...)
               │   │   ├─ worker thread: provider.chat_stream(msgs, params, safe_chunk)
               │   │   └─ main thread: cv_done->wait_for(200ms) + cancel check
               │   ├─ on_stream_chunk callback → screen.post → TUI 更新
               │   │   ├─ think_filter 分离 <think> 标签
               │   │   ├─ thinking → Thinking TreeNode (Running)
               │   │   └─ text → Response TreeNode (Running)
               │   └─ 记录 ActionNode(llm_call) + token 统计
               │
               ├─ [3] 检查 stopReason
               │   ├─ != ToolUse → turn_result.reply = response.text() → return
               │   └─ == ToolUse → 进入 tool 执行
               │
               ├─ [4] 逐个执行 tool calls
               │   ├─ cancel check
               │   │
               │   ├─ "manage_tree" (虚拟工具) → handle_manage_tree()
               │   │   ├─ add_task / start_task / complete_task / cancel_task / update_task / batch
               │   │   └─ 操作 TaskTree + TreeNode 树结构
               │   │
               │   ├─ "execute_lua" (虚拟工具) → lua_sandbox->execute()
               │   │   ├─ worker thread: L_loadstring + pcall
               │   │   ├─ Lua 代码调用 pkg.*/sys.*/ver.* → trampoline → dispatch_capability
               │   │   │   └─ registry_.get(cap_name)->execute(args, stream, cancel)
               │   │   ├─ debug.sethook 每 10000 指令检查 cancel/timeout
               │   │   └─ 返回 ExecutionLog JSON 给 LLM
               │   │
               │   └─ 其他工具 → handle_tool_call()
               │       ├─ ApprovalPolicy check (auto/confirm/deny)
               │       ├─ bridge.execute(name, args, stream, cancel)
               │       │   └─ registry_.get(name)->execute(args, stream, cancel)
               │       └─ event_buffer 注入 DataEvent 到 result
               │
               ├─ [5] tool result → conversation.push(Tool msg)
               │
               ├─ [6] runaway detection (>40 consecutive tool-only → stop)
               │
               └─ continue → 回到 [1], LLM 看到 tool results 继续决策
```

## 线程模型

```
Main Thread (Screen::loop)          Worker Thread (LLM / Lua)
────────────────────────            ─────────────────────────
screen.poll_stdin()                 provider.chat_stream()
  ├─ ESC → cancel_.pause()           或 lua::pcall()
  ├─ Ctrl+C ×3 → cancel_.cancel()    ├─ capability calls
  └─ input → LineEditor               └─ stream chunk callbacks
screen.drain_post_queue()
screen.redraw_active_area()         通过 shared_ptr<atomic> + cv 同步
```

## 数据流向

```
LLM response
  ├─ text chunks → on_stream_chunk → screen.post → TUI (Thinking/Response nodes)
  ├─ tool_call   → on_tool_call    → screen.post → TUI (ToolCall node Running)
  │                 execute tool    → capability → EventStream → DataEvent
  │                 on_tool_result  → screen.post → TUI (ToolCall node Done/Failed)
  └─ tool result → conversation    → 下一轮 LLM 输入
```

## 关键设计点

- **虚拟工具**（`manage_tree`, `execute_lua`）在 loop.cppm 拦截，不经过 CapabilityRegistry
- **真实工具** 通过 `ToolBridge` → `Registry` → `Capability::execute()` 执行
- `execute_lua` 提供第二条执行路径：LLM 可以选择生成 Lua 代码批量编排多个 capability，而非逐个 tool call
