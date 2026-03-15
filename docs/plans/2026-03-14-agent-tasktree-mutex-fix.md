# TaskTree：mutex 保护的同步树变异组件

## 问题

树节点动态更新存在"节点偏移"bug：response/tool call 节点落入错误的父任务下。

### 根因

所有树变异通过 `agent_screen->post()` 排队到主线程异步执行。回调用 `[&, ...]` 捕获引用，lambda 在执行时才读 `active_parent_id`——此时其他 lambda 可能已经改了这个值。

```
Worker 线程：
  on_tool_call("manage_tree") → post(lambda_A)
  handle_manage_tree():
    on_tree_update("start_task") → post(lambda_B: set active_parent_id=T1)
  on_tool_call("list_packages") → post(lambda_C: read active_parent_id)

主线程 queue drain:
  lambda_A: manage_tree 返回
  lambda_B: active_parent_id = T1
  lambda_C: 读到 T1 ✓  ← 这次碰巧对了

但如果 lambda 顺序不同（取决于 LLM response 结构），就会读到错误的值。
```

核心问题：**变异走 post() 队列，shared state 在不同 lambda 间隐式竞争**。

## 方案：TaskTree 同步变异 + mutex

### 设计思路

Worker 线程直接调用 TaskTree 方法（同步、mutex 保护），主线程通过 `snapshot()` 获取只读副本用于渲染。不再通过 `post()` 队列变异树状态。

### 数据（mutex 保护）

```cpp
TreeNode root_;
int active_parent_id_ {0};
std::string streaming_text_;
```

### 接口

```cpp
export class TaskTree {
    mutable std::mutex mtx_;
    TreeNode root_;
    int active_parent_id_ {0};
    std::string streaming_text_;

public:
    // ── manage_tree 操作（worker 线程调用）──
    void add_task(int id, int parent_id, const std::string& title);
    void start_task(int id, std::int64_t now_ms);
    void complete_task(int id, std::int64_t now_ms);
    void cancel_task(int id, std::int64_t now_ms);
    void update_task(int id, const std::string& title);

    // ── streaming / response（worker 线程调用）──
    void append_streaming(std::string_view text);
    void flush_as_response(int term_width);
    void clear_streaming();

    // ── tool call 生命周期（worker 线程调用）──
    void add_tool_call(int id, const std::string& title, std::int64_t start_ms);
    void complete_tool_call(int id, bool is_error, std::int64_t end_ms);

    // ── 收尾 ──
    void finalize(std::int64_t end_ms);
    void reset();

    // ── 渲染（主线程调用）──
    auto snapshot() const -> TreeNode;
    bool has_streaming() const;
    auto streaming_text() const -> std::string;
};
```

### cli.cppm 回调改造

**之前（散落逻辑，通过 post 队列）：**
```cpp
agent_screen->post([&, id, n, a, call_start] {
    if (!tui_state.streaming_text.empty()) { /* 15 行 flush 逻辑 */ }
    auto* parent = find_or_activate_parent(...);
    parent->children.push_back({...});
});
```

**之后（直接调用，无 post）：**
```cpp
// on_tool_call (worker 线程)
task_tree.flush_as_response(term_width);
if (name != "manage_tree") {
    task_tree.add_tool_call(id, title, start_ms);
}

// on_tree_update (worker 线程)
task_tree.start_task(node_id, now);

// 渲染 (主线程)
auto tree = task_tree.snapshot();
```

`agent_screen->post()` 只用于更新非树字段（`current_action`、`is_streaming` 等）。

### 线程安全模型

| 操作 | 线程 | 同步方式 |
|------|------|----------|
| add/start/complete task | worker | mutex lock |
| flush/add_tool_call | worker | mutex lock |
| append_streaming | worker（stream callback） | mutex lock |
| snapshot() | main（渲染） | mutex lock + deep copy |

### 文件变更

| 文件 | 变更 |
|------|------|
| `src/agent/tui.cppm` | 新增 `TaskTree` class；`AgentTuiState` 移除 `streaming_text`、`active_parent_id`；保留 `TreeNode`、`TurnNode` 等纯数据结构；删除 `get_active_parent`、`find_parent_id` |
| `src/cli.cppm` | 回调改为直接调 `task_tree.method()`；渲染用 `task_tree.snapshot()`；`post()` 只更新非树状态 |

### 验证

1. `rm -rf build && xmake build` 编译通过
2. 209 tests pass
3. 手动测试多任务场景节点归属正确
