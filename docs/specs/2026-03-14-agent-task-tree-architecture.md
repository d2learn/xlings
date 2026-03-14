# Agent Task Tree Architecture

## 1. Overview

xlings-agent 使用 **两类型递归任务树** 执行用户请求。系统由三个核心组件协作：

```
┌─────────────┐     ask_decision      ┌─────────────┐
│             │ ◄──────────────────── │             │
│     LLM     │   decide(decompose/   │  process_   │
│  (外部API)   │    done) + subtasks   │   node      │
│             │ ────────────────────► │  (递归引擎)   │
└─────────────┘                       └──────┬──────┘
                                             │
                                    set_state / add_child
                                             │
                                             ▼
                                      ┌─────────────┐
                                      │ ABehavior   │
                                      │   Tree      │     snapshot()
                                      │ (线程安全)   │ ──────────────► TUI 渲染
                                      └─────────────┘
```

- **LLM**: 只做决策（execute or decompose），不直接执行工具
- **process_node**: 系统引擎，递归遍历树，调用工具，管理 re-plan
- **ABehaviorTree**: 线程安全的树状态存储，TUI 通过 snapshot() 读取渲染

## 2. Node Types

只有两种节点类型：

### TypeAtom (0) — 原子操作

```
┌──────────────────────────────────┐
│  TypeAtom                        │
│                                  │
│  tool: "remove_package"          │  LLM 在 decompose 时指定
│  tool_args: {"target":"mdbook"}  │  tool + args
│                                  │
│  执行: bridge.execute(tool, args)│  系统直接调用
│  LLM 调用: 0 次                  │  零开销
│                                  │
│  TUI: ⚙ remove_package({"target":"mdbook"})  3ms │
└──────────────────────────────────┘
```

### TypePlan (1) — 决策节点

```
┌──────────────────────────────────┐
│  TypePlan                        │
│                                  │
│  name: "卸载 mdbook"             │  任务标题
│  tool: ""  (空 = 需要 LLM)       │
│                                  │
│  执行: ask_decision(LLM) →       │
│    decompose: 创建子节点          │  1 次 LLM 调用
│    done: 直接完成                 │
│                                  │
│  TUI: ○/⟳/✓/✗ 卸载 mdbook  3.2s │
└──────────────────────────────────┘
```

### 判定规则

```
node.tool 非空？
  ├─ YES → TypeAtom (系统直接执行)
  └─ NO  → TypePlan (LLM 决策)
```

## 3. BehaviorNode 数据结构

```cpp
struct BehaviorNode {
    int id;

    // 类型: TypeAtom(0) | TypePlan(1)
    int type;

    // 状态: Pending(0) | Running(1) | Done(2) | Failed(3) | Skipped(4)
    int state;

    // 内容
    std::string name;            // 任务标题 / 工具名
    std::string detail;          // 描述 / 参数
    std::string result_summary;  // 完成后的结果摘要

    // Atom 专用
    std::string tool;            // 工具名 (非空 = Atom)
    std::string tool_args;       // JSON 参数

    // 时间
    int64_t start_ms, end_ms;

    // 树结构
    vector<BehaviorNode> children;
};
```

## 4. ABehaviorTree API

```
┌─────────────────────────────────────────────────────┐
│  ABehaviorTree (mutex 保护, 线程安全)                 │
│                                                     │
│  ── 写入 (worker thread) ──                          │
│  set_root(id, name, detail)   初始化根节点            │
│  add_child(parent_id, child)  添加子节点              │
│  set_state(id, state, ms)     更新节点状态            │
│  set_result(id, summary)      设置结果摘要            │
│  set_active(id)               设置当前活跃节点         │
│  skip_remaining(parent_id)    跳过剩余 Pending 子节点  │
│  finalize(end_ms)             Running→Done, Pending→Skipped │
│  reset()                      清空整棵树              │
│                                                     │
│  ── 读取 (main/TUI thread) ──                        │
│  snapshot() → BehaviorNode    深拷贝整棵树             │
│  streaming_text() → string    当前流式文本             │
│  has_streaming() → bool                              │
└─────────────────────────────────────────────────────┘
```

**线程模型**:
```
Main Thread (TUI)          Worker Thread (agent)
     │                           │
     │  snapshot()               │  set_root()
     │◄─────────────────────    │  add_child()
     │  (深拷贝, 无锁争用)       │  set_state()
     │                           │  set_result()
     │  render(snapshot)          │  ...
     │                           │
```

## 5. process_node 完整流程

```
process_node(node, depth)
│
│ ┌─────────────────────────────────────────────────────┐
│ │ 取消检查: cancel_token.is_active()?                  │
│ │ 设置状态: node → Running                             │
│ │ 更新 TUI: tree->set_state, tree->set_active          │
│ └─────────────────────────────────────────────────────┘
│
├─ node.is_atom()?  ──YES──►  ┌─────────────────────────┐
│                              │  ATOM 执行路径            │
│                              │                         │
│                              │  1. approval 检查        │
│                              │     policy->check()      │
│                              │     denied → Failed      │
│                              │     need_confirm → 询问   │
│                              │                         │
│                              │  2. 特殊处理             │
│                              │     execute_lua →        │
│                              │       lua_sandbox        │
│                              │                         │
│                              │  3. 普通工具             │
│                              │     bridge.execute()     │
│                              │                         │
│                              │  4. 结果                 │
│                              │     isError → Failed     │
│                              │     !isError → Done      │
│                              │     result_summary 填充   │
│                              │                         │
│                              │  TUI 回调:               │
│                              │    on_tool_call          │
│                              │    on_tool_result        │
│                              └─────────┬───────────────┘
│                                        │ return
│
└─ NO (TypePlan)
   │
   │  ┌──────────────────────────────────────────────┐
   │  │  ask_decision (1次 LLM 调用)                  │
   │  │                                              │
   │  │  prompt = base_system_prompt                  │
   │  │        + task_context (ancestor_path)          │
   │  │        + sibling_results                      │
   │  │        + available_tools (with schemas)        │
   │  │        + conversation_history (depth=0 only)   │
   │  │                                              │
   │  │  tools = [decide]  (只有 decide 工具)          │
   │  │                                              │
   │  │  LLM 返回: decide({action, summary, subtasks})│
   │  └───────────────┬──────────────────────────────┘
   │                  │
   │         ┌────────┴────────┐
   │         │                 │
   │      "done"          "decompose"
   │         │                 │
   │         ▼                 ▼
   │    ┌──────────┐    ┌──────────────────────────┐
   │    │ 直接完成  │    │  创建子节点                │
   │    │          │    │                          │
   │    │ state=   │    │  subtask 有 tool+args     │
   │    │  Done    │    │  → TypeAtom 子节点         │
   │    │ summary  │    │                          │
   │    │ = LLM    │    │  subtask 无 tool+args     │
   │    │  给的    │    │  → TypePlan 子节点          │
   │    │          │    │  (递归 process_node)       │
   │    │ return   │    │                          │
   │    └──────────┘    │  depth >= MAX_DEPTH(5)?   │
   │                    │  → 只允许 Atom, 丢弃 Plan  │
   │                    └────────────┬─────────────┘
   │                                 │
   │                                 ▼
   │                    ┌──────────────────────────┐
   │                    │  DFS 顺序执行子节点        │
   │                    │  execute_pending_children │
   │                    │                          │
   │                    │  对每个 Pending 子节点:     │
   │                    │    构建 sibling_results   │
   │                    │    (前面已完成节点的结果)    │
   │                    │    递归 process_node       │
   │                    │                          │
   │                    │  失败不停止, 继续执行       │
   │                    └────────────┬─────────────┘
   │                                 │
   │                                 ▼
   │                    ┌──────────────────────────┐
   │                    │  全部完成, 有失败节点?      │
   │                    └─────┬────────────┬───────┘
   │                       否 │            │ 是
   │                          ▼            ▼
   │                    ┌──────────┐  ┌────────────────────┐
   │                    │ 全部成功  │  │  RE-PLAN 循环       │
   │                    │          │  │  (最多 MAX_REPLAN=3) │
   │                    │ state=   │  │                    │
   │                    │  Done    │  │  详见下方            │
   │                    │ summary  │  │                    │
   │                    │ = synth  │  └────────────────────┘
   │                    │  esize   │
   │                    │          │
   │                    │ return   │
   │                    └──────────┘
```

## 6. Re-plan 机制

```
┌─────────────────────────────────────────────────────────────┐
│                     RE-PLAN 循环                             │
│                                                             │
│  for replan = 0..MAX_REPLAN:                                │
│                                                             │
│    ┌─────────────────────────────────────────────────────┐  │
│    │  ask_decision (re-plan 模式, 1次 LLM 调用)           │  │
│    │                                                     │  │
│    │  prompt 额外包含:                                    │  │
│    │  ## Completed Subtasks                               │  │
│    │  - [OK] remove_package("d2x"): exitCode=0           │  │
│    │  - [FAILED] remove_package("mdbook"): exitCode=1    │  │
│    │                                                     │  │
│    │  "评估父任务是否已完成, 而不是机械重试"                  │  │
│    └──────────────────┬──────────────────────────────────┘  │
│                       │                                     │
│              ┌────────┴────────┐                            │
│              │                 │                            │
│           "done"          "decompose"                       │
│              │                 │                            │
│              ▼                 ▼                            │
│    ┌──────────────────┐  ┌──────────────────────────┐      │
│    │  接受当前结果      │  │  追加新子节点              │      │
│    │                  │  │                          │      │
│    │  添加可见标记:     │  │  包裹在 "re-plan #N"      │      │
│    │  "re-plan: xxx"  │  │  Plan 子节点下             │      │
│    │                  │  │                          │      │
│    │  node → Done     │  │  执行新子节点              │      │
│    │  return          │  │  → 回到循环顶部检查         │      │
│    └──────────────────┘  └──────────────────────────┘      │
│                                                             │
│  循环结束仍有失败 → node → Failed                             │
└─────────────────────────────────────────────────────────────┘
```

**TUI 中的 re-plan 可见性**:
```
├─ ✗ remove_package({"target":"d2x"})       ← 第一轮, 失败
├─ ✓ remove_package({"target":"mdbook"})    ← 第一轮, 成功
├─ ✓ re-plan #1                             ← re-plan 子节点 (可见)
│  └─ ✓ remove_package({"target":"xim:d2x"})  ← 重试成功
```
或接受失败:
```
├─ ✗ remove_package({"target":"d2x"})
├─ ✓ remove_package({"target":"mdbook"})
└─ ✓ re-plan: d2x未安装无需卸载, mdbook已卸载   ← 接受标记
```

## 7. Context 传递

### 7.1 跨 Turn 上下文 (Conversation)

```
Turn 1: "安装 vim"       → conversation.push(user, assistant)
Turn 2: "刚才装了什么"    → ask_decision 注入 conversation 历史
Turn 3: "卸载它"         → ask_decision 看到 Turn 1+2, 知道 "它"=vim
```

**规则**: 只在 `depth == 0` (根节点) 注入 conversation 历史。嵌套节点只用 sibling_results。

### 7.2 同层上下文 (Sibling Results)

```
Parent Plan: "卸载 d2x 和 mdbook"
├─ Atom: list_packages()  → Done, result_summary = {...包列表...}
├─ Plan: "卸载 d2x"       → ask_decision 看到:
│    sibling_results = [
│      ("list_packages()", "list_packages: {exitCode:0, ...包列表...}")
│    ]
│    → LLM 看到 d2x 不在列表中 → 直接 done
│
├─ Plan: "卸载 mdbook"    → ask_decision 看到:
│    sibling_results = [
│      ("list_packages()", "list_packages: {...}"),
│      ("卸载 d2x", "d2x未安装")
│    ]
```

### 7.3 层级上下文 (Ancestor Path)

```
prompt 中:
  User request: 安装 mdbook 第二新版本
  Task path: 安装 mdbook 第二新版本 > 根据版本信息安装第二新版本
```

### 7.4 synthesize_children (结果汇总)

子节点结果向上传播:
```
Plan 子节点: 使用 LLM 生成的 result_summary
Atom 子节点: "tool_name: raw_result"  (包含工具名 + 原始输出)
```

## 8. TUI 渲染

### 图标映射

| 类型 | 状态 | 图标 | 颜色 |
|------|------|------|------|
| Atom | Pending | ⚙ | dim |
| Atom | Running | ⚙ | amber |
| Atom | Done | ✓ | green |
| Atom | Failed | ✗ | red |
| Plan | Pending | ○ | dim |
| Plan | Running | ⟳ | amber |
| Plan | Done | ✓ | green |
| Plan | Failed | ✗ | red |
| Plan | Skipped | ▷ | dim |

### 显示格式

```
Atom:  ⚙ tool_name(compact_args)  duration
Plan:  ○ task_title  duration
```

Atom 的 compact_args 从 JSON 提取值: `{"target":"mdbook"}` → `"mdbook"`

### 树结构渲染

```
⏵ 卸载 d2x 和 mdbook  5.2s                    ← TurnNode header
├─ ✓ list_packages()  20ms                     ← Atom
├─ ✓ 卸载 d2x  2.1s                            ← Plan (可展开)
│  └─ ✓ remove_package("d2x")  13ms            ← Atom
├─ ✓ 卸载 mdbook  2.3s                         ← Plan
│  └─ ✓ remove_package("xim:mdbook")  15ms     ← Atom
────────────────────────────────────────────
◆ d2x 和 mdbook 已成功卸载                      ← reply
```

## 9. 数据流全景

```
用户输入
  │
  ▼
run_task_tree(TreeConfig)
  │
  ├─ 创建 Root (TypePlan)
  │   tree->set_root()
  │
  ├─ process_node(root, depth=0)
  │   │
  │   ├─ ask_decision ──────────► LLM API call
  │   │   prompt:                  (with decide tool)
  │   │   - base_system_prompt     │
  │   │   - conversation history   │
  │   │   - task context           │
  │   │   - tool schemas           ◄── decide({action, subtasks})
  │   │
  │   ├─ decompose → create_children
  │   │   │   tree->add_child × N
  │   │   │
  │   │   ├─ Atom child ──► bridge.execute() ──► ToolResult
  │   │   │   tree->set_state(Done/Failed)
  │   │   │   on_tool_call / on_tool_result → TUI 状态更新
  │   │   │
  │   │   ├─ Plan child ──► process_node(depth+1) ──► 递归
  │   │   │
  │   │   └─ (all done) ──► has_failed? ──► re-plan loop
  │   │                                      ask_decision(replan)
  │   │                                      → done / decompose
  │   │
  │   └─ result_summary 填充
  │       tree->set_state(Done)
  │       tree->set_result(summary)
  │
  ├─ conversation->push(user, reply)  ← 跨 turn 持久化
  │
  └─ return TreeResult{reply, tokens}

                    同时:
              ┌─────────────────┐
              │   TUI Thread    │
              │                 │
              │  每 200ms:       │
              │  snap = tree    │
              │    ->snapshot() │
              │  render(snap)   │
              └─────────────────┘
```

## 10. 关键常量

| 常量 | 值 | 含义 |
|------|---|------|
| MAX_DEPTH | 5 | 最大嵌套深度, 超过则强制全 Atom |
| MAX_REPLAN | 3 | 每个 Plan 节点最多 re-plan 次数 |
| BASE_ID | 10000 | IdAllocator 起始 ID |

## 11. 文件映射

| 文件 | 职责 |
|------|------|
| `behavior_tree.cppm` | BehaviorNode 定义, ABehaviorTree 线程安全存储, IdAllocator |
| `loop.cppm` | process_node 递归引擎, ask_decision LLM 调用, re-plan, TreeConfig/TreeResult |
| `ftxui_tui.cppm` | TUI 渲染 (node_icon, node_color, render_tree_node, AgentScreen) |
| `tui.cppm` | TurnNode, AgentTuiState, ThinkFilter, re-export behavior_tree types |
| `tool_bridge.cppm` | ToolBridge 工具执行 (exitCode→isError), event buffer |
| `cli.cppm` | Agent REPL, TreeConfig 构建, TUI 回调, session 管理 |
| `prompt_builder.cppm` | PromptBuilder L1-L4 分层 prompt, build_scoped |
