# 系统驱动的递归任务树（Recursive Task Tree）

## Context

当前 agent 的执行模型是 **LLM 驱动**：LLM 通过 `manage_tree` 虚拟工具手动管理任务树的结构和推进（add_task、start_task、complete_task）。系统只是被动记录。

问题：
- LLM 看不到树的当前状态，靠自己记忆推断，容易出错
- LLM 管结构 + 推进 = 两件事混在 tool call 里，增加 prompt 复杂度
- 整个 turn 共享一个对话历史，context 随 tool call 数量线性增长
- auto-advance 逻辑是在猜测 LLM 意图，不可靠

**目标**：改为 **系统驱动** 的递归任务树。LLM 在每个节点只做一个决策（执行 or 拆分），系统负责遍历、推进、回溯。

## 核心模型

### 节点有三种角色

```
Decompose    （拆分节点）：有子任务，LLM 返回子任务列表
Execute      （执行节点）：需要 LLM 推理选工具执行（复杂叶子任务）
DirectExecute（直接执行）：拆分时已指定 tool + args，系统直接调用（原子操作）
```

**关键优化：LLM 在 decompose 时可以直接给出原子操作的 tool + args**。系统对这些节点直接调用工具，零 LLM 开销。只有描述模糊、需要推理的子任务才走 Execute 路径。

子节点 TypeToolCall / TypeResponse 是执行过程的记录，不参与遍历。

### 系统 DFS 遍历

```
用户: "install node and python"
  │
  ▼ system: start root, 聚焦 LLM
  │
  LLM decide(decompose):
    subtasks: [
      { title: "搜索 Node.js",  tool: "search_packages", args: {"query":"nodejs"} },  ← DirectExecute
      { title: "安装 Node.js",  tool: "install_packages", args: {"packages":"nodejs"} }, ← DirectExecute
      { title: "搜索 Python",   tool: "search_packages", args: {"query":"python"} },  ← DirectExecute
      { title: "安装 Python",   tool: "install_packages", args: {"packages":"python"} }, ← DirectExecute
      { title: "验证安装结果" }                                                          ← Execute (需 LLM)
    ]
  │
  ▼ system: 前 4 个节点直接调工具（零 LLM 调用）
    A: search_packages("nodejs") → Done, result: "found node 22.x"
    B: install_packages("nodejs") → Done, result: "installed node 22.14"
    C: search_packages("python") → Done, result: "found python 3.12"
    D: install_packages("python") → Done, result: "installed python 3.12.9"
  │
  ▼ E: 没有 tool+args → 聚焦 LLM 做 decide
    LLM 看到: 兄弟结果 [A:found, B:installed, C:found, D:installed]
    decide(execute) → tool use 循环 → "node and python installed successfully"
  │
  ▼ root 所有子节点 Done → root Done → 返回结果
```

**更复杂的例子（嵌套拆分）**：
```
用户: "setup dev environment with node, python and docker"
  │
  LLM decide(decompose):
    subtasks: [
      { title: "Install Node.js ecosystem" },        ← 无 tool+args, 需要 LLM
      { title: "Install Python ecosystem" },          ← 无 tool+args, 需要 LLM
      { title: "Install Docker",
        tool: "install_packages", args: {"packages":"docker"} },  ← DirectExecute
    ]
  │
  ▼ "Install Node.js ecosystem" → LLM decide(decompose):
      subtasks: [
        { title: "Install Node.js", tool: "install_packages", args: {"packages":"nodejs"} },
        { title: "Install pnpm",    tool: "install_packages", args: {"packages":"pnpm"} },
      ]
      → 系统直接执行两个 tool call → Done
  │
  ▼ "Install Python ecosystem" → LLM decide(decompose):
      subtasks: [
        { title: "Install Python", tool: "install_packages", args: {"packages":"python"} },
        { title: "Install pip",    tool: "install_packages", args: {"packages":"pip"} },
      ]
      → 系统直接执行 → Done
  │
  ▼ "Install Docker" → 系统直接执行 → Done
  │
  ▼ root Done
```

### 回溯规则（纯机械，无 LLM 参与）

| 条件 | 动作 |
|------|------|
| 所有子节点 Done | parent → Done, advance 到 parent 的下一个兄弟 |
| 任一子节点 Failed | 剩余兄弟 → Skipped, parent → Failed, 上溯让上层决策 |
| Cancelled | 当前节点 → Failed, 逐层上溯 |

不需要验收步骤。如果某任务需要验证，LLM 应在拆分时显式加一个验证子任务。

## 1. BehaviorNode 变更

文件：`src/agent/behavior_tree.cppm`

```cpp
export struct BehaviorNode {
    int id {0};

    // 节点类型
    inline static constexpr int TypeRoot       = 0;  // 合成根节点
    inline static constexpr int TypeDecompose  = 1;  // 拆分节点（有子任务）
    inline static constexpr int TypeExecute    = 2;  // 执行节点（需要 LLM 推理选工具）
    inline static constexpr int TypeDirectExec = 3;  // 直接执行（拆分时已指定 tool+args）
    inline static constexpr int TypeToolCall   = 4;  // Execute 的子记录：工具调用
    inline static constexpr int TypeResponse   = 5;  // Execute 的子记录：LLM 文本
    int type {TypeRoot};

    // 状态（不变）
    inline static constexpr int Pending  = 0;
    inline static constexpr int Running  = 1;
    inline static constexpr int Done     = 2;
    inline static constexpr int Failed   = 3;
    inline static constexpr int Skipped  = 4;
    int state {Pending};

    // 内容
    std::string name;            // 任务标题 / 工具名
    std::string detail;          // 任务描述 / 参数摘要
    std::string result_summary;  // 完成后的结果摘要（给兄弟节点做 context）

    // DirectExec 专用：拆分时 LLM 指定的工具和参数
    std::string tool;            // 工具名（非空 = DirectExec）
    std::string tool_args;       // JSON 格式的参数

    // 时间
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};

    // 树结构
    std::vector<BehaviorNode> children;

    auto is_terminal() const -> bool {
        return state == Done || state == Failed || state == Skipped;
    }

    auto is_direct_exec() const -> bool {
        return !tool.empty();
    }
};
```

**与当前的差异**：
- `kind` → `type`，新增 TypeRoot / TypeDecompose / TypeExecute / TypeDirectExec
- 新增 `result_summary`：节点完成后填充结果摘要（给兄弟节点做 context）
- 新增 `tool` / `tool_args`：DirectExec 节点的工具名和参数（LLM 在 decompose 时指定）
- 删除旧 KindPlan / KindLuaCall（TypeDecompose 覆盖 KindPlan，KindLuaCall 并入 TypeToolCall）

## 2. ABehaviorTree 变更

```cpp
export class ABehaviorTree {
    mutable std::mutex mtx_;
    BehaviorNode root_;
    int active_node_id_ {0};        // 当前聚焦的节点（替代 active_plan_id_）
    std::string streaming_text_;

public:
    // ── 节点操作（系统调用，非 LLM）──
    void set_root(int id, const std::string& name, const std::string& detail);
    auto add_child(int parent_id, BehaviorNode child) -> int;   // 返回 child.id
    void set_state(int id, int state, std::int64_t now_ms);
    void set_result(int id, const std::string& summary);
    void set_active(int id);
    void skip_remaining(int parent_id, std::int64_t now_ms);    // 剩余 Pending 兄弟 → Skipped

    // ── 工具调用记录（Execute 节点内部）──
    void begin_tool(int parent_id, int tool_id,
                    const std::string& name, const std::string& args,
                    std::int64_t start_ms);
    void end_tool(int tool_id, bool is_error, std::int64_t end_ms);

    // ── Streaming（Execute 节点内部）──
    void append_streaming(std::string_view text);
    void flush_as_response(int parent_id, int max_chars);
    void clear_streaming();

    // ── 读取 ──
    auto snapshot() const -> BehaviorNode;
    auto streaming_text() const -> std::string;
    auto find_node(int id) const -> const BehaviorNode*;  // snapshot 上查找
    void reset();
};
```

**删除的方法**：add_plan, start_plan, complete_plan, cancel_plan, update_plan（都是为 manage_tree 设计的）
**删除的规则**：Rule A/B/C 全部删除。遍历完全由 `run_task_tree` 的调用栈控制。

## 3. LLM 决策协议

删除 `manage_tree` 虚拟工具。新增 `decide` 虚拟工具：

```json
{
  "name": "decide",
  "description": "判断当前任务：直接执行(execute)还是拆分为子任务(decompose)。拆分时，对于明确的原子操作可以直接指定 tool 和 args，系统会自动执行无需再次询问。",
  "inputSchema": {
    "type": "object",
    "properties": {
      "action": {
        "type": "string",
        "enum": ["execute", "decompose"]
      },
      "subtasks": {
        "type": "array",
        "description": "action=decompose 时必填，按执行顺序列出子任务",
        "items": {
          "type": "object",
          "properties": {
            "title": { "type": "string", "description": "子任务标题" },
            "description": { "type": "string", "description": "子任务详细描述" },
            "tool": { "type": "string", "description": "可选：直接执行的工具名。指定后系统自动调用，无需 LLM 介入" },
            "args": { "type": "object", "description": "可选：工具参数 JSON。tool 非空时必填" }
          },
          "required": ["title"]
        }
      }
    },
    "required": ["action"]
  }
}
```

**决策调用流程**：
1. 构建聚焦 prompt（见第 4 节），工具列表只包含 `decide`
2. LLM 调用 `decide` 返回决策
3. 如果 LLM 不调 decide 而直接回复文本或调真实工具 → 视为隐式 execute

**子任务处理（decompose 后）**：

系统遍历 subtasks，根据有无 `tool` 字段分流：

| subtask 字段 | 节点类型 | 系统行为 |
|---|---|---|
| 只有 title（+description） | TypeDecompose（默认） | 递归 → ask_decision → 可能再拆分或 execute |
| 有 tool + args | TypeDirectExec | 系统直接调 `bridge.execute(tool, args)` → Done/Failed |

**Execute 节点流程**（decision == execute 或 depth >= MAX_DEPTH）：
1. 构建新的聚焦 prompt
2. 工具列表包含所有真实工具 + execute_lua，**不含** decide
3. 运行标准 tool-use 循环
4. LLM 最终回复 → 节点 Done

**DirectExec 节点流程**（subtask 有 tool + args）：
1. 无 LLM 调用
2. 系统直接 `bridge.execute(tool, JSON.stringify(args))`
3. 结果 → `result_summary`，isError → Failed/Done

## 4. Context 聚焦

每个节点拿到的 prompt 只包含与当前子树相关的信息：

```
System Prompt:
  [L1 Core: 身份 + 工具列表 + 规则]
  [L4 User Instructions: .agents/prompt/user.md]

  ## 任务上下文
  用户原始请求: {root.name}
  任务路径: {root.name} > {parent.name} > {current.name}

  ## 已完成的兄弟任务
  - [done] {sibling1.name}: {sibling1.result_summary}
  - [failed] {sibling2.name}: {sibling2.result_summary}

  ## 当前任务
  {current.name}
  {current.detail}
```

**不包含的内容**：
- 其他子树的完整对话历史
- 兄弟节点的 tool call 细节（只有 result_summary）
- L2/L3 context cache（那是跨 session 的，不是跨子树的）

**Token 效率**：每个节点独立的对话，天然受控。ContextManager 的 auto-compact 仍可在单个 Execute 节点内工作（如果某个节点的 tool-use 循环很长）。

## 5. 遍历引擎

文件：`src/agent/loop.cppm`

### 新入口：`run_task_tree`

```cpp
struct TreeConfig {
    std::string user_input;
    const std::string& base_system_prompt;  // L1 + L4
    const std::vector<llm::ToolDef>& tools; // 真实工具（不含 decide/manage_tree）
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    ApprovalPolicy* policy;
    ConfirmCallback confirm_cb;
    CancellationToken* cancel;
    ABehaviorTree* tree;
    IdAllocator* id_alloc;
    TokenTracker* tracker;
    ContextManager* ctx_mgr;
    LuaSandbox* lua_sandbox;
    // TUI 回调
    std::function<void(std::string_view)> on_stream_chunk;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    TokenUpdateCallback on_token_update;
};

struct TreeResult {
    std::string reply;           // root 的 result_summary
    int input_tokens {0};
    int output_tokens {0};
    int cache_read_tokens {0};
    int cache_write_tokens {0};
};
```

### 核心递归

```cpp
inline constexpr int MAX_DEPTH = 5;

void process_node(BehaviorNode& node, TreeConfig& tc, int depth) {
    if (tc.cancel && !tc.cancel->is_active()) throw ...;

    tc.tree->set_state(node.id, BehaviorNode::Running, now());
    tc.tree->set_active(node.id);

    // ── DirectExec：系统直接调工具，零 LLM ──
    if (node.is_direct_exec()) {
        node.type = BehaviorNode::TypeDirectExec;
        auto result = tc.bridge.execute(node.tool, node.tool_args, tc.stream, tc.cancel);
        node.state = result.isError ? BehaviorNode::Failed : BehaviorNode::Done;
        node.result_summary = result.content.substr(0, 200);
        node.end_ms = now();
        tc.tree->set_state(node.id, node.state, node.end_ms);
        tc.tree->set_result(node.id, node.result_summary);
        return;
    }

    // ── 决策：LLM 判断 execute or decompose ──
    auto decision = ask_decision(node, tc, depth);

    if (decision.is_execute || depth >= MAX_DEPTH) {
        // ── Execute：LLM tool-use 循环 ──
        node.type = BehaviorNode::TypeExecute;
        auto result = run_execute(node, tc);
        node.state = result.failed ? BehaviorNode::Failed : BehaviorNode::Done;
        node.result_summary = result.reply.substr(0, 200);
        node.end_ms = now();
        tc.tree->set_state(node.id, node.state, node.end_ms);
        tc.tree->set_result(node.id, node.result_summary);
    } else {
        // ── Decompose：创建子节点，DFS 遍历 ──
        node.type = BehaviorNode::TypeDecompose;
        for (auto& sub : decision.subtasks) {
            BehaviorNode child;
            child.id = tc.id_alloc->alloc();
            child.name = sub.title;
            child.detail = sub.description;
            child.tool = sub.tool;          // 可能为空
            child.tool_args = sub.tool_args; // 可能为空
            tc.tree->add_child(node.id, child);
            node.children.push_back(std::move(child));
        }

        // ── DFS 顺序执行子节点 ──
        for (auto& child : node.children) {
            if (child.type == BehaviorNode::TypeToolCall ||
                child.type == BehaviorNode::TypeResponse) continue;

            child.start_ms = now();
            process_node(child, tc, depth + 1);  // 递归（DirectExec 在开头短路）

            if (child.state == BehaviorNode::Failed) {
                tc.tree->skip_remaining(node.id, now());
                node.state = BehaviorNode::Failed;
                node.result_summary = "Failed: " + child.name + " - " + child.result_summary;
                node.end_ms = now();
                tc.tree->set_state(node.id, node.state, node.end_ms);
                return;
            }
        }

        node.state = BehaviorNode::Done;
        node.result_summary = synthesize_children(node);
        node.end_ms = now();
        tc.tree->set_state(node.id, node.state, node.end_ms);
        tc.tree->set_result(node.id, node.result_summary);
    }
}
```

### `ask_decision` — 决策调用

```cpp
auto ask_decision(BehaviorNode& node, TreeConfig& tc, int depth) -> Decision {
    if (depth >= MAX_DEPTH) return Decision{.is_execute = true};

    auto prompt = build_scoped_prompt(node, tc, /*decision_mode=*/true);
    auto tools = std::vector<llm::ToolDef>{ decide_tool_def() };

    llm::Conversation conv;
    conv.push(llm::Message::system(prompt));
    conv.push(llm::Message::user("Task: " + node.name + "\n" + node.detail));

    auto response = llm_call(...);  // 复用现有 llm_call_worker

    // 解析 decide tool call
    auto calls = response.tool_calls();
    if (calls.empty() || calls[0].name != "decide") {
        return Decision{.is_execute = true};  // fallback
    }

    auto json = parse(calls[0].arguments);
    if (json["action"] == "decompose" && json.contains("subtasks")) {
        Decision d{.is_execute = false};
        for (auto& s : json["subtasks"]) {
            SubtaskDef sub;
            sub.title = s["title"];
            sub.description = s.value("description", "");
            // DirectExec: LLM 指定了 tool + args
            if (s.contains("tool") && s["tool"].is_string()) {
                sub.tool = s["tool"];
                sub.tool_args = s.contains("args") ? s["args"].dump() : "{}";
            }
            d.subtasks.push_back(std::move(sub));
        }
        return d;
    }
    return Decision{.is_execute = true};
}
```

### `run_execute` — 执行循环

复用现有 `run_one_turn` 的内层循环，但：
- **新建独立 Conversation**（聚焦 prompt）
- **工具列表不含 decide / manage_tree**
- **tool call/result 挂在当前 Execute 节点下**
- 迭代限制不变（MAX_ITERATIONS=50）

```cpp
auto run_execute(BehaviorNode& node, TreeConfig& tc) -> NodeResult {
    auto prompt = build_scoped_prompt(node, tc, /*decision_mode=*/false);
    llm::Conversation conv;
    conv.push(llm::Message::system(prompt));
    conv.push(llm::Message::user(node.name + "\n" + node.detail));

    // ... 复用现有 LLM call → tool dispatch → LLM call 循环 ...
    // begin_tool / end_tool 挂在 node.id 下
    // 返回 NodeResult{reply, tokens, failed}
}
```

## 6. Prompt 构建

文件：`src/agent/prompt_builder.cppm`

新增方法：

```cpp
auto build_scoped(
    const std::string& node_name,
    const std::string& node_detail,
    const std::vector<std::string>& ancestor_path,
    const std::vector<std::pair<std::string, std::string>>& sibling_results,  // {name, summary}
    bool decision_mode  // true: 只含 decide 工具说明; false: 含完整工具列表
) const -> std::string;
```

decision_mode=true 时，prompt 末尾加：
```
判断这个任务：可以直接用工具完成，还是需要拆分为子任务？
调用 decide 工具返回你的决策。
```

decision_mode=false 时，prompt 末尾加：
```
使用工具完成这个任务。完成后给出简短的结果摘要。
```

## 7. TUI 渲染

文件：`src/agent/ftxui_tui.cppm`

图标映射：
| Type | Icon | 颜色规则 |
|------|------|---------|
| TypeDecompose | ○/⟳/✓/✗/▷ (按 state) | 同现在 |
| TypeExecute | ○/⟳/✓/✗/▷ (按 state) | 同现在 |
| TypeDirectExec | ⚙/✓/✗ (按 state) | amber(running) / green(done) / red(failed) |
| TypeToolCall | 同现在 | 同现在 |
| TypeResponse | ✓ | cyan |

DirectExec 节点显示格式：`⚙ search_packages("nodejs")  0.3s`（工具名+参数摘要+耗时）

渲染逻辑不需要大改 — `render_tree_node` 已经是递归的，只要改 `node_icon` / `node_color` 的 switch 从 `kind` 改为 `type`。

Status bar 活动指示器改为显示任务路径：`⟳ Install Node.js > Download binary  3.2s`

## 8. 文件变更总结

| 文件 | 变更程度 | 说明 |
|------|---------|------|
| `src/agent/behavior_tree.cppm` | **大改** | BehaviorNode type 系统, ABehaviorTree 删除 plan 管理方法, 改为通用 set_state/add_child |
| `src/agent/loop.cppm` | **大改** | 新增 run_task_tree + process_node + ask_decision + run_execute, 删除 manage_tree 相关, 提取 tool-use 内循环 |
| `src/agent/prompt_builder.cppm` | **中改** | 新增 build_scoped() 方法 |
| `src/agent/ftxui_tui.cppm` | **小改** | type 替代 kind 的图标/颜色映射, status bar 任务路径 |
| `src/agent/tui.cppm` | **小改** | 更新 BehaviorNode re-export |
| `src/cli.cppm` | **中改** | run_one_turn → run_task_tree, 删除 manage_tree 相关回调, 简化 TurnConfig 构建 |
| `src/agent/session.cppm` | **小改** | 新增 save_tree/load_tree (BehaviorNode JSON 序列化) |
| `src/agent/token_tracker.cppm` | **不变** | TokenTracker 仍跨节点累加 |
| `src/agent/context_manager.cppm` | **不变** | 仍可在 Execute 节点内 auto-compact |

## 9. 实施步骤

### Phase 1: 提取 tool-use 内循环（无行为变化）
- 从 `run_one_turn` 提取 `run_tool_loop()` 函数
- `run_one_turn` 内部调用 `run_tool_loop()`
- 验证编译通过 + 209 tests pass

### Phase 2: BehaviorNode type 系统
- 新增 TypeRoot/TypeDecompose/TypeExecute 常量
- 新增 `result_summary` 字段
- ABehaviorTree 新增 set_state/add_child/set_active/skip_remaining
- 保留旧方法（add_plan 等）暂时共存
- 更新 ftxui_tui.cppm 渲染
- 验证编译通过

### Phase 3: 决策引擎
- 新增 `decide_tool_def()`
- 新增 `ask_decision()` + `process_node()` + `run_task_tree()`
- `run_execute()` 调用 Phase 1 提取的 `run_tool_loop()`
- PromptBuilder 新增 `build_scoped()`
- cli.cppm 改为调用 `run_task_tree`
- 验证编译通过

### Phase 4: 删除旧代码
- 删除 `manage_tree_tool_def()`, `handle_manage_tree()`, `handle_tree_op()`
- 删除 ABehaviorTree 的 add_plan/start_plan/complete_plan 等旧方法
- 删除 `run_one_turn`（或保留为 `run_task_tree` 的简单包装）
- 删除 loop.cppm 中的旧 system prompt（manage_tree 指南部分）
- 验证编译通过 + 全量测试

## 10. 验证

1. `rm -rf build && xmake build` — 编译通过
2. 209 tests pass
3. 手动测试场景：
   - **简单任务**（"search vim"）：root → decide(execute) → tool call → Done
   - **DirectExec**（"install node and python"）：root → decompose 带 tool+args → 系统直接执行 4 个工具 → Done（验证零 LLM 中间调用）
   - **混合模式**：decompose 返回的 subtasks 中既有 tool+args 的也有纯 title 的 → DirectExec 和 Execute/Decompose 混合
   - **嵌套拆分**（"setup dev environment"）：root → decompose → 子节点再 decompose → 递归执行
   - **失败传播**：DirectExec 节点 tool 返回 isError → Failed → 兄弟 Skipped → parent Failed
   - **深度限制**：depth >= 5 时强制 execute
   - **取消**：Ctrl+C → 当前节点中断 → 逐层上溯
   - **TUI 显示**：DirectExec 节点显示 ⚙ 图标 + 工具名参数，树正确递归渲染
