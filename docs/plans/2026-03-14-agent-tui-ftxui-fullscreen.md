# Agent TUI: ftxui 全屏模式重写

**日期**: 2026-03-14
**状态**: 进行中

## Context

刚完成了 tinytui 的简化（print-stream + 2行固定区域），但希望改用 ftxui 构建全屏 TUI，支持动态树展示、鼠标滚轮回滚、固定底部输入框+状态栏。tinytui 方案的限制：scrollback 由终端管理（不可编程控制滚动），没有树形展示，无鼠标支持。

## 布局设计

```
┌──────────────────────────────────────────────┐
│ ⏵ 帮我安装 vim 和 git                        │ ← 对话历史区
│ ├─ ✓ 查找软件包  1.3s                         │   （鼠标滚轮滚动）
│ │  ├─ ✓ search_packages("vim")  0.8s         │
│ │  └─ ✓ search_packages("git")  0.5s         │
│ ├─ ⟳ 安装软件包  3.2s                         │ ← 执行中，时间在跑
│ │  ├─ ✓ install_packages("vim")  3.2s        │
│ │  └─ ⟳ install_packages("git")  1.0s        │
│ └─ ○ 验证安装结果                              │ ← 未开始
│                                              │
│ ◆ 已安装 vim 和 git。                         │
│ ──────────────────────────────────            │
│                                              │
│ ⏵ 查看已安装的包                              │ ← 新一轮
│ └─ ⟳ thinking...  2.1s                       │
│                                              │
│ > user input here_                           │ ← 输入框（固定）
│ model | ctx 12k/128k | ↑ 1.2k | ↓ 850       │ ← 状态栏（固定）
│                                              │ ← 空行
└──────────────────────────────────────────────┘
```

## 树节点设计

### 动态递归树

LLM 规划 level-1 任务节点（plan），每个任务节点可进一步拆分子任务，直到拆分到可执行叶节点（tool call）。深度优先执行，后续任务依赖前序任务的执行结果决定是否/如何执行。

### 节点状态

| 状态 | 图标 | 颜色 |
|------|------|------|
| 未开始 Pending | ○ | dim |
| 执行中 Running | ⟳ | amber |
| 完成 Done | ✓ | green |
| 失败 Failed | ✗ | red |

### 时间显示

每个节点后面都显示时间：
- Running 节点：实时更新的 elapsed 时间
- Done/Failed 节点：固定的总耗时（所有子节点完成后锁定）
- Pending 节点：不显示时间

### 树形连接线（Unicode）

```
⏵ 帮我安装 vim 和 git
├─ ✓ 查找软件包  1.3s
│  ├─ ✓ search_packages("vim")  0.8s
│  └─ ✓ search_packages("git")  0.5s
├─ ⟳ 安装软件包  4.2s
│  ├─ ✓ install_packages("vim")  3.2s
│  └─ ⟳ install_packages("git")  1.0s
└─ ○ 验证安装结果
```

连接线字符：`├─`（中间子节点）、`└─`（最后子节点）、`│ `（上方同级还有兄弟）、`  `（上方同级无兄弟）

### 始终展开

所有节点始终展开显示，完整展示执行历史。用户通过滚轮回看。

## 数据模型

替换当前的扁平 `TaskEntry` + `TaskList`，恢复递归树结构（但比之前更简洁）：

```cpp
// src/agent/tui.cppm

struct TreeNode {
    static constexpr int Pending = 0;
    static constexpr int Running = 1;
    static constexpr int Done    = 2;
    static constexpr int Failed  = 3;

    int id;
    int state {Pending};
    std::string title;
    std::int64_t start_ms {0};    // 开始时间
    std::int64_t end_ms {0};      // 结束时间（子节点全完成后锁定）
    std::vector<TreeNode> children;
};

struct TurnNode {
    std::string user_message;
    TreeNode root;                // 本轮的任务树根
    std::string reply;            // 模型最终回复
    std::int64_t start_ms {0};
};

// AgentTuiState 中：
std::vector<TurnNode> turns;      // 对话历史（每轮一个 TurnNode）
TurnNode* active_turn {nullptr};  // 当前进行中的轮次
```

### ID 分配

`std::atomic<int> next_id_` 保持 lock-free 分配（agent 线程调用）。树的结构变更（add child, state change）通过 `screen.Post()` 在主线程执行。

## 架构

### 新建 `src/agent/ftxui_tui.cppm`

**AgentScreen 类**:
- `turns_`: 引用 `AgentTuiState.turns`
- `scroll_y_`, `at_bottom_`: 滚动状态
- `status_el_`: 状态栏 Element

公开方法：
- `loop()` — `screen_.Loop(main_component_)`
- `exit()` — `screen_.Exit()`
- `post(fn)` — `screen_.Post(fn)`（线程安全）
- `refresh()` — `screen_.PostEvent(Event::Custom)` 触发重绘

**render_tree_node(TreeNode, prefix, is_last) -> Element**: 递归渲染树节点
- 渲染当前节点：`prefix + connector + icon + title + time`
- 递归渲染 children，传递更新后的 prefix
- 连接线：is_last ? "└─ " : "├─ "，子节点 prefix 追加 is_last ? "   " : "│  "

**render_turn(TurnNode) -> Element**: 渲染一轮对话
- `⏵ user_message`
- render_tree_node(root, "", ...)
- 如果有 reply: 空行 + `◆ reply`
- separator

**render_status_bar(AgentTuiState) -> Element**: 状态栏
**render_streaming(text) -> Element**: 流式文本

**LineEditorAdapter**: 包装 `tinytui::LineEditor` 为 ftxui ComponentBase

### ftxui 组件树

```
ScreenInteractive::FullscreenPrimaryScreen()
  └─ CatchEvent(main_renderer, global_key_handler)
       └─ Renderer([&] {
            return vbox({
              // 历史区域（flex 占满剩余空间）
              render_all_turns() | focusPositionRelative(0, scroll_y_) | yframe | flex,
              // 输入行
              editor_component_->Render(),
              // 状态栏
              render_status_bar(tui_state_),
              // 空行
              text(""),
            });
          })
```

`render_all_turns()` 遍历 `turns_` 生成 `vbox({render_turn(t) for t in turns})`。每次重绘都重新生成 Element 树（ftxui 的设计模式——immediate mode rendering）。

### 线程模型（不变）

```
Agent Thread                    Main Thread (ftxui event loop)
────────────                    ──────────────────────────────
screen.post(fn)  ──────────→   screen_.Post() → fn() 修改 turns_/state
screen.refresh() ──────────→   screen_.PostEvent(Custom) → 重绘
msg_queue ←────────────────    Enter 键 → msg_cv.notify_all()
approval_cv ←──────────────    Y/N 键 → approval_result
cancel_token ←─────────────    Esc/Ctrl+C
```

**删除 timer_thread**: agent 线程的 streaming callback 以足够频率触发 refresh。对于 Running 节点的实时时间更新，在 Renderer 中用 `steady_now_ms() - node.start_ms` 计算（每次重绘时自动更新）。但需要一个 ~200ms 的 timer 来驱动时间刷新：

```cpp
// 在 loop() 前启动一个简单的刷新线程
std::jthread timer([&](std::stop_token st) {
    while (!st.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        screen_.PostEvent(Event::Custom);
    }
});
```

### 使用 FullscreenPrimaryScreen

退出后对话历史保留在终端 scrollback 中。`AlternateScreen` 退出后清屏，不利于回看。

### 鼠标滚轮

```cpp
screen_.TrackMouse(true);

// CatchEvent 中：
if (event.is_mouse()) {
    auto& m = event.mouse();
    if (m.button == Mouse::WheelUp) {
        scroll_y_ = std::max(0.0f, scroll_y_ - 0.05f);
        at_bottom_ = false;
        return true;
    }
    if (m.button == Mouse::WheelDown) {
        scroll_y_ = std::min(1.0f, scroll_y_ + 0.05f);
        if (scroll_y_ >= 0.99f) at_bottom_ = true;
        return true;
    }
}
```

新内容到达时如果 `at_bottom_`，自动保持 `scroll_y_ = 1.0`。

## 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/agent/ftxui_tui.cppm` | **新建** | AgentScreen, LineEditorAdapter, render_tree_node, render_turn, render_status_bar |
| `src/agent/tui.cppm` | **修改** | 替换扁平 TaskEntry/TaskList 为递归 TreeNode/TurnNode；删除 format_* 和 icons；保留 ThinkFilter/ChatLine/time helpers |
| `src/agent/agent.cppm` | **修改** | 添加 `export import xlings.agent.ftxui_tui` |
| `src/agent/loop.cppm` | **修改** | TurnConfig 使用新 TreeNode*，handle_manage_tree 操作递归树 |
| `src/cli.cppm` | **修改** | 替换 tinytui::Screen 为 AgentScreen，回调操作 TurnNode/TreeNode |
| `src/libs/tinytui.cppm` | **保留** | 不修改，LineEditor 仍被 ftxui_tui 使用 |

### ftxui_tui.cppm 模块结构

```cpp
module;
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

export module xlings.agent.ftxui_tui;

import std;
import xlings.agent.tui;
import xlings.agent.token_tracker;
import xlings.core.utf8;
import xlings.ui;              // theme::* 复用色彩体系
import xlings.libs.tinytui;   // LineEditor
```

### cli.cppm 改造要点

**回调不再 print_line，改为操作树结构**:
```cpp
// tool_call callback:
agent_screen.post([&] {
    // 找到 active task node，添加 ToolCall 子节点
    auto& node = find_active_task(active_turn->root);
    node.children.push_back({.id=id, .state=Running, .title=name+"("+args+")", .start_ms=now});
});
agent_screen.refresh();

// tool_result callback:
agent_screen.post([&] {
    auto* tool = find_node_by_id(active_turn->root, id);
    tool->state = is_error ? Failed : Done;
    tool->end_ms = now;
});
agent_screen.refresh();

// tree_update callback (manage_tree):
agent_screen.post([&] {
    if (action == "add_task") {
        auto* parent = find_node_by_id(active_turn->root, parent_id);
        parent->children.push_back({.id=node_id, .title=title, .state=Pending});
    } else if (action == "complete_task") {
        auto* node = find_node_by_id(active_turn->root, node_id);
        node->state = Done;
        node->end_ms = now;
    }
    // ...
});
```

**Key handler 迁移到 AgentScreen CatchEvent**:
- `Event::Return` → Enter (submit message)
- `Event::Escape` → pause
- `Event::ArrowUp/Down` → history
- `Event::Tab` → completion select
- `event.is_mouse()` → 滚轮
- Ctrl+C → cancel (注意 ftxui 默认 Ctrl+C 行为需拦截)

### loop.cppm 改造

TurnConfig 中 `TaskList*` 改回 `TreeNode*`（递归树根），`handle_manage_tree` 操作递归结构（add_task 创建子节点，start/complete/cancel 修改节点状态）。

## GCC 15 注意事项

- ftxui headers 必须在 `module;` global module fragment 中 `#include`（同 selector.cppm 模式）
- 不在模块接口导出 ftxui 类型 — AgentScreen 内部持有
- TreeNode 使用 `static constexpr int` 不用 enum class（保持现有模式）
- render_* 返回 `ftxui::Element`（shared_ptr 内部），跨模块传递安全
- 避免在导出的 lambda 中按值捕获含 vector 的结构体

## 实现步骤

### Phase 1: 数据模型 — 修改 tui.cppm
1. 替换 TaskEntry/TaskList 为 TreeNode + TurnNode
2. 保留 AgentTuiState（turns 字段替换 task_list）
3. 删除 format_* 函数和 icons 命名空间
4. 保留 ThinkFilter, ChatLine, time helpers, print_error/print_hint
5. 添加 find_node_by_id 辅助函数

### Phase 2: 创建 ftxui_tui.cppm
1. 新建文件，global module fragment + ftxui includes
2. 实现 render_tree_node（递归 + Unicode 连接线 + 状态图标 + 时间）
3. 实现 render_turn, render_status_bar, render_streaming
4. 实现 LineEditorAdapter（ftxui Event → tinytui KeyEvent）
5. 实现 AgentScreen（loop, exit, post, refresh, 组件树构建）
6. 实现鼠标滚轮处理

### Phase 3: 修改 loop.cppm
1. TurnConfig 中 TaskList* → TreeNode*（树根）
2. handle_manage_tree 操作递归 TreeNode
3. 更新 run_one_turn 便捷重载

### Phase 4: 改造 cli.cppm
1. 替换 tinytui::Screen 初始化为 AgentScreen
2. 迁移 key handler 到 CatchEvent
3. 替换所有回调（操作树结构而非 print_line）
4. 替换 timer_thread 为 200ms refresh timer
5. 迁移 session resume 的 print_conversation_history
6. 更新 agent.cppm re-export

### Phase 5: 编译测试
1. `rm -rf build && xmake build`
2. `xmake build xlings_tests && ./build/.../xlings_tests`
3. 手动测试全屏布局、树展示、滚轮、输入、审批、暂停/取消

## 验证

1. `rm -rf build && xmake build` — 编译通过
2. 209 tests pass
3. 手动测试：
   - 全屏布局正确（历史区 + 输入框 + 状态栏 + 空行）
   - 树形动态更新（plan → 子任务 → tool call → 完成）
   - Unicode 连接线正确渲染（├─ └─ │）
   - 节点时间实时更新，完成后固定
   - 鼠标滚轮上下滚动历史
   - 新内容到达自动滚到底部
   - agent 运行时可输入新消息
   - 审批 Y/N、Esc 暂停、Ctrl+C 取消
   - 终端调整大小自适应

## 关键参考文件

- `src/ui/selector.cppm` — ftxui 模块集成模式
- `src/ui/theme.cppm` — 复用色彩体系（cyan/green/amber/red/dim）
- `src/libs/tinytui.cppm:LineEditor` — 被 LineEditorAdapter 包装
- `src/agent/loop.cppm:handle_tree_op` — manage_tree 处理逻辑（需改为递归树版本）
- `src/cli.cppm:1044-1799` — agent mode 回调全部改造
