# Simplified Agent TUI Design

## Problem

The current agent TUI uses a FrameBuffer + diff rendering system with `reserve_screen_height`, `cursor_up/cursor_down`, and multi-line active area management. This causes a persistent root node duplication bug in terminal scrollback and adds ~600 lines of rendering complexity. The fundamental issue: managing a variable-height active area with terminal scrolling is inherently fragile.

## Solution

Replace the entire FrameBuffer/diff rendering pipeline with a **print-stream + 2-line fixed area** model:

- **Scrollback**: All tree events, tool calls, and replies print sequentially to stdout (like a log stream)
- **Fixed 2-line area**: Spinner line + input line at the bottom, always visible
- **No variable-height active area**: Eliminates the root cause of the duplication bug

## Architecture

### New Screen Loop

```
loop:
  enable_raw_mode()
  while !quit_:
    1. drain post queue (lambdas may call print_line → stdout)
    2. if dirty_: refresh spinner line + input line (fixed 2 lines)
    3. poll stdin (42ms) → key_handler_
  restore_mode()
```

### Screen Members

**Keep:**
- `mtx_`, `queue_` — thread-safe lambda queue
- `quit_`, `dirty_` — atomic flags
- `key_handler_` — key event dispatch
- raw mode (termios)

**Add:**
- `spinner_text_` — string, current spinner content
- `spinner_visible_` — bool

**Delete:**
- `renderer_`, `frame_buf_`, `prev_frame_`, `prev_width_`, `prev_cursor_line_`, `max_active_lines_`
- `reserve_screen_height()`, `flush_to_scrollback()`, `set_renderer()`

### print_line Method

All stdout output goes through `screen.print_line(text)`, which interleaves with the fixed 2-line area:

```
1. cursor_up(1)          // move to spinner line
2. \r\033[2K             // clear spinner
3. print text + \n       // text enters scrollback, cursor at old spinner position
4. write spinner_text_   // redraw spinner
5. \n                    // move to input line position
6. \r\033[2K + redraw input line
7. flush
```

This is deterministic — always exactly 2 fixed lines, no variable height, no frame diffing.

### Fixed 2-Line Area

```
⟳ executing list_packages...       ← spinner line (agent running)
> user input here_                  ← input line (always visible)
```

**State transitions:**
- idle: spinner line shows status bar (model + tokens), input line shows `> `
- thinking/tool_exec/streaming: spinner shows current action, input still active
- user submits while agent runs: message queued, printed to scrollback above spinner

**Refresh**: `cursor_up(1)` + `\r\033[2K` for spinner, `cursor_down(1)` + `\r\033[2K` for input. Only 2 lines, always.

### User Input During Agent Execution

Users can type and submit messages while the agent is running. Submitted messages:
1. Enter a pending message queue
2. Print to scrollback with `📎` prefix (via print_line)
3. Input line clears for next input

```
  ✓ list_packages  1.2s
📎 顺便也查一下 vim                  ← queued message in scrollback
⟳ executing remove_package...       ← spinner continues
> _                                  ← input cleared
```

## Print Protocol

Tree events print to scrollback as they happen:

| Event | Print | Example |
|-------|-------|---------|
| manage_tree: add_task | `  ○ title` | `  ○ 查找已安装的包` |
| manage_tree: cancel_task | `  ✗ title (cancelled)` | |
| on_tool_call | `    ⟳ name(args)` | `    ⟳ list_packages({})` |
| on_tool_result (success) | `    ✓ name  duration` | `    ✓ list_packages  1.2s` |
| on_tool_result (error) | `    ✗ name  error` | |
| data_event (detail) | `    [kind] summary` | `    [styled_list] Installed` |
| turn start | `⏵ user message` | |
| turn end (reply) | `◆ reply text` | `◆ 已卸载 d2x 和 mdbook。` |
| turn end | separator line | `─────────────` |

Indentation is simple (2-space per level), no recursive tree connector building.

## Thread Safety

**Rule**: All stdout writes happen on the main thread only.

```
Agent thread                        Main thread (Screen.loop)
────────────                        ────────────────────────
on_tool_call → screen.post(λ)   →  drain queue → λ calls print_line()
handle_manage_tree → screen.post()  (NO direct tree mutation on agent thread)
data_listener → screen.post(λ)  →  drain queue → λ calls print_line()
```

**Key change**: `handle_manage_tree` no longer mutates tree state directly on the agent thread. All mutations go through `screen.post()`. This eliminates the data race that caused the original children.back() bugs.

## Deletion Scope

### tinytui.cppm (~300 lines removed)
- `FrameBuffer` class (~73 lines)
- `reserve_screen_height()`, `flush_to_scrollback()`
- `set_renderer()` / `renderer_` callback
- `prev_frame_`, `prev_width_`, `prev_cursor_line_`, `max_active_lines_`
- Diff rendering logic in loop() (~80 lines)

### agent/tui.cppm (~260 lines removed)
- `render_active_area()` (~43 lines)
- `print_tree_node()` recursive rendering (~130 lines)
- `TaskTree` class (~90 lines)
- `TreeNode` complex fields (children, find_child, find_last_running_tool)

### cli.cppm (~200 lines removed)
- `screen.set_renderer()` callback (~100 lines)
- `tui_state.task_tree` operations
- Complex tree node management in on_tool_call/on_stream_chunk/on_tool_result

### Kept
- `Screen`: post(), refresh(), loop() (simplified), raw mode, key handler
- `LineEditor`: complete
- `read_key()`, cursor helpers, ANSI constants
- `print_chat_line()` for reply formatting
- `TreeNode` slimmed to pure data (kind, title, duration) for print formatting

**Net change**: ~760 lines removed, ~60-80 lines added.
