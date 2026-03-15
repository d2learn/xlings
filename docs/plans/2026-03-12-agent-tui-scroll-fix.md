# 修复 Agent TUI: 历史滚动混乱 + 自动压缩刷屏

## 背景

实际使用中发现两个 TUI 严重 bug：
1. **历史滚动混乱**：所有内容（历史 + 活动树）都在 ftxui Renderer 里以 24fps 重绘。`focusPositionRelative(0,1)` 每帧强制滚到底部。当帧高度超过终端高度时，每次重绘都把重复内容推入终端滚动缓冲区，向上滚动看到的是破碎/重复的历史内容。
2. **自动压缩刷屏**：`maybe_auto_compact()` 即使没有实际回收任何内容（0 turns, 0 tokens），也会触发回调显示消息。

## 调研结论

### ftxui v6.1.9 限制
- `Screen::Print()` **无参数** — 只输出自身 buffer，不接受字符串参数
- `WithRestoredIO(fn)` 只做 `Uninstall(); fn(); Install()` — 不管光标位置
- `Draw()` 使用 `ResetCursorPosition()` + `ResetPosition(clear=resized)` 配合 **私有** 光标偏移量
- **无法在事件循环中安全注入文本到终端滚动区** — 在帧间写 stdout 会破坏 ftxui 的相对光标追踪

### Codex CLI 的方案（参考，不采用）
- 使用 ratatui + crossterm，有自定义 Terminal 类
- 用 ANSI scroll regions (DECSTBM `\033[{top};{bottom}r`) + Reverse Index (ESC M) 在视口上方插入历史内容
- 核心代码在 `codex-rs/tui/src/insert_history.rs`
- 这套方案依赖底层终端控制权，与 ftxui 不兼容（ftxui 管理自己的光标偏移和帧生命周期）

### Claude Code 的方案（参考，采用核心思路）
- 基于 React 组件模型 + 流式渲染（Bun 运行时 + React Server Components）
- **核心设计**：每次渲染只输出当前状态的完整帧，不在 TUI 帧内累积历史
- 状态变更触发 re-render（React hooks: useState），输出通过 buffered writers 流式写入 stdout
- 历史消息保留在内存中管理，但渲染帧只包含当前活动内容
- 输入通过 async event handlers 处理，与渲染帧解耦

### 最终方案：参考 Claude Code 的流式渲染思路
- **帧内只保留活动区域**（类似 Claude Code 的 "只渲染当前状态"）
- 不写终端滚动缓冲区（不用 scroll regions，不用 WithRestoredIO 写 stdout）
- 渲染器只管理：turn 期间显示 tree + response，turn 之间显示 last_response
- 帧始终小且稳定 → 无重复帧推入滚动缓冲区 → 滚动混乱消除
- hints/errors 用 flash 消息（带超时自动清除，类似 toast notification）
- 状态变更通过 `screen.Post()` + `PostEvent(Custom)` 触发重渲染（对应 Claude Code 的 setState → re-render）

## 修改步骤

### Step 1: AgentTuiState 新增字段 (`src/agent/tui.cppm`)

在 `AgentTuiState` 中添加：
```cpp
std::string last_response;       // turn 间持续显示的上次回复
std::string flash_text;          // 临时消息（hint/error）
int64_t flash_until_ms {0};      // 超时后自动清除
```

### Step 2: 修复 `add_hint` / `add_error` (`src/cli.cppm`)

替换 `screen.Print(ansi_hint/ansi_error)` 为设置 flash_text：
```cpp
auto add_hint = [&](std::string text) {
    screen.Post([&, t = std::move(text)] {
        tui_state.flash_text = std::move(t);
        tui_state.flash_until_ms = agent::tui::steady_now_ms() + 5000;
    });
    screen.PostEvent(ftxui::Event::Custom);
};
auto add_error = [&](std::string text) {
    screen.Post([&, t = std::move(text)] {
        tui_state.flash_text = "✗ " + std::move(t);
        tui_state.flash_until_ms = agent::tui::steady_now_ms() + 8000;
    });
    screen.PostEvent(ftxui::Event::Custom);
};
```

### Step 3: 简化 `populate_tui_from_conversation` (`src/cli.cppm`)

只提取最后一条 assistant 消息设置 `tui_state.last_response`（不再遍历所有消息调用 screen.Print）。

### Step 4: 修复 Ctrl+C hint (`src/cli.cppm`)

用 `tui_state.flash_text` 替代 `screen.Print(ansi_hint(...))`.

### Step 5: 修复用户消息发送 (`src/cli.cppm`)

移除 `screen.Print(ansi_user_msg(input))`，用户在输入框已看到自己的输入。

### Step 6: 修复异常处理器 (`src/cli.cppm`)

- **PausedException**: `tui_state.flash_text = "⏸ paused — send new message to continue"`
- **CancelledException**: 标记 active_turn 为 Failed，reset，设 flash_text
- **Generic exception**: 同上 + 错误信息

### Step 7: 修复 Turn 完成 (`src/cli.cppm`)

```cpp
tui_state.last_response = tr.reply;
tui_state.active_turn.reset();
```

### Step 8: 更新渲染器显示 last_response 和 flash (`src/cli.cppm`)

渲染器布局：
```
[last_response — 仅当没有 active_turn 时]    ← ◆ text...（高度受限，可滚动）
[active tree — 仅 turn 期间]                 ← 树节点
[--- 虚线分隔符 ---]                          ← 仅当有 response 时
[response 区域]                               ← ◆ text...（实时流式）
[flash 消息]                                  ← 如果 flash_until_ms > now
[--- 分隔符 ---]
[> 输入框]
[--- 分隔符 ---]
[补全弹窗]
[状态栏]
```

### Step 9: `/clear` 命令

```cpp
tui_state.last_response.clear();
tui_state.flash_text.clear();
```

## 修复 2：自动压缩刷屏抑制（已完成）

**文件**: `src/agent/loop.cppm`

```diff
- if (on_auto_compact) on_auto_compact(evicted, freed);
+ if (on_auto_compact && (evicted > 0 || freed > 0)) on_auto_compact(evicted, freed);
```

## 需修改的文件

| 文件 | 修改内容 |
|------|----------|
| `src/agent/tui.cppm` | 在 `AgentTuiState` 中添加 `last_response`、`flash_text`、`flash_until_ms` |
| `src/cli.cppm` | 替换所有 `screen.Print()` 为 flash/last_response 方案，更新渲染器布局 |
| `src/agent/loop.cppm` | 自动压缩：抑制 0/0 回调（已完成） |

## 验证

1. `rm -rf build && xmake build` — 编译通过
2. `xmake run xlings_tests` — 测试通过
3. 手动测试：
   - 运行多工具调用 turn → turn 期间树实时渲染
   - turn 完成后：last_response 可见，树消失，滚动缓冲区无重复内容
   - 终端向上滚动 → 无破碎/重复内容
   - hints/errors 显示 flash 消息，超时后自动消失
   - 无 `auto-compact: evicted 0` 刷屏
   - ESC 暂停 / Ctrl+C 仍正常工作
   - `/clear` 清除显示
   - 会话恢复显示上次 assistant 消息
