# xlings Agent 系统设计

> 状态：设计提案
> 日期：2026-03-10
> 版本：v0.2

## 1. 设计目标

将 xlings 从"面向人类终端的 CLI 工具"演进为"能力可编程的平台"，使得：

- **人类**通过 TUI 直接操作（当前体验不变）
- **AI Agent**通过结构化 API 异步操控 xlings 全部能力
- **外部工具**通过 MCP 协议接入（Claude Code、Cursor 等）
- **未来桌面端**实现自己的 Consumer 即可接入

核心原则：**Core 是通用能力，产出统一事件流，不同前端是不同的 Consumer。**

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Layer 3: Frontends (事件消费者)                                          │
│                                                                         │
│  ┌───────────┐  ┌───────────────┐  ┌───────────┐  ┌───────────────┐   │
│  │  CLI/TUI  │  │ Agent Runtime │  │ MCP Server│  │ Desktop (未来) │   │
│  │  (人类)    │  │ (LLM)        │  │ (外部)     │  │ (GUI)         │   │
│  └─────┬─────┘  └──────┬────────┘  └─────┬─────┘  └──────┬────────┘   │
│        │               │                 │                │            │
│   TuiConsumer     AgentConsumer      McpConsumer    DesktopConsumer    │
│        │               │                 │                │            │
├────────┴───────────────┴─────────────────┴────────────────┴────────────┤
│ Layer 2: Task Runtime (仅 Agent/MCP 使用)                               │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────┐          │
│  │ TaskManager                                              │          │
│  │  submit / cancel / status / events / respond             │          │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐              │          │
│  │  │ Task#1   │  │ Task#2   │  │ Task#3   │  (并发执行)   │          │
│  │  │ +Stream  │  │ +Stream  │  │ +Stream  │              │          │
│  │  └──────────┘  └──────────┘  └──────────┘              │          │
│  └──────────────────────────────────────────────────────────┘          │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 1: Capability (能力抽象层)                                         │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────┐          │
│  │ Capability Registry                                      │          │
│  │                                                          │          │
│  │  High-level (面向意图，LLM 默认使用):                     │          │
│  │    install_package / remove_package / search_packages    │          │
│  │    setup_environment / switch_version / get_status       │          │
│  │    manage_subos / get_package_info                       │          │
│  │                                                          │          │
│  │  Low-level (原子操作，精细控制):                           │          │
│  │    xim::* / xvm::* / subos::* / config::* / index::*    │          │
│  └──────────────────────────────────────────────────────────┘          │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 0: Core (核心能力 + EventStream)                                   │
│                                                                         │
│  ┌──────────────────────────────────┐  ┌─────────────────────────┐     │
│  │ 核心模块                          │  │ EventStream             │     │
│  │  xim    xvm    subos            │  │  emit(Event)            │     │
│  │  config  platform  log          │──│  on_event(Consumer)     │     │
│  │  index   script                 │  │  prompt(req) → response │     │
│  └──────────────────────────────────┘  │  respond(id, value)     │     │
│                                        └─────────────────────────┘     │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Layer 0: Core + EventStream

### 3.1 设计原理

当前 Core 模块直接调用 `log::info()`、`ui::progress()`、`stdin` 读取等，I/O 和业务逻辑耦合。

改造后，Core 模块通过 **EventStream** 发射结构化事件。上层注册 Consumer 消费事件。**终端渲染、Agent 日志收集、MCP 通知、桌面 GUI 都是 Consumer——没有特殊路径。**

这一设计对标：
- **Bazel BEP**: 终端 renderer 是 Build Event Protocol 的消费者之一
- **OpenTofu**: 人类输出和机器输出可以同时消费同一个事件流
- **OpenAI Codex CLI**: TUI / headless / JSON-RPC 三种模式共享 codex-core 事件
- **Nushell**: 一切皆结构化数据，终端渲染只是 formatter

### 3.2 事件定义

```cpp
// core/event.cppm

export module xlings.event;

import std;

namespace xlings {

// ─── 进度阶段 ───
enum class Phase {
    resolving,      // 解析依赖
    downloading,    // 下载
    extracting,     // 解压
    installing,     // 安装
    configuring,    // 配置
    verifying       // 验证
};

// ─── 日志级别 ───
enum class LogLevel { debug, info, warn, error };

// ─── 事件类型 ───

struct ProgressEvent {
    Phase phase;
    float percent;                          // 0.0 ~ 1.0
    std::string message;
};

struct LogEvent {
    LogLevel level;
    std::string message;
};

struct PromptEvent {
    std::string id;                         // 唯一标识，用于匹配响应
    std::string question;
    std::vector<std::string> options;       // 空 = 自由输入
    std::string default_value;
};

struct ErrorEvent {
    int code;
    std::string message;
    bool recoverable = false;
};

struct DataEvent {
    std::string kind;                       // "package_info" / "search_results" 等
    std::string json;                       // 结构化 JSON 数据体
};

struct CompletedEvent {
    bool success;
    std::string summary;
};

// ─── 统一事件 ───
using Event = std::variant<
    ProgressEvent,
    LogEvent,
    PromptEvent,
    ErrorEvent,
    DataEvent,
    CompletedEvent
>;

}  // namespace xlings
```

### 3.3 EventStream

EventStream 是 Core 的唯一 I/O 出口，承担两个职责：
1. **事件广播**——Core 发射事件，所有 Consumer 收到
2. **Prompt 应答通道**——发射 PromptEvent 后阻塞，等待外部 respond()

```cpp
// core/event_stream.cppm

export module xlings.event_stream;

import xlings.event;
import std;

namespace xlings {

// Consumer 回调类型
using EventConsumer = std::function<void(const Event&)>;

class EventStream {
public:
    // ─── Consumer 注册 ───
    void on_event(EventConsumer consumer);

    // ─── Core 调用：发射事件 ───
    void emit(Event event);

    // ─── Core 调用：发射 prompt 并阻塞等待响应 ───
    //     内部：emit(PromptEvent) → 挂起当前线程 → 等待 respond() 唤醒
    auto prompt(PromptEvent req) -> std::string;

    // ─── 外部调用：注入 prompt 响应 ───
    //     TUI Consumer 读取用户输入后调用
    //     Agent Consumer 由 LLM 决策后调用
    void respond(std::string_view prompt_id, std::string_view response);

private:
    std::vector<EventConsumer> consumers_;

    // prompt 同步机制
    std::mutex prompt_mutex_;
    std::condition_variable prompt_cv_;
    std::unordered_map<std::string, std::string> prompt_responses_;
};
```

**实现要点：**

```cpp
void EventStream::emit(Event event) {
    for (auto& consumer : consumers_) {
        consumer(event);
    }
}

auto EventStream::prompt(PromptEvent req) -> std::string {
    auto id = req.id;

    // 1. 广播 PromptEvent 给所有 Consumer
    emit(Event{req});

    // 2. 阻塞等待响应
    std::unique_lock lock(prompt_mutex_);
    prompt_cv_.wait(lock, [&] {
        return prompt_responses_.contains(id);
    });

    // 3. 取出响应并返回
    auto response = std::move(prompt_responses_[id]);
    prompt_responses_.erase(id);
    return response;
}

void EventStream::respond(std::string_view prompt_id, std::string_view response) {
    std::lock_guard lock(prompt_mutex_);
    prompt_responses_[std::string(prompt_id)] = std::string(response);
    prompt_cv_.notify_all();
}
```

### 3.4 Core 模块改造

```cpp
// ─── 改造前 (core/xim/installer.cppm) ───
void install(const Package& pkg) {
    log::info("Downloading {}...", pkg.url);              // 直接打印终端
    auto bytes = download(pkg.url);
    log::info("Installing {}...", pkg.name);
    if (fs::exists(target)) {
        auto answer = ui::confirm("已存在，覆盖？");       // 直接阻塞 stdin
        if (answer != "y") return;
    }
    do_install(bytes, target);
    log::info("Done.");
}

// ─── 改造后 ───
void install(const Package& pkg, EventStream& stream) {
    stream.emit(ProgressEvent{
        .phase = Phase::downloading,
        .percent = 0.0,
        .message = std::format("Downloading {}...", pkg.url)
    });

    auto bytes = download(pkg.url, [&](float pct) {
        stream.emit(ProgressEvent{Phase::downloading, pct, ""});
    });

    stream.emit(ProgressEvent{
        .phase = Phase::installing,
        .percent = 0.0,
        .message = std::format("Installing {}...", pkg.name)
    });

    if (fs::exists(target)) {
        auto answer = stream.prompt({
            .id = generate_id(),
            .question = std::format("{} 已存在，是否覆盖？", pkg.name),
            .options = {"y", "n"},
            .default_value = "n"
        });
        if (answer != "y") return;
    }

    do_install(bytes, target);

    stream.emit(CompletedEvent{
        .success = true,
        .summary = std::format("{} installed", pkg.name)
    });
}
```

**Core 不感知调用者是谁。它只向 EventStream 发射事件，由 Consumer 决定如何呈现和响应。**

### 3.5 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| EventStream 传递方式 | 函数参数 `EventStream& stream` | 显式依赖，每个调用链可用不同 stream |
| 事件模型 | `std::variant` + Listener/Observer | 轻量，2-3 个 consumer，无需 channel/protobuf |
| prompt 语义 | 发射事件 + 阻塞等待 respond() | Core 逻辑顺序执行；并发在 Task Runtime 层 |
| 事件类型 | 类型化 struct（非 JSON） | 内部零序列化开销；序列化交给需要的 Consumer |
| Consumer 注册时机 | EventStream 创建后、Core 执行前 | 保证不丢事件 |

---

## 4. Layer 1: Capability（能力抽象层）

### 4.1 能力接口

```cpp
// core/capability.cppm

export module xlings.capability;

import xlings.event_stream;
import std;

namespace xlings::capability {

using Params = std::string;   // JSON string
using Result = std::string;   // JSON string

// 能力描述（可直接转换为 MCP tool schema）
struct CapabilitySpec {
    std::string name;              // "install_package"
    std::string description;       // "Install a package with optional version"
    std::string input_schema;      // JSON Schema
    std::string output_schema;     // JSON Schema
    bool destructive = false;      // 是否有副作用（MCP annotation）
    bool async_capable = true;     // 是否支持异步执行
};

// 能力接口 — 所有能力实现这个接口
struct Capability {
    virtual ~Capability() = default;
    virtual auto spec() const -> CapabilitySpec = 0;
    virtual auto execute(Params params, EventStream& stream) -> Result = 0;
};

}  // namespace xlings::capability
```

### 4.2 双层能力模型

#### High-level：面向意图（LLM 默认调用）

每个能力是一个**自包含的意图单元**，LLM 不需要了解 xlings 内部模块划分。

| 能力名 | 说明 | destructive | 内部编排 |
|--------|------|:-----------:|----------|
| `install_package` | 安装包（含搜索+下载+安装+版本设置） | yes | xim::search → xim::install → xvm::use |
| `remove_package` | 卸载包 | yes | xim::remove |
| `search_packages` | 搜索包 | no | xim::search → DataEvent |
| `get_package_info` | 查询包详情 | no | xim::info → DataEvent |
| `setup_environment` | 分析项目并安装所有依赖 | yes | 解析配置 → 批量 install |
| `switch_version` | 切换工具版本 | yes | xvm::use |
| `manage_subos` | SubOS 生命周期管理 | yes | subos::create/attach/detach |
| `get_system_status` | 全局状态快照 | no | 汇总 installed + versions + subos + config |

#### Low-level：原子操作（Agent 精细控制）

直接暴露 Core 模块的单一操作：

```
xim::install    xim::remove     xim::search     xim::list
xim::info       xim::update     xvm::use        xvm::list_versions
xvm::resolve    subos::create   subos::attach   subos::detach
subos::list     config::get     config::set     index::sync
index::add_repo script::run
```

### 4.3 Capability Registry

```cpp
// core/capability_registry.cppm

export module xlings.capability_registry;

import xlings.capability;
import std;

namespace xlings::capability {

class Registry {
public:
    void register_capability(std::unique_ptr<Capability> cap);
    auto get(std::string_view name) -> Capability*;
    auto list_all() -> std::vector<CapabilitySpec>;
    auto list_by_destructive(bool destructive) -> std::vector<CapabilitySpec>;

private:
    std::unordered_map<std::string, std::unique_ptr<Capability>> capabilities_;
};

auto build_default_registry() -> Registry;

}  // namespace xlings::capability
```

### 4.4 Capability → MCP Tool Schema 自动映射

```
CapabilitySpec {                          MCP Tool {
  name: "install_package"        →          name: "install_package"
  description: "Install a..."    →          description: "Install a..."
  input_schema: "{...}"          →          inputSchema: {...}
  output_schema: "{...}"         →          // structured output
  destructive: true              →          annotations: { destructive: true }
}
```

能力注册一次，CLI / Agent / MCP / Desktop 四端自动可用。

---

## 5. Layer 2: Task Runtime（任务运行时）

**仅 Agent 和 MCP 路径使用。**

CLI/TUI 同步调用 Capability，不经过 Task Runtime。Task Runtime 将同步的 Capability 执行包装为异步任务，支持并发、取消、状态查询。

### 5.1 核心概念

```
Task = 一个 Capability 的一次异步执行
     = 独立线程 + 独立 EventStream + 事件缓冲区
```

每个 Task 拥有自己的 EventStream 实例。TaskManager 为其注册一个 AgentConsumer，将事件缓冲到队列中供外部拉取。

### 5.2 TaskManager API

```cpp
// core/task.cppm

export module xlings.task;

import xlings.event;
import xlings.capability;
import std;

namespace xlings::task {

using TaskId = std::string;

enum class TaskStatus {
    pending,            // 已提交，等待执行
    running,            // 执行中
    waiting_prompt,     // 阻塞在 prompt，等待外部响应
    completed,          // 成功完成
    failed,             // 失败
    cancelled           // 已取消
};

// 带时间戳的事件记录
struct EventRecord {
    std::size_t index;           // 序号，用于增量拉取
    std::string timestamp;
    Event event;
};

// 任务快照
struct TaskInfo {
    TaskId id;
    std::string capability_name;
    std::string params;
    TaskStatus status;
    float progress_pct;
    std::string current_phase;
    std::size_t event_count;
    std::size_t pending_prompt_count;      // 未响应的 prompt 数
};

class TaskManager {
public:
    explicit TaskManager(capability::Registry& registry);

    // ─── 提交任务 ───
    auto submit(std::string_view capability_name, capability::Params params) -> TaskId;
    auto submit_batch(
        std::vector<std::pair<std::string, capability::Params>> tasks
    ) -> std::vector<TaskId>;

    // ─── 任务控制 ───
    void cancel(TaskId id);

    // ─── 状态查询 ───
    auto info(TaskId id) -> TaskInfo;
    auto info_all() -> std::vector<TaskInfo>;
    bool has_active_tasks() const;

    // ─── 事件拉取（增量） ───
    auto events(TaskId id, std::size_t since_index = 0) -> std::vector<EventRecord>;

    // ─── 响应 prompt ───
    void respond(TaskId task_id, std::string_view prompt_id, std::string_view response);
};

}  // namespace xlings::task
```

### 5.3 Task 内部结构

```
TaskManager::submit("install_package", params)
    │
    ├─ 1. 从 Registry 获取 Capability
    │
    ├─ 2. 创建该 Task 专属的 EventStream
    │
    ├─ 3. 为 EventStream 注册 AgentConsumer:
    │        on_event → 追加到 EventBuffer（线程安全）
    │        PromptEvent → 同时标记 task 状态为 waiting_prompt
    │
    ├─ 4. 在线程池中执行:
    │        capability.execute(params, task_event_stream)
    │        ↓ Core 逻辑正常运行，发射事件到 task_event_stream
    │        ↓ 遇到 prompt() → 阻塞任务线程，等待 respond()
    │
    └─ 5. 返回 TaskId

外部交互:
    events(task_id, since)      → 从 EventBuffer 增量拉取
    respond(task_id, pid, val)  → 转发给 task_event_stream.respond()
                                  → 任务线程被唤醒，继续执行
```

### 5.4 EventStream 在两种路径下的差异

```
CLI/TUI 路径:
    一个 EventStream 实例
    注册 TuiConsumer
    同步执行 Capability
    Capability 调用 stream.prompt() → 同步广播到 TuiConsumer
    TuiConsumer 渲染交互组件 → 读取用户输入 → 调用 stream.respond()
    prompt() 返回 → Core 继续
    整个过程在主线程完成，零异步开销

Agent/MCP 路径:
    每个 Task 一个 EventStream 实例
    注册 AgentConsumer（缓冲事件到队列）
    异步执行 Capability（在独立线程）
    Capability 调用 stream.prompt() → 阻塞任务线程
    Agent 主循环 poll events → 发现 PromptEvent → LLM 决策/转发用户
    Agent 调用 TaskManager.respond() → 转发到 stream.respond()
    任务线程被唤醒 → Core 继续
```

---

## 6. Layer 3: Frontends（事件消费者）

### 6.1 TUI Frontend（人类用户）

**同步调用 Capability + TuiConsumer。不经过 Task Runtime。**

```cpp
// TuiConsumer — 对接 ftxui 渲染
void run_tui_command(const std::string& capability_name,
                     capability::Params params,
                     capability::Registry& registry) {

    EventStream stream;

    // 注册 TUI 消费者
    stream.on_event([&](const Event& e) {
        std::visit(overloaded{
            [](const ProgressEvent& p) {
                // ftxui 进度条 + 动画 + 主题色
                tui::render_progress(p.phase, p.percent, p.message);
            },
            [](const LogEvent& l) {
                // 彩色 ANSI 打印（复用现有 theme 系统）
                // debug 级别不显示（人类不需要）
                if (l.level != LogLevel::debug)
                    tui::color_print(l.level, l.message);
            },
            [&](const PromptEvent& p) {
                // ftxui 交互组件 → 读取用户输入 → 注入响应
                auto answer = tui::interactive_prompt(p.question, p.options, p.default_value);
                stream.respond(p.id, answer);
            },
            [](const ErrorEvent& e) {
                // 红色错误 + 建议信息
                tui::render_error(e.code, e.message);
            },
            [](const DataEvent& d) {
                // ftxui 表格/面板渲染
                tui::render_data(d.kind, d.json);
            },
            [](const CompletedEvent& c) {
                tui::render_completed(c.success, c.summary);
            }
        }, e);
    });

    // 同步执行
    auto* cap = registry.get(capability_name);
    cap->execute(params, stream);
}
```

**用户体验完全不变。** 改造量仅为将 Core 内的直接 I/O 调用替换为 EventStream 发射。TuiConsumer 内部调用的仍是现有 ftxui/theme 渲染逻辑。

### 6.2 Agent Frontend（内嵌 Agent Runtime）

```
`xlings agent <command>` CLI 入口
    │
    ├─ 初始化:
    │    Agent Runtime（LLM provider + system prompt）
    │    TaskManager（持有 Capability Registry）
    │    工具列表（从 Registry.list_all() 自动生成）
    │
    ├─ Agent Loop:
    │    ┌─────────────────────────────────────────────────────────┐
    │    │ 1. LLM 推理                                            │
    │    │    输入: 用户请求 + 系统状态 + 任务状态 + 工具列表      │
    │    │    输出: 下一步 action（调用工具 / 回复用户）            │
    │    │                                                         │
    │    │ 2. 执行工具调用                                         │
    │    │    → TaskManager.submit(capability, params)             │
    │    │    → 可并发提交多个任务                                  │
    │    │                                                         │
    │    │ 3. 事件轮询                                             │
    │    │    while has_active_tasks:                              │
    │    │      for task in active_tasks:                          │
    │    │        events = TaskManager.events(task.id, since)      │
    │    │        for event in events:                             │
    │    │          ProgressEvent → 按策略采样（避免 token 浪费）   │
    │    │          LogEvent      → 追加到 LLM 上下文              │
    │    │          PromptEvent   → LLM 决策 or 转发用户           │
    │    │                         → TaskManager.respond(...)     │
    │    │          ErrorEvent    → LLM 判断重试/终止/上报          │
    │    │          DataEvent     → 解析 JSON，作为推理输入         │
    │    │          CompletedEvent→ 记录结果                       │
    │    │                                                         │
    │    │ 4. 汇总结果 → 回到步骤 1 或告知用户                     │
    │    └─────────────────────────────────────────────────────────┘
    │
    └─ 结束
```

#### Agent 事件消费策略

Agent 和人类对信息的需求不同：

| 事件类型 | TUI Consumer (人类) | Agent Consumer (LLM) |
|---------|---------------------|----------------------|
| ProgressEvent | ftxui 进度条动画 | 按阈值采样（如每 20% 一次），避免 token 浪费 |
| LogEvent(debug) | 不显示 | **全量保留**——LLM 诊断问题的关键信息 |
| LogEvent(info) | 彩色打印 | 保留，追加到上下文 |
| LogEvent(warn/error) | 高亮显示 | **高优先级**推入 LLM 上下文 |
| PromptEvent | ftxui 交互组件 | LLM 自主决策 or 转发用户 |
| DataEvent | ftxui 表格/面板 | 直接解析 JSON，作为 LLM 推理输入 |
| ErrorEvent | 红色提示 | LLM 判断：重试 / 换方案 / 上报用户 |
| CompletedEvent | 完成提示 | 汇总到结果集 |

日志详细度可配置：
- `verbose`: 全量日志注入 LLM 上下文（调试模式）
- `normal`: info + warn + error（默认）
- `minimal`: 仅 warn + error（节省 token）

### 6.3 MCP Server Frontend（外部 Agent）

```
MCP Client (Claude Code / Cursor / ...)
    │
    ├─ tools/list
    │    → Registry.list_all() → 自动转换为 MCP tool definitions
    │    → 附加 task 管理工具: get_task_status / get_task_events / respond_prompt
    │
    ├─ tools/call "install_package" {name: "gcc", version: "15"}
    │    → TaskManager.submit("install_package", params)
    │    → 返回 { task_id: "t1" }
    │
    ├─ tools/call "get_task_events" {task_id: "t1", since: 0}
    │    → TaskManager.events("t1", 0)
    │    → 返回事件列表 JSON
    │
    ├─ tools/call "respond_prompt" {task_id: "t1", prompt_id: "p1", response: "y"}
    │    → TaskManager.respond("t1", "p1", "y")
    │
    └─ 传输层: stdio (本地) / Streamable HTTP (远程)
```

MCP Server 是 Registry + TaskManager 的薄 JSON-RPC 包装，不含业务逻辑。

### 6.4 Desktop Frontend（未来）

```cpp
// DesktopConsumer — Qt/GTK/native GUI
stream.on_event([&](const Event& e) {
    std::visit(overloaded{
        [](const ProgressEvent& p)  { gui::update_progress_bar(p); },
        [](const LogEvent& l)       { gui::append_log_panel(l); },
        [&](const PromptEvent& p)   {
            // GUI 对话框 → 用户选择 → 注入响应
            gui::show_dialog(p, [&](auto answer) { stream.respond(p.id, answer); });
        },
        [](const ErrorEvent& e)     { gui::show_error_dialog(e); },
        [](const DataEvent& d)      { gui::render_data_card(d); },
        [](const CompletedEvent& c) { gui::show_notification(c); }
    }, e);
});
```

只需实现 Consumer + 调用 Capability，即可获得 xlings 全部能力。

---

## 7. 完整数据流

### 7.1 人类通过 TUI 安装包

```
用户输入: xlings install gcc@15

CLI 解析参数
    → 创建 EventStream
    → 注册 TuiConsumer
    → Capability("install_package").execute(params, stream)       ← 同步
        │
        ├─ stream.emit(ProgressEvent{downloading, 0.3, "..."})
        │    └→ TuiConsumer: 渲染 ftxui 进度条
        │
        ├─ stream.emit(ProgressEvent{downloading, 0.8, "..."})
        │    └→ TuiConsumer: 更新进度条
        │
        ├─ stream.prompt(PromptEvent{id:"p1", question:"gcc-14 已存在，覆盖？"})
        │    ├→ TuiConsumer: 收到 PromptEvent → 渲染 y/n 交互
        │    ├→ 用户输入 "y" → stream.respond("p1", "y")
        │    └→ prompt() 返回 "y" → Core 继续
        │
        ├─ stream.emit(ProgressEvent{installing, 1.0, "Done"})
        │    └→ TuiConsumer: 显示完成
        │
        └─ stream.emit(CompletedEvent{true, "gcc-15 installed"})

直接、同步、单线程。无 Task Runtime 开销。
```

### 7.2 Agent 并发安装 + 处理 prompt

```
用户: "帮我搭建 gcc15 + cmake 环境"

Agent LLM 推理: 需要两个包 → 并发安装
    → t1 = TaskManager.submit("install_package", {gcc, 15})
    → t2 = TaskManager.submit("install_package", {cmake, 3.30})

Task#1 线程 (EventStream#1 + AgentConsumer#1):
    Capability.execute({gcc,15}, stream#1)
        stream#1.emit(LogEvent{info, "Resolving gcc-15..."})     → 缓冲到队列
        stream#1.emit(ProgressEvent{downloading, 0.5, ...})      → 缓冲到队列
        stream#1.emit(CompletedEvent{true, "gcc-15 installed"})  → 缓冲到队列

Task#2 线程 (EventStream#2 + AgentConsumer#2):
    Capability.execute({cmake,3.30}, stream#2)
        stream#2.emit(ProgressEvent{downloading, 0.8, ...})      → 缓冲到队列
        stream#2.prompt(PromptEvent{id:"p1", "cmake-3.29 已存在，覆盖？"})
            → PromptEvent 缓冲到队列
            → Task#2 线程阻塞，等待 respond

Agent 主循环 (与用户交互的主线程):
    events(t1) → [LogEvent, ProgressEvent, CompletedEvent]  → t1 完成
    events(t2) → [ProgressEvent, PromptEvent{id:"p1"}]      → 发现 prompt!
        → LLM 推理: 用户要 3.30，应该覆盖
        → TaskManager.respond("t2", "p1", "y")
            → stream#2.respond("p1", "y")
            → Task#2 线程被唤醒，继续执行

        stream#2.emit(ProgressEvent{installing, 0.6, ...})
        stream#2.emit(CompletedEvent{true, "cmake-3.30 installed"})

    events(t2) → [ProgressEvent, CompletedEvent]  → t2 完成

Agent 汇总: "gcc 15 和 cmake 3.30 安装完成"
```

### 7.3 外部 Agent 通过 MCP

```
Claude Code:  tools/call "search_packages" {query: "json parser"}
    → TaskManager.submit("search_packages", ...)
    → TaskId: "t1"

Claude Code:  tools/call "get_task_events" {task_id: "t1", since: 0}
    → [DataEvent{kind:"search_results", json:[...]}, CompletedEvent]
    → Claude Code 拿到结构化搜索结果

Claude Code:  tools/call "install_package" {name: "nlohmann-json"}
    → TaskId: "t2"

Claude Code:  tools/call "get_task_events" {task_id: "t2", since: 0}
    → [ProgressEvent, ProgressEvent, CompletedEvent]
    → 安装完成
```

---

## 8. 安全模型

### 8.1 三层防线

```
Layer 1: Capability 级
    CapabilitySpec.destructive 标注
    非 destructive（search, info, status）→ 无需确认
    destructive（install, remove, config set）→ Core 发射 PromptEvent

Layer 2: Consumer 级
    TuiConsumer   → 人类直接确认
    AgentConsumer → LLM 决策 or 转发用户（受 trust_level 约束）
    McpConsumer   → MCP tool annotations 告知客户端
    DesktopConsumer → GUI 对话框

Layer 3: 审计级
    所有 EventStream 事件 → JSONL 审计日志
    记录: capability 名 + 参数 + 全部事件 + prompt 应答 + 时间戳
```

### 8.2 Agent trust_level 配置

```json
// .xlings.json
{
  "agent": {
    "trust_level": "confirm",
    "provider": "anthropic",
    "model": "claude-sonnet-4-6",
    "log_verbosity": "normal"
  }
}
```

| trust_level | 行为 |
|-------------|------|
| `confirm` | 所有 destructive PromptEvent 转发给用户确认（默认） |
| `auto` | Agent 自主决策所有 prompt，仅 ErrorEvent 上报用户 |
| `readonly` | Agent 只能调用 destructive=false 的能力 |

---

## 9. 模块依赖与编译顺序

```
xlings.event               ← 零依赖，纯类型定义
xlings.event_stream        ← 依赖 event
xlings.capability          ← 依赖 event_stream
xlings.capability_registry ← 依赖 capability
xlings.task                ← 依赖 capability, event_stream
xlings.frontend.tui        ← 依赖 event_stream, capability_registry
xlings.frontend.agent      ← 依赖 task, capability_registry, LLM client
xlings.frontend.mcp        ← 依赖 task, capability_registry, MCP protocol
```

编译顺序：`event → event_stream → capability → registry → task → frontends`

Core 现有模块（xim, xvm, subos 等）仅新增 `EventStream&` 参数，不依赖上层任何模块。

---

## 10. 改造路径

### Phase 1: EventStream + TuiConsumer（对用户无感的重构）

- 定义 `xlings.event` 和 `xlings.event_stream` 模块
- 实现 TuiConsumer（包装现有 ftxui/theme/log 调用）
- 逐步改造 Core 模块：直接 I/O → EventStream 发射
- CLI 入口创建 EventStream + 注册 TuiConsumer + 同步调用 Capability
- **用户体验不变，但 I/O 解耦完成**

### Phase 2: Capability 层 + Registry

- 定义 `xlings.capability` 模块
- 实现 8 个 high-level 能力（包装现有 CLI 逻辑）
- 注册 low-level 原子能力
- CLI 命令从直接调用 Core 改为通过 Capability 调用

### Phase 3: Task Runtime

- 实现 `xlings.task` 模块（TaskManager + 线程池）
- 每个 Task 独立 EventStream + AgentConsumer
- prompt 阻塞/唤醒机制

### Phase 4: Agent Runtime

- 集成 LLM client 库
- 实现 Agent Loop（gather → act → verify）
- `xlings agent` 子命令入口
- agent 子命令：`setup` / `scaffold` / `contribute`

### Phase 5: MCP Server

- CapabilitySpec → MCP tool schema 自动转换
- TaskManager → MCP tools 包装（get_task_events / respond_prompt）
- stdio + Streamable HTTP 传输层
- `xlings mcp-server` 启动命令

---

## 11. 已知问题与待定事项

| 问题 | 状态 | 备注 |
|------|------|------|
| EventStream 参数传递到深层调用链的人体工学 | 待验证 | 可考虑 thread_local EventStream* 备选方案 |
| GCC 15 模块 + std::variant + std::function 兼容性 | 待验证 | 需要在模块接口中测试 |
| LLM client 库选择 | 待定 | 用户已有库支持 |
| 协程 vs 线程池的 Task 执行模型 | 待定 | 初期用线程池，C++23 协程成熟后可迁移 |
| Capability 参数 JSON vs typed struct | 待定 | 内部 typed struct + JSON 序列化层 |
| EventStream Consumer 注册的线程安全 | 设计中 | 注册在执行前完成，执行期间 consumer 列表不变 |
| 多 Consumer 的 PromptEvent 响应冲突 | 设计中 | 约定：同一时刻只有一个 Consumer 会 respond |

---

## 附录 A: 设计依据

### 前沿研究与实现对标

| 设计决策 | 依据 |
|---------|------|
| 统一 EventStream（CLI 也走事件流） | Bazel BEP / OpenTofu / Codex CLI / Nushell 共识：终端是消费者之一 |
| Observer/Listener 模式 | Gradle 事件模型；2-3 consumer 场景下虚函数开销可忽略 |
| MCP 在边界不在内部 | Claude Code / OpenHands / LangGraph 均不在内部使用 MCP |
| 意图优先的能力分层 | MCP tool design 最佳实践：语义自明、最小决策空间 |
| prompt 事件 + respond 通道 | OpenHands V1 SDK event-stream（arXiv 2511.03690） |
| 组件化分层 | arXiv 2512.09458 "Architectures for Building Agentic AI" |
| 三层安全防线 | OWASP AI Agent Security Top 10 2026 |
| 薄 Agent Loop | Claude Agent SDK + OpenAI Agents SDK 共识 |
| trust_level 可配置 | Claude Code sandboxing 分级权限 |

### 参考实现

| 项目 | 关联 |
|------|------|
| Bazel BEP | 事件协议设计、DAG 事件结构、多消费者模型 |
| OpenTofu `-json-into` | 双输出流（人类+机器）同时消费 |
| OpenAI Codex CLI | TUI/headless/JSON-RPC 三模式共享 core |
| Claude Agent SDK | Agent Loop、MCP 工具集成 |
| OpenHands V1 SDK | event-stream 架构、Action/Observation 分离 |
| Semantic Kernel | Plugin Registry、Capability 发现 |
| MCP 2025-11-25 Spec | tool annotations、elicitation、structured output |
