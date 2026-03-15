# Agent TUI 统一任务树重设计

## Context

基于真实使用反馈，当前行为树存在核心问题：
1. **取消机制失效**：主线程/进程卡住无法响应，ESC 应暂停而非取消，Ctrl+C×3 退出
2. **树根语义错误**：`Turn 1` 无意义，应以用户消息为根
3. **树结构扁平**：当前所有节点都是 TurnRoot 的直接子节点，无法表达递归任务拆解
4. **缺乏动态任务管理**：模型无法主动拆解/更新任务，缺乏结构化输出

### 核心设计理念

一棵树同时作为 **执行树 + 任务拆解树 + 状态树**：
- **每个节点服务于其父节点的目标**
- **任意节点都可以有子树**（递归拆解，不限深度）
- **未执行节点可动态更新**（模型发现计划变化时调整）
- **计划更新本身也是动作**，被记录到树中
- **节点结束 = 任务完成 OR 识别该任务应取消** → 进入同级下一个节点
- **执行中发现需要进一步拆解**，可以在当前节点下创建子任务
- **叶子节点 = 具体可执行的确定性任务**（工具调用等）

### UI 渲染方案：直接动态树

**Pending 节点 = 计划的可视化**。`manage_tree(add_task)` 创建 Pending 状态节点，用 dim 颜色 + `○` 图标显示。
模型先拆解出一级任务（Pending），再逐个执行（Running → Done/Failed）。执行中如需进一步拆解，在当前节点下 add 子节点。

ftxui 每帧从 TreeNode 数据递归重建 Element 树（`render_tree_node`），只需 `screen.Post` 中修改数据，下一帧自动渲染。无需 diff/patch，动态更新是免费的。

```
> 安装 nodejs 并配置环境                      ← UserTask (Running)
├─ ✓ 搜索 nodejs 版本                1.8s    ← SubTask (Done)
│  └─ ✓ search_packages(...)         1.2s    ← ToolCall (叶子)
├─ … 安装 nodejs v20                          ← SubTask (Running, 当前活跃)
│  ├─ ✗ install_packages(...)        3.2s    ← ToolCall (Failed)
│  ├─ ○ 清理残留                              ← SubTask (Pending, 动态新增)
│  └─ ○ 使用备选源安装                         ← SubTask (Pending, 动态新增)
├─ ○ 配置环境变量                              ← SubTask (Pending, 等待)
```

---

## Phase 1: CancellationToken 升级 — 暂停/恢复

**文件**: `src/runtime/cancellation.cppm`

将 `atomic<bool> cancelled_` 替换为 `atomic<int> state_`（0=Active, 1=Paused, 2=Cancelled）。

```cpp
export struct PausedException : std::runtime_error {
    PausedException() : std::runtime_error("operation paused") {}
};

export class CancellationToken {
    std::atomic<int> state_{0};  // 0=Active, 1=Paused, 2=Cancelled
    std::mutex mtx_;
    std::condition_variable cv_;

public:
    void pause()  { state_.store(1, std::memory_order_release); cv_.notify_all(); }
    void resume() { state_.store(0, std::memory_order_release); cv_.notify_all(); }
    void cancel() { state_.store(2, std::memory_order_release); cv_.notify_all(); }
    void reset()  { state_.store(0, std::memory_order_release); }

    bool is_paused() const    { return state_.load(std::memory_order_acquire) == 1; }
    bool is_cancelled() const { return state_.load(std::memory_order_acquire) == 2; }
    bool is_active() const    { return state_.load(std::memory_order_acquire) == 0; }

    void throw_if_cancelled() {
        auto s = state_.load(std::memory_order_acquire);
        if (s == 2) throw CancelledException{};
        if (s == 1) throw PausedException{};
    }

    // wait_or_cancel: Paused 也返回 false
    template<typename Pred>
    bool wait_or_cancel(std::unique_lock<std::mutex>& lock,
                        std::condition_variable& cv, Pred pred,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) {
        // 同现有逻辑，将 is_cancelled() 改为 !is_active()
    }
};
```

**注意**: 使用 `std::atomic<int>`（非 enum atomic），避免 GCC 15 模块 ICE。

---

## Phase 2: TreeNode 递归任务模型

**文件**: `src/agent/tui.cppm`

### 新 TreeNode 结构

```cpp
export struct TreeNode {
    enum State { Pending, Running, Done, Failed, Cancelled, Paused };
    enum Kind { UserTask, SubTask, Thinking, ToolCall, PlanUpdate, Detail, Download, Response };

    Kind kind;
    State state {Pending};
    std::string title;           // 树节点显示文本（1行）
    std::string details_json;    // JSON 详情（默认折叠）
    int node_id {-1};            // manage_tree 分配的 ID（SubTask用）
    int action_id {-1};          // 全局 action 计数器
    int64_t start_ms {0};
    int64_t end_ms {0};
    int input_tokens {0};
    int output_tokens {0};

    // Download 专用
    float progress {0.0f};
    std::string speed;
    std::string eta;

    bool expanded {false};       // 详情展开控制

    std::vector<TreeNode> children;

    auto line_count() const -> int {
        int n = 1;
        if (kind == Response && state == Running && !title.empty()) n = 2;
        for (auto& c : children) n += c.line_count();
        return n;
    }

    // 查找节点 by node_id（递归）
    auto find_node(int id) -> TreeNode* {
        if (node_id == id) return this;
        for (auto& c : children) {
            if (auto* found = c.find_node(id)) return found;
        }
        return nullptr;
    }
};
```

### 关键变化

| 旧 | 新 | 说明 |
|----|-----|------|
| `text` | `title` | 语义更清晰 |
| `done/failed` | `State` 枚举 | 6 种状态：Pending/Running/Done/Failed/Cancelled/Paused |
| `TurnRoot` | `UserTask` | 根 = 用户消息 |
| — | `SubTask` | 模型拆解的子任务（可递归包含子任务） |
| — | `PlanUpdate` | 计划变更记录节点 |
| — | `Cancelled` | 模型主动取消的任务 |
| — | `node_id` | 任务节点 ID，用于 manage_tree 操作 |
| — | `find_node()` | 递归查找，支持任意深度 |

### 状态图标

```
Pending   → ○ (dim)
Running   → … (amber) / ⚡ (ToolCall) / ▸ (Download)
Done      → ✓ (green)
Failed    → ✗ (red)
Cancelled → ⊘ (dim, 删除线效果)
Paused    → ⏸ (cyan)
```

### render_tree_node 更新

- UserTask 根：`> 用户消息`（amber bold）+ 耗时
- SubTask：`○/…/✓/✗/⊘ 任务标题` + 耗时，递归渲染子节点
- Pending 的 SubTask 用 dim 颜色，表示未开始
- PlanUpdate：`↻ 更新计划: ...`（cyan）
- Response Running：`◆` + paragraph（流式文本）
- Response Done：`✓` + 截断 title

---

## Phase 3: manage_tree 虚拟工具 + 活跃任务跟踪

**文件**: `src/agent/tui.cppm`（TaskTree 类）, `src/agent/tool_bridge.cppm`（注册）, `src/agent/loop.cppm`（集成）

### 3a. TaskTree 管理器（tui.cppm 新增）

```cpp
export class TaskTree {
    int next_node_id_{1};
    int active_node_id_{-1};  // 当前正在执行的任务节点

public:
    // 创建子任务，返回 node_id
    auto add_task(TreeNode& root, int parent_id, const std::string& title,
                  const std::string& details = "") -> int;

    // 开始执行任务
    void start_task(TreeNode& root, int node_id);

    // 完成任务（done / failed / cancelled）
    void complete_task(TreeNode& root, int node_id, TreeNode::State state,
                       const std::string& result = "");

    // 更新未执行任务的标题/详情
    void update_task(TreeNode& root, int node_id, const std::string& new_title,
                     const std::string& new_details = "");

    // 获取/设置活跃节点
    int active_node() const { return active_node_id_; }
    void set_active(int id) { active_node_id_ = id; }

    // 工具调用/Thinking/Response 自动挂载到 active_node 下
    auto active_parent(TreeNode& root) -> TreeNode* {
        if (active_node_id_ > 0) return root.find_node(active_node_id_);
        return &root;  // fallback: 挂到根节点
    }
};
```

**自动嵌套规则**：
- 模型调用 `manage_tree(start_task, id=3)` → `active_node_id_ = 3`
- 后续 ToolCall/Thinking/Response 节点自动成为 node 3 的 children
- 模型调用 `manage_tree(complete_task, id=3)` → active 回退到 node 3 的 parent
- 如果模型没用 manage_tree → 所有节点挂在 UserTask 根下（兼容旧行为）

### 3b. manage_tree 工具定义（ToolBridge 注册）

不通过 CapabilityRegistry，而是在 `run_one_turn` 中直接处理（虚拟工具，不走 bridge.execute）。

工具 schema:
```json
{
  "name": "manage_tree",
  "description": "Manage the task tree: decompose tasks, track progress, update plans. Call this to structure your work before executing.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "action": {
        "type": "string",
        "enum": ["add_task", "start_task", "complete_task", "cancel_task", "update_task"]
      },
      "parent_id": { "type": "integer", "description": "Parent node ID (for add_task). 0 = root." },
      "node_id": { "type": "integer", "description": "Target node ID (for start/complete/cancel/update)." },
      "title": { "type": "string", "description": "Task title (for add_task/update_task)." },
      "details": { "type": "string", "description": "JSON details (optional)." },
      "result": { "type": "string", "description": "Completion result (for complete_task)." }
    },
    "required": ["action"]
  }
}
```

### 3c. loop.cppm 集成

在 `run_one_turn` 的 tool call 处理中，拦截 `manage_tree`：

```cpp
for (const auto& call : calls) {
    if (call.name == "manage_tree") {
        // 解析 JSON，调用 TaskTree 方法
        // 通过回调通知 TUI 更新
        // 返回 ToolResult 带 node_id / 状态
        auto result = handle_manage_tree(call, task_tree, tui_root);
        // push tool message
        continue;
    }
    // 正常工具执行...
    // 但节点挂载到 task_tree.active_parent() 下
}
```

新增回调类型：
```cpp
export using TreeUpdateCallback = std::function<void(const std::string& action, int node_id, const std::string& title)>;
```

### 3d. 系统提示更新（build_system_prompt）

在 `## CRITICAL Rules` 之后新增：

```
## Task Management

You have a `manage_tree` tool to structure your work as a task tree.

### Workflow:
1. When receiving a user request, first decompose it into subtasks using `manage_tree(add_task)`.
2. Start each subtask with `manage_tree(start_task)` before executing it.
3. Complete each subtask with `manage_tree(complete_task)` when done.
4. If a subtask needs further decomposition during execution, add child tasks under it.
5. If you discover a planned task is no longer needed, cancel it with `manage_tree(cancel_task)`.
6. If you need to modify an unstarted task, use `manage_tree(update_task)`.

### Rules:
- Every tool call automatically nests under the currently active task.
- A subtask can itself contain sub-subtasks (recursive decomposition).
- Completing a task automatically activates the next sibling or returns to parent.
- Plan changes are recorded in the tree — the user can see what was adjusted and why.

### Example flow:
User: "Install an old version of mdbook"
1. add_task(parent=0, title="Search for mdbook packages")
2. add_task(parent=0, title="Install specific version")
3. add_task(parent=0, title="Verify installation")
4. start_task(node=1) → search_packages(...)
5. complete_task(node=1, result="found v0.4.40")
6. start_task(node=2) → install_packages(...)
   — if install fails, add sub-tasks under node 2 for retry/alternative approach
7. complete_task(node=2)
8. start_task(node=3) → run_command(...)
9. complete_task(node=3)

### Response Format:
Start every reply with a one-line title summarizing your action or decision.
Then provide details on subsequent lines.
```

---

## Phase 4: CLI Agent Thread 迁移

**文件**: `src/cli.cppm`

### 4a. 新增状态

```cpp
// AgentTuiState 中新增：
agent::tui::TaskTree task_tree;
```

### 4b. 树初始化 — 用户消息为根

```diff
- tui_state.active_turn = agent::tui::TreeNode{
-     .kind = agent::tui::TreeNode::TurnRoot,
-     .text = "Turn " + std::to_string(turn_num),
- };
+ tui_state.active_turn = agent::tui::TreeNode{
+     .kind = agent::tui::TreeNode::UserTask,
+     .state = agent::tui::TreeNode::Running,
+     .title = utf8::safe_truncate(input, 80),
+     .node_id = 0,  // root = 0
+     .start_ms = now_ms,
+ };
+ task_tree = agent::tui::TaskTree{};  // reset per turn
```

### 4c. Streaming callback — title 提取

```cpp
// 提取 title: 第一行
auto nl_pos = tui_state.streaming_text.find('\n');
std::string title = (nl_pos != std::string::npos)
    ? tui_state.streaming_text.substr(0, nl_pos)
    : tui_state.streaming_text;
if (title.size() > 80) title = utf8::safe_truncate(title, 80);

// Response 节点挂载到 active_parent
auto* parent = task_tree.active_parent(*tui_state.active_turn);
// 更新或创建 Response 子节点
```

### 4d. ToolCall/ToolResult — 嵌套到活跃任务

```cpp
// on_tool_call:
auto* parent = task_tree.active_parent(*tui_state.active_turn);
parent->children.push_back({
    .kind = agent::tui::TreeNode::ToolCall,
    .state = agent::tui::TreeNode::Running,
    .title = name + "(" + args + ")",
    .action_id = id,
    .start_ms = now_ms,
});

// on_tool_result:
// 找到对应 ToolCall 节点，更新 state
```

### 4e. manage_tree 回调

```cpp
// on_tree_update callback:
screen.Post([&, action, node_id, title] {
    if (!tui_state.active_turn) return;
    auto& root = *tui_state.active_turn;

    if (action == "add_task") {
        // PlanUpdate 节点记录这个动作
        auto* active = task_tree.active_parent(root);
        active->children.push_back({
            .kind = TreeNode::PlanUpdate,
            .state = TreeNode::Done,
            .title = "plan: add \"" + title + "\"",
            .start_ms = now_ms, .end_ms = now_ms,
        });
    }
    // 其他 action 类似...
});
```

### 4f. ESC → 暂停

```diff
  if (tui_state.is_streaming || !tui_state.current_action.empty()) {
-     cancel_token.cancel();
+     cancel_token.pause();
      stream.cancel_all_prompts();
  }
```

### 4g. Ctrl+C × 3 退出

```cpp
int ctrl_c_count = 0;
int64_t last_ctrl_c_ms = 0;

// 在 CatchEvent 中：
if (event.input() == "\x03") {
    auto now = agent::tui::steady_now_ms();
    if (now - last_ctrl_c_ms > 2000) ctrl_c_count = 0;
    ++ctrl_c_count;
    last_ctrl_c_ms = now;
    if (ctrl_c_count >= 3) {
        cancel_token.cancel();
        screen.Exit();
        return true;
    }
    add_hint("Ctrl+C " + std::to_string(ctrl_c_count) + "/3");
    return true;
}
```

### 4h. PausedException 处理

```cpp
} catch (const PausedException&) {
    cancel_token.reset();
    screen.Post([&] {
        tui_state.is_streaming = false;
        tui_state.current_action = "paused";
        // 标记当前 Running 节点为 Paused（递归查找）
        // 不移入 history
        tui_state.lines.push_back({
            .type = ChatLine::Hint,
            .text = "  ⏸ paused — send new message to continue",
        });
    });
    continue;  // agent thread 继续等下一条消息
}
```

### 4i. loop.cppm PausedException 传播

在 worker thread polling loop 和 `handle_tool_call` 中，检查 `is_paused()` → `throw PausedException{}`。

### 4j. Turn 完成

```diff
- turn.done = true;
+ turn.state = TreeNode::Done;
  turn.end_ms = now_ms;
```

遍历所有 Pending 子任务，标记为 Done（如果 turn 正常结束）。

---

## 任务树可视化示例

```
> 安装老版本的 mdbook                                    12.3s
├─ ↻ plan: add "搜索 mdbook 包"
├─ ↻ plan: add "安装指定版本"
├─ ↻ plan: add "验证安装"
├─ ✓ 搜索 mdbook 包                                     1.8s
│  ├─ … thinking                                        0.3s
│  ├─ ✓ search_packages({"query":"mdbook"})              1.2s
│  │  └─ [results] 3 packages found
│  └─ ✓ 找到 mdbook v0.4.40                             0.3s
├─ ✓ 安装指定版本                                        5.2s
│  ├─ ✓ install_packages({"name":"mdbook","ver":"0.4.40"}) 4.8s
│  │  └─ ▸ downloading ████████████ 100%  2.1MB/s
│  └─ ✓ 安装完成
├─ ⊘ 验证安装（已跳过 — mdbook 安装成功无需验证）
```

**动态拆解示例**（执行中发现需要更多步骤）：
```
> 安装 nodejs 并配置环境                                  18.5s
├─ ✓ 搜索 nodejs 版本                                    2.1s
├─ ✗ 安装 nodejs v20                                     3.5s
│  ├─ ✗ install_packages(...)                            3.2s
│  ├─ ↻ plan: add "清理残留文件"                          ← 动态新增
│  ├─ ↻ plan: add "使用备选源安装"                         ← 动态新增
│  ├─ ✓ 清理残留文件                                     0.8s
│  └─ ✓ 使用备选源安装                                    5.1s
├─ ✓ 配置环境变量                                        2.3s
```

---

## 执行顺序

```
Phase 1: CancellationToken 升级          ← 最小，无依赖
  ↓
Phase 2: TreeNode 递归模型 + TaskTree    ← 类型变更 + 管理器
  ↓
Phase 3: manage_tree 工具 + 系统提示     ← loop.cppm 集成
  ↓
Phase 4: CLI Agent Thread 迁移          ← 回调/ESC/Ctrl+C/Pause
  ↓
Phase 5: 编译修复 + 测试
```

## 修改文件汇总

| 文件 | Phase | 改动 |
|------|-------|------|
| `src/runtime/cancellation.cppm` | 1 | atomic<int> state + pause/resume + PausedException |
| `src/agent/tui.cppm` | 2 | TreeNode 重设计 + TaskTree 管理器 + render 更新 |
| `src/agent/loop.cppm` | 3 | manage_tree 拦截 + 系统提示 + PausedException 传播 + TreeUpdateCallback |
| `src/cli.cppm` | 4 | 树初始化/回调嵌套/ESC 暂停/Ctrl+C×3/PausedException |

## 验证

1. **编译**: `rm -rf build && xmake build`
2. **测试**: `xmake run xlings_tests` — 全部通过
3. **手动测试**:
   - 树根显示用户消息
   - 模型自动拆解任务为 SubTask 节点
   - ToolCall 嵌套在对应 SubTask 下
   - 执行中动态新增子任务，显示 PlanUpdate 节点
   - 完成/取消任务后正确移动到下一个兄弟节点
   - ESC 暂停 → ⏸ 图标，可继续
   - Ctrl+C × 3 退出
   - 不使用 manage_tree 时兼容旧行为（扁平挂在根下）
