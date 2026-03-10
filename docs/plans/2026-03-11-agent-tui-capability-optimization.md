# Agent TUI 交互优化 & 能力输出捕获修复

## Context

xlings agent 当前有两个核心问题：

1. **能力输出丢失**: Capability 执行时通过 `stream.emit(DataEvent)` 输出数据，CLI 模式下渲染为 TUI，但 agent 模式下 LLM 只收到 `{"exitCode": 0}`，看不到任何实际结果。
2. **TUI 输入阻塞**: 用户提交消息后必须等 agent 处理完才出现新输入框，且 `flush_stdin()` 丢弃用户中途输入。

**方案方向**:
- 从**事件流层面**解决，不是在 ToolBridge 打补丁
- Agent 模式下，**默认不渲染 TUI 输出**（能力调用结果不需要用户看到），提供快捷键切换显示/隐藏 agent 活动细节（类似 Claude Code）
- Agent 调用 xlings 能力时，直接获取事件流的**原始信息**，由 agent 交给模型

---

## Phase 1: 事件流双消费者架构

### 核心设计

在 agent 模式下注册**两个 EventStream 消费者**：

1. **AgentConsumer** (新增): 捕获 DataEvent 原始 JSON，缓存到 buffer，供 ToolBridge 读取后交给 LLM
2. **TuiConsumer** (现有 `dispatch_data_event`): 可选，默认隐藏，通过快捷键切换

```
EventStream
  ├── AgentConsumer (always active)  → 捕获原始数据 → 注入 ToolResult → 交给 LLM
  └── TuiConsumer (toggleable)       → 渲染 TUI（默认 off，快捷键切换）
```

### 修改文件

#### 1. `src/agent/tool_bridge.cppm` — 添加事件缓冲机制

ToolBridge 持有一个事件缓冲区。在 `execute()` 前清空缓冲区，执行后将捕获的 DataEvent 原始 JSON 直接附加到返回结果中——LLM 能直接理解 JSON，无需格式转换。

```cpp
export class ToolBridge {
    capability::Registry& registry_;
    std::vector<DataEvent> event_buffer_;  // 新增

public:
    // 作为 EventStream 消费者回调
    void on_data_event(const DataEvent& e) {
        event_buffer_.push_back(e);
    }

    auto execute(...) -> ToolResult {
        event_buffer_.clear();
        auto result = cap->execute(args, stream);
        // 将 DataEvent 原始 JSON 直接附加到结果
        auto json = nlohmann::json::parse(result);
        nlohmann::json events = nlohmann::json::array();
        for (auto& ev : event_buffer_) {
            events.push_back({{"kind", ev.kind}, {"data", nlohmann::json::parse(ev.json)}});
        }
        json["events"] = std::move(events);
        return ToolResult{.content = json.dump()};
    }
};
```

模型收到的结果示例：
```json
{"exitCode":0,"events":[{"kind":"styled_list","data":{"title":"Search results:","items":[["xim:node","Node.js is..."]]}}]}
```

#### 2. `src/runtime/event_stream.cppm` — 支持 listener ID + remove

```cpp
export class EventStream {
    struct ListenerEntry {
        int id;
        EventConsumer consumer;
        bool enabled;
    };
    std::vector<ListenerEntry> consumers_;
    int next_id_ {0};

public:
    auto on_event(EventConsumer consumer) -> int {
        int id = next_id_++;
        consumers_.push_back({id, std::move(consumer), true});
        return id;
    }

    void remove_listener(int id);
    void set_enabled(int id, bool enabled);  // 用于切换 TUI 显示

    void emit(Event event) {
        for (auto& entry : consumers_) {
            if (entry.enabled) entry.consumer(event);
        }
    }
};
```

#### 3. `src/cli.cppm` — Agent 模式下注册双消费者

```cpp
// Agent 模式初始化时:
auto& bridge = ...;
bool show_activity = false;  // 默认隐藏 TUI 输出

// 消费者 1: Agent 数据捕获（始终 active）
int agent_listener = stream.on_event([&bridge](const Event& e) {
    if (auto* d = std::get_if<DataEvent>(&e)) {
        bridge.on_data_event(*d);
    }
});

// 消费者 2: TUI 渲染（默认 disabled）
int tui_listener = stream.on_event([&stream](const Event& e) {
    if (auto* d = std::get_if<DataEvent>(&e)) dispatch_data_event(*d);
    if (auto* p = std::get_if<PromptEvent>(&e)) handle_prompt(stream, *p);
});
stream.set_enabled(tui_listener, show_activity);  // 默认关闭
```

CLI 模式(非 agent)下保持现有行为，只注册 TuiConsumer。

#### 4. 快捷键切换 agent 活动细节可见性

在输入处理中识别 `/verbose` 或 `/v` 命令切换 `show_activity`：

```cpp
if (input == "/verbose" || input == "/v") {
    show_activity = !show_activity;
    stream.set_enabled(tui_listener, show_activity);
    agent::tui::print_hint(show_activity ? "activity details: ON" : "activity details: OFF");
    continue;
}
```

---

## Phase 2: 异步输入 + 消息队列 + 历史记录

### 架构

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  InputThread      │────▶│  MessageQueue    │◀────│  AgentThread     │
│  (raw terminal)   │     │  (thread-safe)   │     │  (LLM loop)     │
└──────────────────┘     └──────────────────┘     └──────────────────┘
     ↕                          │                        ↕
  历史(↑↓)                    消息排队              流式输出到终端
  行编辑                    不丢弃输入             处理完取下一条
  焦点始终在输入框
```

### 新文件: `src/agent/input.cppm`

```cpp
export class InputManager {
    std::deque<std::string> history_;
    size_t history_pos_ {0};

    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    std::deque<std::string> pending_;

    std::atomic<bool> running_ {true};
    std::jthread input_thread_;

    // Raw terminal mode 行编辑
    std::string current_line_;
    size_t cursor_pos_ {0};

public:
    void start();   // 启动输入线程，进入 raw mode
    void stop();    // 停止，恢复 cooked mode

    // 消息队列
    auto wait_pop() -> std::optional<std::string>;  // 阻塞等待下一条
    auto pending_count() -> size_t;

    // 终端管理（agent 输出时调用）
    void save_input_line();     // 临时隐藏当前输入行
    void restore_input_line();  // 恢复输入行显示

private:
    void run_loop();            // 输入线程主循环
    void handle_key(int ch);    // 处理按键（↑↓←→, backspace, enter）
    void redraw_prompt();       // 重绘 "> current_line_"
};
```

**关键行为**:
- 输入线程用 `termios` raw mode 逐字符读取
- 方向键 ↑↓ 翻阅历史
- Enter 提交到 `pending_` 队列，通知 `queue_cv_`
- Agent 流式输出时调用 `save_input_line()` / `restore_input_line()` 保证不冲突
- **不再使用 `std::getline()`**，不再 `flush_stdin()`

### `src/cli.cppm` REPL 重构

```cpp
InputManager input;
input.start();

while (true) {
    auto msg = input.wait_pop();
    if (!msg || *msg == "exit") break;

    if (handle_special(*msg)) continue;

    // Agent 输出前保存输入行
    input.save_input_line();
    print_separator();
    print_assistant_start();

    auto reply = agent::run_one_turn(...,
        [&](std::string_view chunk) {
            think_filter.feed(chunk);  // 流式输出
        }, ...);

    print_assistant_end();
    print_separator();
    input.restore_input_line();  // 恢复输入行

    // 不 flush_stdin！用户输入已在队列中
    // 如果队列有待处理消息，自动继续处理
}
```

### `src/agent/tui.cppm` 修改

- 移除 `flush_stdin()`（不再需要）
- 流式输出函数（`print_assistant_chunk` 等）配合 InputManager 的 save/restore
- 添加 `print_queued_hint(size_t count)` — 当有排队消息时提示 "N messages queued"

---

## Phase 3: Slash 命令系统（`/` 命令提示列表）

### 设计

类似 Claude Code，用户输入 `/` 时弹出可用命令列表，支持模糊匹配和选择。

### 命令列表

| 命令 | 说明 |
|------|------|
| `/save` | 保存当前会话 |
| `/sessions` | 列出所有会话 |
| `/resume [id]` | 恢复指定会话 |
| `/verbose` | 切换显示/隐藏 agent 活动细节 |
| `/model [name]` | 切换模型 / 查看当前模型 |
| `/tokens` | 查看当前会话 token 用量 |
| `/trust [level]` | 设置权限级别 (auto/confirm/readonly) |
| `/clear` | 清屏 |
| `/help` | 显示帮助 |

### 实现方式

在 `InputManager` 中，当检测到用户输入 `/` 时：

1. 进入**命令补全模式**
2. 显示匹配的命令列表（ANSI 渲染，类似下拉菜单）
3. 用户可以 ↑↓ 选择或继续输入过滤
4. Tab 或 Enter 确认选择
5. Esc 退出补全模式

### 新增文件: `src/agent/commands.cppm`

```cpp
export struct SlashCommand {
    std::string name;        // "/save"
    std::string description; // "Save current session"
    std::function<void()> handler;
};

export class CommandRegistry {
    std::vector<SlashCommand> commands_;
public:
    void register_command(SlashCommand cmd);
    auto match(std::string_view prefix) -> std::vector<const SlashCommand*>;
    auto execute(std::string_view input) -> bool;  // true = handled
};
```

### Token 统计

在 `LlmConfig` 或会话中维护 token 计数器：
- 每次 LLM 响应后累加 `response.usage.input_tokens` / `output_tokens`
- `/tokens` 命令展示累计用量

### 模型切换

`/model` 命令修改运行时的 `LlmConfig.model`，下一次 LLM 调用使用新模型。

---

## Phase 4: Agent 扩展工具集

### 设计理念

除了内置的包管理能力，agent 需要更多"元工具"来增强自主性，类似 Claude Code 的工具集。

### 4.1 Log 级别切换工具

暴露为 LLM 可调用的 tool，让模型按需调整日志详细程度（排查问题时切到 debug，正常运行切回 info）。

**新增 Capability: `SetLogLevel`**

```cpp
class SetLogLevel : public Capability {
    auto spec() -> CapabilitySpec {
        return {
            .name = "set_log_level",
            .description = "Switch log verbosity: debug, info, warn, error",
            .inputSchema = R"({"type":"object","properties":{"level":{"type":"string","enum":["debug","info","warn","error"]}},"required":["level"]})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result {
        auto level = json.value("level", "info");
        log::set_level(level);
        return R"({"success":true,"level":")" + level + R"("})";
    }
};
```

在 `src/capabilities.cppm` 的 `build_registry()` 中注册。

### 4.2 通用命令执行工具（非内置包）

对于非内置能力（如用户安装的 CLI 工具），agent 需要能执行任意命令并**捕获 stdout/stderr 作为纯文本结果**返回给模型。

**新增 Capability: `RunCommand`**

```cpp
class RunCommand : public Capability {
    auto spec() -> CapabilitySpec {
        return {
            .name = "run_command",
            .description = "Execute a shell command and capture stdout/stderr as text",
            .inputSchema = R"({"type":"object","properties":{
                "command":{"type":"string"},
                "timeout_ms":{"type":"integer","default":30000}
            },"required":["command"]})",
            .destructive = true,  // 需要用户确认
        };
    }
    auto execute(Params params, EventStream& stream) -> Result {
        // popen() 或 fork+exec 捕获 stdout+stderr
        // 返回 {"exitCode": N, "stdout": "...", "stderr": "..."}
    }
};
```

### 4.3 输出截取工具

当命令输出过长时，模型可以请求只查看特定范围，避免浪费 token。

**新增 Capability: `ViewOutput`**

```cpp
class ViewOutput : public Capability {
    // 维护一个 last_output 缓冲区
    auto spec() -> CapabilitySpec {
        return {
            .name = "view_output",
            .description = "View a range of lines from the last command output",
            .inputSchema = R"({"type":"object","properties":{
                "start_line":{"type":"integer","default":1},
                "end_line":{"type":"integer","default":50},
                "search":{"type":"string","description":"Optional: filter lines containing this text"}
            }})",
            .destructive = false,
        };
    }
    auto execute(Params params, EventStream& stream) -> Result {
        // 从 last_output 缓冲区截取 [start_line, end_line]
        // 如果有 search，只返回匹配行
        return json with truncated output;
    }
};
```

### 4.4 内容搜索工具

让模型能搜索文件内容或命令输出，类似 grep。

**新增 Capability: `SearchContent`**

```cpp
class SearchContent : public Capability {
    auto spec() -> CapabilitySpec {
        return {
            .name = "search_content",
            .description = "Search for text patterns in files or last output",
            .inputSchema = R"({"type":"object","properties":{
                "pattern":{"type":"string"},
                "source":{"type":"string","enum":["last_output","file"],"default":"last_output"},
                "path":{"type":"string","description":"File path (when source=file)"},
                "max_results":{"type":"integer","default":20}
            },"required":["pattern"]})",
            .destructive = false,
        };
    }
};
```

### 4.5 共享输出缓冲区

`RunCommand`、`ViewOutput`、`SearchContent` 共享一个**输出缓冲区**：

```cpp
// src/agent/output_buffer.cppm (新文件)
export class OutputBuffer {
    std::string content_;
    std::vector<std::string> lines_;  // 按行分割
    std::mutex mtx_;
public:
    void set(std::string output);           // RunCommand 执行后存入
    auto lines(int start, int end) -> std::string;  // ViewOutput 使用
    auto search(std::string_view pattern, int max) -> std::string;  // SearchContent 使用
    auto line_count() -> size_t;
};
```

---

## 实施顺序

### Phase 1: 能力输出捕获（修复核心 bug）
| Step | 文件 | 内容 |
|------|------|------|
| 1 | `src/runtime/event_stream.cppm` | 添加 listener ID、remove_listener、set_enabled |
| 2 | `src/agent/tool_bridge.cppm` | 添加 event_buffer_、on_data_event()、原始 JSON 注入 |
| 3 | `src/cli.cppm` | Agent 模式注册双消费者，/verbose 切换 |
| 4 | 测试 | 验证 agent 能看到能力执行结果 |

### Phase 2: 异步输入 + 消息队列 + 历史
| Step | 文件 | 内容 |
|------|------|------|
| 5 | `src/agent/input.cppm` | 新建 InputManager (raw mode, history, queue) |
| 6 | `src/agent/tui.cppm` | 适配异步输入，移除 flush_stdin |
| 7 | `src/cli.cppm` | REPL 循环重构为异步 |
| 8 | `xmake.lua` | 添加 input.cppm |
| 9 | 测试 | 验证异步输入、消息队列、↑↓ 历史 |

### Phase 3: Slash 命令系统
| Step | 文件 | 内容 |
|------|------|------|
| 10 | `src/agent/commands.cppm` | 新建 CommandRegistry + SlashCommand 定义 |
| 11 | `src/agent/input.cppm` | 添加 `/` 补全模式，匹配+选择 UI |
| 12 | `src/cli.cppm` | 注册所有命令，token 统计，模型切换逻辑 |
| 13 | `src/agent/loop.cppm` | 累计 token usage 到会话 |
| 14 | `xmake.lua` | 添加 commands.cppm |
| 15 | 测试 | 验证 `/` 命令补全、token 统计、模型切换 |

### Phase 4: Agent 扩展工具集
| Step | 文件 | 内容 |
|------|------|------|
| 16 | `src/agent/output_buffer.cppm` | 新建共享输出缓冲区 |
| 17 | `src/capabilities.cppm` | 新增 SetLogLevel、RunCommand、ViewOutput、SearchContent |
| 18 | `src/capabilities.cppm` | 在 build_registry() 中注册新 Capability |
| 19 | `xmake.lua` | 添加 output_buffer.cppm |
| 20 | 测试 | 验证 run_command 捕获输出、view_output 截取、search_content 搜索 |

## 关键文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/runtime/event_stream.cppm` | 修改 | listener ID + enable/disable |
| `src/agent/tool_bridge.cppm` | 修改 | 事件缓冲 + 原始 JSON 注入 |
| `src/cli.cppm` | 修改 | 双消费者 + REPL 重构 + 命令注册 |
| `src/agent/input.cppm` | **新建** | 异步输入管理器 + `/` 补全 |
| `src/agent/commands.cppm` | **新建** | Slash 命令注册表 |
| `src/agent/output_buffer.cppm` | **新建** | 共享输出缓冲区 |
| `src/agent/tui.cppm` | 修改 | 适配异步输入 |
| `src/agent/loop.cppm` | 修改 | token usage 累计 |
| `src/capabilities.cppm` | 修改 | 新增 4 个扩展 Capability |
| `xmake.lua` | 修改 | 添加新模块 |

## 验证

1. `xlings agent chat` → "搜索 node" → agent 回复正确引用搜索结果（不再说 output 为空）
2. `/verbose` → 切换显示/隐藏能力调用的 TUI 输出
3. Agent 回复中 → 按 ↑ 键 → 显示上一条输入
4. Agent 处理中 → 输入新消息 → 消息排队，处理完后自动执行
5. 输入 `/` → 弹出命令列表，继续输入过滤，↑↓ 选择
6. `/tokens` → 显示当前会话 token 用量
7. `/model qwen3` → 切换模型
8. Agent: "运行 ls -la" → run_command 执行，stdout 作为纯文本返回给模型
9. Agent: "看输出的第 10-20 行" → view_output 截取范围
10. Agent: "搜索输出中包含 error 的行" → search_content 过滤
11. Agent: "切换日志到 debug" → set_log_level 生效
