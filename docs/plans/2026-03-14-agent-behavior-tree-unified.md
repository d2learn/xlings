# 统一 Agent 执行模型：BehaviorNode + ABehaviorTree

## Context

当前 agent 系统有三套独立的行为追踪结构：
- **TreeNode** (tui.cppm) — TUI 任务树，混合了 plan 节点和 execution 节点
- **ActionNode** (token_tracker.cppm) — 扁平的 LLM/tool call 日志，仅用于 token 计费
- **Action** (lua_engine.cppm) — Lua 执行树，仅在 execute_lua 时填充

三套结构各管各的，没有统一接口。同时 auto-advance 逻辑不可靠。

**目标**：
1. 一个统一的数据结构（BehaviorNode）描述所有 agent 行为
2. 一个干净的管理类（ABehaviorTree）封装所有状态管理和任务推进逻辑

## 核心设计

### 1. BehaviorNode — 统一行为节点

替代 TreeNode + ActionNode + Action：

```cpp
// src/agent/behavior_tree.cppm
export struct BehaviorNode {
    int id {0};

    // 节点类型
    static constexpr int KindPlan     = 0;  // 计划节点（manage_tree add_task）
    static constexpr int KindToolCall = 1;  // 工具调用
    static constexpr int KindResponse = 2;  // 模型文本回复
    static constexpr int KindLuaCall  = 3;  // Lua capability 调用
    int kind {KindPlan};

    // 生命周期状态
    static constexpr int Pending  = 0;
    static constexpr int Running  = 1;
    static constexpr int Done     = 2;
    static constexpr int Failed   = 3;
    static constexpr int Skipped  = 4;
    int state {Pending};

    // 内容
    std::string name;      // 工具名 / 任务标题
    std::string detail;    // 参数摘要 / 回复文本预览

    // 时间
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};

    // 树结构
    std::vector<BehaviorNode> children;

    auto is_terminal() const -> bool {
        return state == Done || state == Failed || state == Skipped;
    }
};
```

### 2. ABehaviorTree — Agent 行为树

```cpp
export class ABehaviorTree {
    mutable std::mutex mtx_;
    BehaviorNode root_;
    int active_plan_id_ {0};
    std::string streaming_text_;

public:
    // ── Plan 管理 ──
    void add_plan(int id, int parent_id, const std::string& title);
    void start_plan(int id, std::int64_t now_ms);
    void complete_plan(int id, std::int64_t now_ms);
    void cancel_plan(int id, std::int64_t now_ms);
    void update_plan(int id, const std::string& title);

    // ── 工具调用 ──
    void begin_tool(int id, const std::string& name,
                    const std::string& args, std::int64_t start_ms);
    void end_tool(int id, bool is_error, std::int64_t end_ms);

    // ── Streaming / Response ──
    void append_streaming(std::string_view text);
    void flush_as_response(int max_chars);
    void clear_streaming();

    // ── Lua 集成 ──
    void begin_lua_call(int parent_id, int lua_id,
                        const std::string& name, std::int64_t start_ms);
    void end_lua_call(int lua_id, bool success, std::int64_t end_ms);

    // ── 生命周期 ──
    void finalize(std::int64_t end_ms);
    void reset();

    // ── 读取（主线程渲染）──
    auto snapshot() const -> BehaviorNode;
    auto streaming_text() const -> std::string;
    auto get_streaming_as_reply() const -> std::string;
};
```

### 3. 任务推进规则

**删除 `maybe_auto_advance` 的 "all children terminal" 检查**。改用三条简单规则：

**规则 A — Auto-start**：
`begin_tool` / `flush_as_response` 时，如果 `active_plan_id_ == 0`，自动 start 第一个 Pending 的 KindPlan 子节点。

**规则 B — Start implies complete**：
`start_plan(N)` 时，如果当前有别的 active plan 且 N ≠ active，自动 complete 旧 plan。

**规则 C — Finalize 兜底**：
turn 结束时 `finalize()` 把所有 Running → Done、Pending → Skipped。

**为什么比现在可靠**：
- 不猜测"任务是否完成"——只有显式 start_plan(下一个) 或 turn 结束才 complete
- 同一任务下连续多个 tool call 不会被误 advance
- 模型不调 start_plan 时，auto-start 保证嵌套

## 文件变更

| 文件 | 变更 |
|------|------|
| `src/agent/behavior_tree.cppm` | **新文件**：BehaviorNode + ABehaviorTree |
| `src/agent/tui.cppm` | 删除 TreeNode、TaskTree；TurnNode.root → BehaviorNode；AgentTuiState → ABehaviorTree |
| `src/agent/ftxui_tui.cppm` | render_tree_node 改为渲染 BehaviorNode，按 kind 选图标/颜色 |
| `src/agent/loop.cppm` | TurnConfig 用 ABehaviorTree 替代 tree_root/id_alloc/on_tree_update |
| `src/cli.cppm` | 回调直接调 ABehaviorTree 方法 |
| `src/agent/agent.cppm` | 更新 re-exports |
| `src/agent/token_tracker.cppm` | 删除 ActionNode（后续） |
| `src/agent/lua_engine.cppm` | 删除 Action/ActionResult，集成 ABehaviorTree（后续） |

## 实施步骤

### Step 1: 创建 behavior_tree.cppm
- BehaviorNode 结构体（含 Kind 和 State 常量）
- ABehaviorTree 类（mutex + 所有方法实现）
- 内部辅助：find_node、find_parent_id、finalize_impl
- IdAllocator 移入此文件

### Step 2: 更新 tui.cppm
- `import xlings.agent.behavior_tree`
- TurnNode.root 改为 BehaviorNode
- AgentTuiState 中 TaskTree → ABehaviorTree
- 删除 TreeNode、TaskTree、旧辅助函数
- 保留 ChatLine、ThinkFilter、IdAllocator（或从 behavior_tree 导入）

### Step 3: 更新 ftxui_tui.cppm
- render_tree_node 接收 `const BehaviorNode&`
- 按 kind 选图标：KindPlan 同现在，KindToolCall 同现在，KindResponse cyan ✓，KindLuaCall 同 ToolCall
- render_turn 接收 snapshot + streaming_text

### Step 4: 更新 loop.cppm
- TurnConfig 中加 `ABehaviorTree* behavior_tree`
- manage_tree 操作直接调 `behavior_tree->add_plan/start_plan` 等
- 删除 on_tree_update callback（ABehaviorTree 直接处理）
- 保留 on_tool_call / on_tool_result（给 cli.cppm 更新 UI 状态用）

### Step 5: 更新 cli.cppm
- on_tool_call 回调：`behavior_tree.flush_as_response()` + `behavior_tree.begin_tool()`
- on_tool_result 回调：`behavior_tree.end_tool()`
- 删除 on_tree_update 回调逻辑（loop.cppm 直接调 ABehaviorTree）
- 渲染用 `behavior_tree.snapshot()`
- turn 结束时 `behavior_tree.finalize()` + 拷贝 snapshot 到 TurnNode.root

### Step 6: 更新 xmake.lua
- 添加 `src/agent/behavior_tree.cppm` 到构建

### Step 7: 后续清理（可单独 PR）
- 删除 ActionNode、TurnResult.actions
- LuaSandbox 集成 ABehaviorTree
- 删除 Action/ActionResult

## 验证

1. `rm -rf build && xmake build` — 编译通过
2. 209 tests pass
3. 手动测试：
   - 多任务场景：tool call 正确嵌套在 plan 节点下
   - 模型不调 start_plan：auto-start 保证嵌套
   - 模型调 start_plan(N+1)：自动 complete 当前 plan
   - 单任务 / 无 plan 场景：tool call 在 root 下（降级兼容）
   - finalize 正确处理剩余 Running/Pending 节点
