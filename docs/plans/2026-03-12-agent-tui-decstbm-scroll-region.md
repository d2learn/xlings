# Agent TUI: DECSTBM Scroll Region 渲染隔离

## 背景

Agent TUI 的活动区域（tree + input + status）在执行期间反复渲染。两个问题：
1. **Root 节点泄漏**：grow 预滚动用 `\n` 创建空间时，会把活动区顶部（root 节点）推入 scrollback
2. **无法翻看**：每帧渲染 stdout 输出会把 viewport 拉回底部，用户无法在执行期间向上翻看已提交内容

## 方案：DECSTBM Scroll Region

用 ANSI DECSTBM (`\033[T;Br`) 将 viewport 分为两个区域：

```
┌─────────────────────────┐ ← viewport top
│  Previous turn output   │
│  ✓ 安装 nodejs 24       │ ← 冻结区：不受渲染影响
│  Reply: 已完成安装...    │    用户可以在 ESC 暂停后 scroll up 查看
│                         │
├═════════════════════════┤ ← region_top_ (scroll region 上界)
│  ⏵ 卸载所有的 mdbook    │
│    ├─ ✓ SearchPackages  │ ← 活动区：scroll region 内
│    └─ ◐ RemovePackage   │    所有渲染限制在此区域
│  > _                    │    \n 只在 region 内滚动
│  ──── thinking... 1.2s  │    溢出行被丢弃（不进 scrollback）
└─────────────────────────┘ ← term_h (scroll region 下界)
```

### ANSI 关键行为

| 操作 | 代码 | 行为 |
|------|------|------|
| 设置 scroll region | `\033[T;Br` | T 到 B 行为 scroll region（1-indexed） |
| 重置 scroll region | `\033[r` | 恢复全屏 scroll region |
| `\n` 在 region 底部 | — | **只在 region 内滚动**（顶行丢弃，不进 scrollback） |
| `cursor_up/down` | `\033[A/B` | **不受 region 限制**，可跨区域移动 |
| 查询光标位置 | `\033[6n` | 终端回复 `\033[row;colR` |

### 渲染流程

```
flush_to_scrollback:
  reset_scroll_region → clear active area → print committed → reset state

Initial frame (old_count=0, no region):
  \n × new_count → query_cursor_position → set_scroll_region(active_top, term_h)

Subsequent diff frames (within region):
  cursor_up to top → cursor_down per line → safe

Growth frame (within region):
  cursor_up to top → cursor_down for old lines → \n for new lines →
  \n scrolls WITHIN region (top tree lines discarded, not to scrollback)

Turn ends:
  flush_to_scrollback → reset region → print tree → next initial frame
```

### Bug 修复：消除 Grow 预滚动

删除独立的 grow 预滚动块，将增长集成到 full_repaint：

- `i < old_count-1`：cursor_down（在旧帧内，不滚动）
- `i >= old_count-1`：`\n`（在 scroll region 内滚动，不污染 scrollback）

### 用户翻看支持

- 冻结区完全稳定（渲染不影响 region 上方内容）
- ESC 暂停后渲染停止 → 用户可自由 Shift+PgUp 翻看历史
- 恢复执行后 viewport 回到活动区

### 跨平台

- Linux/macOS: DECSTBM + `\033[6n` 在 xterm, VTE, kitty, iTerm2, alacritty 均支持
- Windows: Windows Terminal 支持（需 VT processing 已启用）
- Fallback: `\033[6n` 无响应时假设活动区在 viewport 底部

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/libs/tinytui.cppm` | ANSI helpers, Screen members, 渲染逻辑, flush_to_scrollback, loop cleanup |

## 验证

1. Root 节点在 scrollback 中只出现一次（turn 结束 flush 时）
2. 冻结区内容在执行期间不闪烁不移动
3. ESC 暂停后可自由翻滚
4. 小终端 tree 在 region 内滚出，input/status 可见
5. Ctrl+C 退出后终端状态正常（scroll region 已重置）
