# Agent TUI ftxui 重构设计方案

## 核心变更

1. **用 ftxui `ScreenInteractive::Fullscreen()`** 替代所有 `std::print` + ANSI 转义码
2. **状态栏放在输入框下方**
3. **输入框始终可见** — ftxui 自动管理终端渲染，不再有 stdout 竞争
4. **复用 `ui::theme` 色彩系统** — 不重复定义颜色

## 终端布局

```
┌────────────────────────────────────────────────────────┐
│ ◆ 我来帮你安装 node...                                  │
│   ⚡ search_packages ("node")                ↑42 ↓156  │
│   ✓ search_packages done                               │
│   ⚡ install_package ("node")                ↑38 ↓92   │
│   ▸ ████████████████░░░░ 78%  node  3.2MB/s  ETA 5s   │
│   ✓ install_package done                               │
│ 找到并安装了 node v22.1.0                                │
│                                         ← flex 占满剩余 │
├────────────────────────────────────────────────────────┤
│ > user input here_                                     │
│ qwen3-235b | ctx: 12.4k / 128k | ↑ 8.2k ↓ 3.1k       │
└────────────────────────────────────────────────────────┘
```

- **输出区**: `vbox(chat_elements) | flex | vscroll_indicator | frame` — 自动滚到底部
- **输入行**: ftxui `Input` 组件 + `> ` 前缀
- **状态栏**: 输入行下方，dim 灰色

## 架构: ftxui 接管终端

```
ScreenInteractive::Fullscreen()
│
├── Renderer (main layout)
│   ├── output_area (flex, scrollable)
│   │   └── vbox(chat_elements_)  ← shared state, mutex protected
│   ├── separator
│   ├── Input component (> prompt)
│   └── status_bar text
│
├── CatchEvent
│   ├── Enter → enqueue message, clear input
│   ├── ↑↓   → history navigation (when not in completion)
│   ├── Tab   → accept completion
│   └── Esc   → cancel completion
│
└── Cross-thread communication
    ├── LLM thread → screen.Post([&]{ append chunk }) → auto re-render
    ├── Tool thread → screen.Post([&]{ update progress }) → auto re-render
    └── No more stdout race condition!
```

### 为什么 ftxui 解决了 stdout 竞争

当前问题: InputManager 线程、主线程、async LLM 线程三方都直接 `std::print` 到 stdout。

ftxui 方案: **所有可视状态只通过 `screen.Post()` 更新共享数据结构**，ftxui 在单一渲染循环中统一绘制。没有任何线程直接写 stdout。

## 共享状态设计

```cpp
// src/agent/tui.cppm 中的核心状态

// 聊天输出的一行
struct ChatLine {
    enum Type { UserMsg, AssistantText, ToolCall, ToolResult, Progress, Separator, Hint, Error };
    Type type;
    std::string text;
    // ToolCall/ToolResult 附加数据
    int action_id {-1};
    int input_tokens {0};
    int output_tokens {0};
    bool is_error {false};
    // Progress 附加数据
    float progress {0.0f};
    std::string speed;
    std::string eta;
};

struct AgentTuiState {
    std::mutex mtx;

    // 输出区
    std::vector<ChatLine> lines;
    std::string streaming_text;    // 当前正在流式输出的助手文本
    bool is_streaming {false};
    bool is_thinking {false};

    // 状态栏
    std::string model_name;
    int ctx_used {0};
    int ctx_limit {0};
    int session_input {0};
    int session_output {0};
    int l2_cache_count {0};

    // 补全
    std::vector<std::pair<std::string, std::string>> completions;
    int completion_selected {-1};

    // 历史
    std::deque<std::string> history;
    int history_pos {-1};  // -1 = not browsing history
    std::string saved_input;  // 保存进入历史模式前的输入
};
```

## 渲染函数

```cpp
// 将 ChatLine 转为 ftxui Element
auto render_chat_line(const ChatLine& line) -> ftxui::Element {
    using namespace ftxui;
    switch (line.type) {
        case ChatLine::UserMsg:
            return hbox({ text("> ") | bold | color(theme::cyan()),
                          paragraph(line.text) | color(theme::text_color()) });

        case ChatLine::AssistantText:
            return hbox({ text("◆ ") | color(theme::magenta()),
                          paragraph(line.text) });

        case ChatLine::ToolCall: {
            auto el = hbox({
                text("  ⚡ " + line.text) | color(theme::amber()),
            });
            if (line.input_tokens > 0 || line.output_tokens > 0) {
                el = hbox({ el,
                    text("  ↑" + format_tokens(line.input_tokens) +
                         " ↓" + format_tokens(line.output_tokens))
                    | color(theme::dim_color()) });
            }
            return el;
        }

        case ChatLine::ToolResult:
            if (line.is_error)
                return text("  ✗ " + line.text + " failed") | color(theme::red());
            return text("  ✓ " + line.text + " done") | color(theme::green());

        case ChatLine::Progress:
            return hbox({
                text("  ▸ ") | color(theme::cyan()),
                gauge(line.progress) | size(WIDTH, EQUAL, 24) | color(theme::cyan()),
                text(" " + std::to_string(int(line.progress * 100)) + "%") | bold,
                text("  " + line.text) | color(theme::magenta()),
                text("  " + line.speed) | color(theme::cyan()),
                text("  " + line.eta) | color(theme::dim_color()),
            });

        case ChatLine::Separator:
            return separator() | color(theme::border_color());

        case ChatLine::Hint:
            return text(line.text) | color(theme::dim_color());

        case ChatLine::Error:
            return text("✗ " + line.text) | color(theme::red());
    }
}

// 状态栏
auto render_status_bar(const AgentTuiState& st) -> ftxui::Element {
    using namespace ftxui;
    auto fmt = [](int t) { return TokenTracker::format_tokens(t); };
    std::string status = " " + st.model_name +
        " | ctx: " + fmt(st.ctx_used) + " / " + fmt(st.ctx_limit) +
        " | ↑ " + fmt(st.session_input) + " ↓ " + fmt(st.session_output);
    if (st.l2_cache_count > 0) {
        status += " | cache: " + std::to_string(st.l2_cache_count) + "t";
    }
    return text(status) | color(theme::dim_color());
}
```

## 主组件构建

```cpp
// src/agent/tui.cppm — 核心 run 函数

export auto run_agent_tui(AgentTuiState& state,
                          std::function<void(std::string)> on_submit)
    -> ftxui::Component
{
    using namespace ftxui;

    std::string input_content;
    auto input_opt = InputOption::Default();
    input_opt.multiline = false;
    auto input = Input(&input_content, "type message or / for commands", input_opt);

    auto component = CatchEvent(input, [&](Event event) {
        // Enter: submit message
        if (event == Event::Return) {
            if (!input_content.empty()) {
                auto msg = input_content;
                input_content.clear();
                // Reset history browsing
                state.history_pos = -1;
                state.saved_input.clear();
                on_submit(std::move(msg));
            }
            return true;
        }
        // ↑: history up
        if (event == Event::ArrowUp) {
            std::lock_guard lk(state.mtx);
            if (!state.history.empty()) {
                if (state.history_pos < 0) {
                    state.saved_input = input_content;
                    state.history_pos = state.history.size() - 1;
                } else if (state.history_pos > 0) {
                    --state.history_pos;
                }
                input_content = state.history[state.history_pos];
            }
            return true;
        }
        // ↓: history down
        if (event == Event::ArrowDown) {
            std::lock_guard lk(state.mtx);
            if (state.history_pos >= 0) {
                ++state.history_pos;
                if (state.history_pos >= (int)state.history.size()) {
                    state.history_pos = -1;
                    input_content = state.saved_input;
                } else {
                    input_content = state.history[state.history_pos];
                }
            }
            return true;
        }
        // Tab: accept completion
        if (event == Event::Tab) {
            std::lock_guard lk(state.mtx);
            if (state.completion_selected >= 0 &&
                state.completion_selected < (int)state.completions.size()) {
                input_content = state.completions[state.completion_selected].first + " ";
                state.completions.clear();
                state.completion_selected = -1;
            }
            return true;
        }
        return false;
    });

    return Renderer(component, [&, component] {
        std::lock_guard lk(state.mtx);

        // Build output elements
        Elements output_elems;
        for (auto& line : state.lines) {
            output_elems.push_back(render_chat_line(line));
        }
        // Streaming text (currently being received)
        if (state.is_streaming) {
            if (state.is_thinking) {
                output_elems.push_back(
                    text("  thinking...") | color(theme::dim_color()) | blink);
            } else if (!state.streaming_text.empty()) {
                output_elems.push_back(hbox({
                    text("◆ ") | color(theme::magenta()),
                    paragraph(state.streaming_text),
                }));
            }
        }

        auto output_area = vbox(std::move(output_elems))
            | vscroll_indicator | focusPositionRelative(0, 1.0)  // auto scroll to bottom
            | frame | flex;

        // Input line
        auto input_line = hbox({
            text("> ") | bold | color(theme::cyan()),
            component->Render() | flex,
        });

        // Status bar
        auto status = render_status_bar(state);

        // Completion popup (if active)
        Elements main_elems = {
            output_area,
            separator() | color(theme::border_color()),
            input_line,
            status,
        };

        // Show completions between input and status
        if (!state.completions.empty()) {
            Elements comp_elems;
            int max_show = std::min((int)state.completions.size(), 8);
            for (int i = 0; i < max_show; ++i) {
                auto& [name, desc] = state.completions[i];
                if (i == state.completion_selected) {
                    comp_elems.push_back(hbox({
                        text("  › " + name) | bold | color(theme::cyan()),
                        text("  " + desc) | color(theme::dim_color()),
                    }));
                } else {
                    comp_elems.push_back(hbox({
                        text("    " + name) | color(theme::dim_color()),
                        text("  " + desc) | color(theme::border_color()),
                    }));
                }
            }
            // Insert completions after input, before status
            main_elems = {
                output_area,
                separator() | color(theme::border_color()),
                input_line,
                vbox(std::move(comp_elems)),
                status,
            };
        }

        return vbox(std::move(main_elems));
    });
}
```

## CLI 集成 (src/cli.cppm)

```cpp
// Agent REPL — 用 ftxui 驱动

auto screen = ftxui::ScreenInteractive::Fullscreen();
agent::AgentTuiState tui_state;
tui_state.model_name = cfg.model;
tui_state.ctx_limit = agent::TokenTracker::context_limit(cfg.model);

// 消息队列 (用户提交 → agent 处理)
std::mutex msg_mtx;
std::condition_variable msg_cv;
std::deque<std::string> msg_queue;

// 创建 TUI 组件
auto tui_component = agent::run_agent_tui(tui_state,
    [&](std::string msg) {
        // on_submit: 用户按 Enter
        {
            std::lock_guard lk(msg_mtx);
            msg_queue.push_back(std::move(msg));
        }
        msg_cv.notify_one();
    });

// Agent 工作线程
std::jthread agent_thread([&](std::stop_token st) {
    while (!st.stop_requested()) {
        std::string input;
        {
            std::unique_lock lk(msg_mtx);
            msg_cv.wait(lk, [&] { return !msg_queue.empty() || st.stop_requested(); });
            if (st.stop_requested()) break;
            input = std::move(msg_queue.front());
            msg_queue.pop_front();
        }

        if (input == "exit" || input == "quit") {
            screen.Post([&] { screen.Exit(); });
            break;
        }

        // Handle slash commands
        if (input[0] == '/' && cmd_registry.execute(input)) {
            screen.PostEvent(ftxui::Event::Custom);
            continue;
        }

        // 添加用户消息到输出
        screen.Post([&] {
            std::lock_guard lk(tui_state.mtx);
            tui_state.lines.push_back({agent::ChatLine::Separator});
            tui_state.lines.push_back({agent::ChatLine::UserMsg, .text = input});
            tui_state.lines.push_back({agent::ChatLine::Separator});
            tui_state.is_streaming = true;
            tui_state.is_thinking = true;
            tui_state.streaming_text.clear();
            // 更新历史
            if (tui_state.history.empty() || tui_state.history.back() != input) {
                tui_state.history.push_back(input);
            }
        });
        screen.PostEvent(ftxui::Event::Custom);

        // ThinkFilter for streaming
        agent::tui::ThinkFilter think_filter;

        // Run LLM turn
        auto turn_result = agent::run_one_turn(
            conversation, input, system_prompt, tools, bridge, stream, cfg,
            // Streaming callback — 通过 screen.Post 更新 TUI
            [&](std::string_view chunk) {
                think_filter.feed_to_state(chunk, tui_state, screen);
            },
            policy_ptr, confirm_cb,
            // Tool call
            [&](int id, std::string_view name, std::string_view args) {
                screen.Post([&, id, n=std::string(name), a=std::string(args)] {
                    std::lock_guard lk(tui_state.mtx);
                    ChatLine line;
                    line.type = ChatLine::ToolCall;
                    line.text = n + " (" + (a.size() > 60 ? a.substr(0,57)+"..." : a) + ")";
                    line.action_id = id;
                    tui_state.lines.push_back(std::move(line));
                });
                screen.PostEvent(ftxui::Event::Custom);
            },
            // Tool result
            [&](int id, std::string_view name, bool is_error) {
                screen.Post([&, id, n=std::string(name), is_error] {
                    std::lock_guard lk(tui_state.mtx);
                    ChatLine line;
                    line.type = ChatLine::ToolResult;
                    line.text = n;
                    line.action_id = id;
                    line.is_error = is_error;
                    tui_state.lines.push_back(std::move(line));
                });
                screen.PostEvent(ftxui::Event::Custom);
            },
            &ctx_mgr, &tracker,
            // Auto-compact
            [&](int evicted, int freed) {
                screen.Post([&, evicted, freed] {
                    std::lock_guard lk(tui_state.mtx);
                    tui_state.lines.push_back({ChatLine::Hint,
                        .text = "◈ auto-compact: evicted " + std::to_string(evicted) +
                                " turns, freed ~" + TokenTracker::format_tokens(freed) + " tokens"});
                });
                screen.PostEvent(ftxui::Event::Custom);
            }
        );

        // Turn complete: finalize streaming, update status
        screen.Post([&] {
            std::lock_guard lk(tui_state.mtx);
            if (!tui_state.streaming_text.empty()) {
                tui_state.lines.push_back({ChatLine::AssistantText,
                    .text = tui_state.streaming_text});
            }
            tui_state.is_streaming = false;
            tui_state.is_thinking = false;
            tui_state.streaming_text.clear();
            tui_state.lines.push_back({ChatLine::Separator});

            // Update status bar
            tracker.record(turn_result.input_tokens, turn_result.output_tokens);
            ctx_mgr.record_turn();
            tui_state.ctx_used = tracker.context_used();
            tui_state.session_input = tracker.session_input();
            tui_state.session_output = tracker.session_output();
            tui_state.l2_cache_count = ctx_mgr.l2_count();
        });
        screen.PostEvent(ftxui::Event::Custom);
    }
});

// ftxui 主循环 (阻塞主线程)
screen.Loop(tui_component);

// 清理
agent_thread.request_stop();
msg_cv.notify_all();
```

## ThinkFilter 适配 ftxui

当前 `ThinkFilter.feed()` 直接 `std::print`。需要改为更新 `AgentTuiState`:

```cpp
// 新增方法
void feed_to_state(std::string_view chunk, AgentTuiState& state,
                   ftxui::ScreenInteractive& screen) {
    // 现有过滤逻辑不变，但输出目标从 std::print 改为:
    // 1. 检测到 <think> → state.is_thinking = true
    // 2. 检测到 </think> → state.is_thinking = false
    // 3. 非 think 内容 → state.streaming_text += filtered_chunk
    // 每次状态变更后: screen.PostEvent(Event::Custom) 触发重绘

    for (size_t i = 0; i < chunk.size(); ++i) {
        char c = chunk[i];
        // ... 复用现有 tag 检测逻辑 ...
        // 但将 std::print → state 更新
        if (/* 非 think 内容, 非 tag buffer */) {
            std::lock_guard lk(state.mtx);
            state.streaming_text += c;
            screen.PostEvent(ftxui::Event::Custom);
        }
    }
}
```

**优化**: 不要每个字符都 PostEvent。改为批量:

```cpp
void feed_to_state(std::string_view chunk, AgentTuiState& state,
                   ftxui::ScreenInteractive& screen) {
    std::string filtered;
    // 过滤 <think> 标签，将可见内容收集到 filtered

    if (!filtered.empty()) {
        std::lock_guard lk(state.mtx);
        state.is_thinking = false;
        state.streaming_text += filtered;
    }
    // 每个 chunk 只触发一次重绘
    screen.PostEvent(ftxui::Event::Custom);
}
```

## 进度条显示

ToolBridge 的 `on_data_event` 检测 progress 事件，通过 screen.Post 更新:

```cpp
// 在 cli.cppm 注册 agent_listener 时
int agent_listener = stream.on_event([&](const auto& e) {
    if (auto* d = std::get_if<DataEvent>(&e)) {
        bridge.on_data_event(*d);

        // 进度事件 → 更新 TUI
        if (d->kind == "download_progress") {
            auto j = nlohmann::json::parse(d->json, nullptr, false);
            if (!j.is_discarded()) {
                screen.Post([&, j=std::move(j)] {
                    std::lock_guard lk(tui_state.mtx);
                    // 更新或插入 Progress line
                    update_progress_line(tui_state, j);
                });
                screen.PostEvent(ftxui::Event::Custom);
            }
        }
    }
});
```

进度行更新逻辑: 找到最后一个 Progress 类型的 line 并原地更新（而非追加新行）:

```cpp
void update_progress_line(AgentTuiState& state, const nlohmann::json& j) {
    float pct = j.value("percent", 0.0f) / 100.0f;
    auto name = j.value("name", "");
    auto speed = j.value("speed", "");
    auto eta = j.value("eta", "");

    // 找已有的 progress line 更新之
    for (auto it = state.lines.rbegin(); it != state.lines.rend(); ++it) {
        if (it->type == ChatLine::Progress) {
            it->progress = pct;
            it->text = name;
            it->speed = speed;
            it->eta = eta;
            return;
        }
        if (it->type == ChatLine::ToolCall) break;  // 只在最近的工具调用范围内找
    }
    // 没找到则新建
    state.lines.push_back({ChatLine::Progress,
        .text = name, .progress = pct, .speed = speed, .eta = eta});
}
```

## Slash 命令补全

输入变化时检测 `/` 前缀，更新补全列表:

```cpp
// 在 CatchEvent 中，监听所有字符事件后更新补全
// 或用 InputOption::on_change 回调
input_opt.on_change = [&] {
    if (!input_content.empty() && input_content[0] == '/' &&
        input_content.find(' ') == std::string::npos) {
        auto matches = cmd_registry.match(input_content);
        std::lock_guard lk(tui_state.mtx);
        tui_state.completions = std::move(matches);
        tui_state.completion_selected = tui_state.completions.empty() ? -1 : 0;
    } else {
        std::lock_guard lk(tui_state.mtx);
        tui_state.completions.clear();
        tui_state.completion_selected = -1;
    }
};
```

## GCC 15 注意事项

1. **避免 std::format 宽度说明符** — 在模块中 `{:<20s}` 会崩溃，用手动 padding
2. **Lambda 在模块中**: `selector.cppm` 已成功使用 ftxui lambda，说明简单 lambda 可以工作
3. **复杂 lambda capture**: 避免在模块接口单元中 capture `std::atomic`/`std::mutex` 等类型
4. **解决方案**: `AgentTuiState` 作为引用传递，不在 lambda 中复制复杂类型

## 文件修改清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/agent/tui.cppm` | **重写** | ChatLine + AgentTuiState + render 函数 + run_agent_tui |
| `src/agent/input.cppm` | **删除** | 不再需要自定义 raw mode 输入管理器 |
| `src/cli.cppm` | **修改** | agent REPL 用 ScreenInteractive + agent_thread |
| `src/agent/agent.cppm` | **修改** | 移除 `export import xlings.agent.input` |
| `xmake.lua` | **修改** | 移除 input.cppm |

## 不变的部分

- `loop.cppm` — 回调接口不变，只是调用方式从直接 print 改为 screen.Post
- `tool_bridge.cppm` — 事件缓冲机制不变
- `commands.cppm` — CommandRegistry 接口不变
- `token_tracker.cppm` — 数据结构不变
- `context_manager.cppm` — 接口不变
- `output_buffer.cppm` — 接口不变

## 实施步骤

### Step 1: 重写 `src/agent/tui.cppm`
- 定义 `ChatLine`、`AgentTuiState`
- 实现 `render_chat_line()`、`render_status_bar()`
- 实现 `run_agent_tui()` — 返回 ftxui Component
- 改造 `ThinkFilter` 增加 `feed_to_state()` 方法
- 保留旧的 print_ 函数（标记 deprecated）供渐进迁移

### Step 2: 修改 `src/cli.cppm` agent REPL
- 替换 InputManager 为 ScreenInteractive + agent_thread
- 消息通过 msg_queue + condition_variable 传递
- 所有 TUI 更新通过 screen.Post()

### Step 3: 删除 `src/agent/input.cppm`
- 从 `agent.cppm` 移除 re-export
- 从 `xmake.lua` 移除

### Step 4: 验证
- 编译通过
- 输入始终可见
- 流式输出正常
- 工具调用显示正常
- 进度条显示
- ↑↓ 历史
- `/` 命令补全
- 状态栏在输入框下方

## 效果对比

**Before (ANSI print)**:
```
输入框消失 → LLM 阻塞中 → 无反馈 → 输入框恢复
stdout 竞争导致乱码
```

**After (ftxui)**:
```
┌────────────────────────────────────────┐
│ ◆ thinking...                          │
│   ⚡ install_package ("node")          │
│   ▸ ████████░░░░ 45% node  2.1MB/s    │
├────────────────────────────────────────┤
│ > 用户随时可以在这里输入_                │
│ qwen3-235b | ctx: 12.4k / 128k | ...  │
└────────────────────────────────────────┘
```
