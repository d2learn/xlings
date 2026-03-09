# xlings Agent 基础架构实施计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 实现 EventStream + Capability + TaskRuntime 三层基础架构，使 xlings Core 能力可被 TUI/Agent/MCP/Desktop 四种前端统一消费。

**Architecture:** Core 模块通过 EventStream 发射结构化事件，不直接做 I/O。TuiConsumer/AgentConsumer 等消费者订阅事件流，各自实现渲染/响应逻辑。Capability 层将 Core 操作封装为可注册、可发现的能力单元。TaskRuntime 为 Agent/MCP 提供异步并发执行。

**Tech Stack:** C++23 modules (.cppm), GCC 15, xmake, gtest 1.15.2, ftxui 6.1.9

**Scope:** 本计划聚焦底层架构（EventStream / Capability / TaskRuntime）。Agent 内部细节（LLM 集成、记忆、skills、决策）后续单独计划。

---

## 现有代码关键发现

实施前必须了解的现状：

| 模块 | 现状 | 改造点 |
|------|------|--------|
| `core/xim/installer.cppm:683` | 已有 `onStatus` 回调 (`InstallStatus{name, phase, progress, message}`) | 天然适配 EventStream |
| `core/xim/downloader.cppm:322` | 已有 `onProgress` 回调 | 同上 |
| `core/log.cppm` | 全局函数 `log::info/warn/error` 直接写 stdout/stderr | 需包装为 EventStream 发射 |
| `core/ui/progress.cppm` | `Phase` 枚举 + `StatusEntry` 结构 + ftxui 渲染 | TuiConsumer 内部复用 |
| `core/cli.cppm:445-551` | `cmdline::App` 子命令注册模式 | Capability 注册对齐此模式 |
| `core/xim/libxpkg/types/type.cppm:13-22` | `InstallPhase` 枚举已有 Pending/Downloading/.../Done/Failed | 可复用为 EventStream Phase |

---

## Task 1: Event 类型定义模块

**Files:**
- Create: `core/event.cppm`
- Test: `tests/unit/test_main.cpp` (追加 Event 测试)

**Step 1: 写测试**

在 `tests/unit/test_main.cpp` 末尾追加：

```cpp
// ═══════════════════════════════════════════════════════════════
//  EventStream tests
// ═══════════════════════════════════════════════════════════════

import xlings.event;

TEST(Event, ProgressEventConstruction) {
    xlings::ProgressEvent e{
        .phase = xlings::Phase::downloading,
        .percent = 0.5f,
        .message = "Downloading gcc-15..."
    };
    EXPECT_EQ(e.phase, xlings::Phase::downloading);
    EXPECT_FLOAT_EQ(e.percent, 0.5f);
    EXPECT_EQ(e.message, "Downloading gcc-15...");
}

TEST(Event, PromptEventConstruction) {
    xlings::PromptEvent e{
        .id = "p1",
        .question = "Override existing?",
        .options = {"y", "n"},
        .default_value = "n"
    };
    EXPECT_EQ(e.id, "p1");
    EXPECT_EQ(e.options.size(), 2);
    EXPECT_EQ(e.default_value, "n");
}

TEST(Event, VariantHoldsTypes) {
    xlings::Event ev = xlings::LogEvent{xlings::LogLevel::info, "hello"};
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(ev));

    ev = xlings::ErrorEvent{.code = 42, .message = "fail", .recoverable = true};
    auto& err = std::get<xlings::ErrorEvent>(ev);
    EXPECT_EQ(err.code, 42);
    EXPECT_TRUE(err.recoverable);
}

TEST(Event, CompletedEvent) {
    xlings::Event ev = xlings::CompletedEvent{.success = true, .summary = "done"};
    auto& c = std::get<xlings::CompletedEvent>(ev);
    EXPECT_TRUE(c.success);
}

TEST(Event, DataEvent) {
    xlings::Event ev = xlings::DataEvent{.kind = "search_results", .json = R"({"count":3})"};
    auto& d = std::get<xlings::DataEvent>(ev);
    EXPECT_EQ(d.kind, "search_results");
}
```

**Step 2: 运行测试确认失败**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Event.*"
```

Expected: 编译失败 — `xlings.event` 模块不存在

**Step 3: 实现 event 模块**

创建 `core/event.cppm`：

```cpp
module;

#include <string>
#include <variant>
#include <vector>

export module xlings.event;

namespace xlings {

// ─── Phase ───
export enum class Phase {
    resolving,
    downloading,
    extracting,
    installing,
    configuring,
    verifying
};

// ─── LogLevel ───
export enum class LogLevel { debug, info, warn, error };

// ─── Event Types ───

export struct ProgressEvent {
    Phase phase;
    float percent;           // 0.0 ~ 1.0
    std::string message;
};

export struct LogEvent {
    LogLevel level;
    std::string message;
};

export struct PromptEvent {
    std::string id;
    std::string question;
    std::vector<std::string> options;    // 空 = 自由输入
    std::string default_value;
};

export struct ErrorEvent {
    int code;
    std::string message;
    bool recoverable = false;
};

export struct DataEvent {
    std::string kind;        // "search_results" / "package_info" 等
    std::string json;
};

export struct CompletedEvent {
    bool success;
    std::string summary;
};

// ─── 统一事件类型 ───
export using Event = std::variant<
    ProgressEvent,
    LogEvent,
    PromptEvent,
    ErrorEvent,
    DataEvent,
    CompletedEvent
>;

}  // namespace xlings
```

**Step 4: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Event.*"
```

Expected: 5 tests PASS

**Step 5: 提交**

```bash
git add core/event.cppm tests/unit/test_main.cpp
git commit -m "feat: add Event type definitions module (xlings.event)"
```

---

## Task 2: EventStream 核心模块

**Files:**
- Create: `core/event_stream.cppm`
- Modify: `tests/unit/test_main.cpp` (追加 EventStream 测试)

**Step 1: 写测试**

```cpp
import xlings.event_stream;

TEST(EventStream, EmitAndConsume) {
    xlings::EventStream stream;
    std::vector<xlings::Event> received;

    stream.on_event([&](const xlings::Event& e) {
        received.push_back(e);
    });

    stream.emit(xlings::LogEvent{xlings::LogLevel::info, "hello"});
    stream.emit(xlings::ProgressEvent{xlings::Phase::downloading, 0.5f, "..."});

    ASSERT_EQ(received.size(), 2);
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(received[0]));
    EXPECT_TRUE(std::holds_alternative<xlings::ProgressEvent>(received[1]));
}

TEST(EventStream, MultipleConsumers) {
    xlings::EventStream stream;
    int count_a = 0, count_b = 0;

    stream.on_event([&](const xlings::Event&) { ++count_a; });
    stream.on_event([&](const xlings::Event&) { ++count_b; });

    stream.emit(xlings::LogEvent{xlings::LogLevel::info, "test"});

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
}

TEST(EventStream, PromptAndRespond) {
    xlings::EventStream stream;
    std::string captured_question;

    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            captured_question = p->question;
            // 模拟消费者异步响应
            stream.respond(p->id, "y");
        }
    });

    // prompt() 发射事件并阻塞等待 respond()
    auto answer = stream.prompt({
        .id = "p1",
        .question = "Override?",
        .options = {"y", "n"},
        .default_value = "n"
    });

    EXPECT_EQ(captured_question, "Override?");
    EXPECT_EQ(answer, "y");
}

TEST(EventStream, PromptDefaultOnEmpty) {
    // 测试：如果没有 consumer 响应，prompt 应能超时或使用默认值
    // 初期实现：同步 consumer 模式下总是有人响应
    // 此测试验证正常流程
    xlings::EventStream stream;

    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, p->default_value);
        }
    });

    auto answer = stream.prompt({
        .id = "p2",
        .question = "Continue?",
        .options = {},
        .default_value = "yes"
    });
    EXPECT_EQ(answer, "yes");
}
```

**Step 2: 运行测试确认失败**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="EventStream.*"
```

Expected: 编译失败

**Step 3: 实现 EventStream**

创建 `core/event_stream.cppm`：

```cpp
module;

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

export module xlings.event_stream;

import xlings.event;

namespace xlings {

export using EventConsumer = std::function<void(const Event&)>;

export class EventStream {
public:
    void on_event(EventConsumer consumer) {
        consumers_.push_back(std::move(consumer));
    }

    void emit(Event event) {
        for (auto& consumer : consumers_) {
            consumer(event);
        }
    }

    auto prompt(PromptEvent req) -> std::string {
        auto id = req.id;

        // 广播 PromptEvent
        emit(Event{std::move(req)});

        // 阻塞等待响应
        std::unique_lock lock(prompt_mutex_);
        prompt_cv_.wait(lock, [&] {
            return prompt_responses_.contains(id);
        });

        auto response = std::move(prompt_responses_[id]);
        prompt_responses_.erase(id);
        return response;
    }

    void respond(std::string_view prompt_id, std::string_view response) {
        {
            std::lock_guard lock(prompt_mutex_);
            prompt_responses_[std::string(prompt_id)] = std::string(response);
        }
        prompt_cv_.notify_all();
    }

private:
    std::vector<EventConsumer> consumers_;
    std::mutex prompt_mutex_;
    std::condition_variable prompt_cv_;
    std::unordered_map<std::string, std::string> prompt_responses_;
};

}  // namespace xlings
```

**Step 4: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="EventStream.*"
```

Expected: 4 tests PASS

**Step 5: 提交**

```bash
git add core/event_stream.cppm tests/unit/test_main.cpp
git commit -m "feat: add EventStream with emit/prompt/respond (xlings.event_stream)"
```

---

## Task 3: EventStream 多线程 prompt 测试

验证 prompt 在 Task 线程和主线程之间的阻塞/唤醒机制。

**Files:**
- Modify: `tests/unit/test_main.cpp`

**Step 1: 写测试**

```cpp
#include <thread>
#include <chrono>

TEST(EventStream, PromptBlocksUntilRespond) {
    xlings::EventStream stream;
    std::atomic<bool> prompt_returned{false};
    std::string answer;

    // 不在 on_event 中 respond——模拟 Agent 异步响应
    stream.on_event([](const xlings::Event&) {
        // consumer 只记录，不响应
    });

    // 在另一个线程中调用 prompt（模拟 Task 线程）
    std::thread task_thread([&] {
        answer = stream.prompt({
            .id = "p_async",
            .question = "Confirm?",
            .options = {"y", "n"},
            .default_value = "n"
        });
        prompt_returned.store(true);
    });

    // 主线程等一小段时间，确认 prompt 还在阻塞
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(prompt_returned.load());

    // 主线程注入响应（模拟 Agent 主循环）
    stream.respond("p_async", "confirmed");

    task_thread.join();
    EXPECT_TRUE(prompt_returned.load());
    EXPECT_EQ(answer, "confirmed");
}

TEST(EventStream, ConcurrentPromptsFromMultipleTasks) {
    xlings::EventStream stream;
    std::string answer1, answer2;

    stream.on_event([](const xlings::Event&) {});

    std::thread t1([&] {
        answer1 = stream.prompt({.id = "pa", .question = "Q1"});
    });
    std::thread t2([&] {
        answer2 = stream.prompt({.id = "pb", .question = "Q2"});
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    stream.respond("pb", "ans_b");
    stream.respond("pa", "ans_a");

    t1.join();
    t2.join();

    EXPECT_EQ(answer1, "ans_a");
    EXPECT_EQ(answer2, "ans_b");
}
```

**Step 2: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="EventStream.*"
```

Expected: 6 tests PASS (4 from Task 2 + 2 new)

**Step 3: 提交**

```bash
git add tests/unit/test_main.cpp
git commit -m "test: add multi-threaded prompt tests for EventStream"
```

---

## Task 4: Capability 接口 + Registry 模块

**Files:**
- Create: `core/capability.cppm`
- Modify: `tests/unit/test_main.cpp`

**Step 1: 写测试**

```cpp
import xlings.capability;

// 测试用的简单 Capability 实现
namespace {

class MockSearchCapability : public xlings::capability::Capability {
public:
    auto spec() const -> xlings::capability::CapabilitySpec override {
        return {
            .name = "search_packages",
            .description = "Search for packages",
            .input_schema = R"({"type":"object","properties":{"query":{"type":"string"}}})",
            .output_schema = R"({"type":"object","properties":{"results":{"type":"array"}}})",
            .destructive = false,
            .async_capable = true
        };
    }

    auto execute(xlings::capability::Params params,
                 xlings::EventStream& stream) -> xlings::capability::Result override {
        stream.emit(xlings::LogEvent{xlings::LogLevel::info, "Searching..."});
        stream.emit(xlings::DataEvent{.kind = "search_results", .json = R"({"results":["gcc","g++"]})"});
        stream.emit(xlings::CompletedEvent{.success = true, .summary = "Found 2 packages"});
        return R"({"count":2})";
    }
};

class MockInstallCapability : public xlings::capability::Capability {
public:
    auto spec() const -> xlings::capability::CapabilitySpec override {
        return {
            .name = "install_package",
            .description = "Install a package",
            .input_schema = R"({"type":"object","properties":{"name":{"type":"string"}}})",
            .output_schema = R"({"type":"object","properties":{"status":{"type":"string"}}})",
            .destructive = true,
            .async_capable = true
        };
    }

    auto execute(xlings::capability::Params params,
                 xlings::EventStream& stream) -> xlings::capability::Result override {
        stream.emit(xlings::ProgressEvent{xlings::Phase::installing, 0.5f, "Installing..."});
        auto answer = stream.prompt({
            .id = "confirm_install",
            .question = "Proceed with install?",
            .options = {"y", "n"},
            .default_value = "y"
        });
        if (answer == "n") {
            return R"({"status":"cancelled"})";
        }
        stream.emit(xlings::CompletedEvent{.success = true, .summary = "Installed"});
        return R"({"status":"ok"})";
    }
};

}  // anonymous namespace

TEST(Capability, RegistryRegisterAndGet) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());
    reg.register_capability(std::make_unique<MockInstallCapability>());

    auto* search = reg.get("search_packages");
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->spec().name, "search_packages");
    EXPECT_FALSE(search->spec().destructive);

    auto* install = reg.get("install_package");
    ASSERT_NE(install, nullptr);
    EXPECT_TRUE(install->spec().destructive);

    EXPECT_EQ(reg.get("nonexistent"), nullptr);
}

TEST(Capability, RegistryListAll) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());
    reg.register_capability(std::make_unique<MockInstallCapability>());

    auto specs = reg.list_all();
    EXPECT_EQ(specs.size(), 2);
}

TEST(Capability, ExecuteWithEventStream) {
    xlings::EventStream stream;
    std::vector<xlings::Event> events;
    stream.on_event([&](const xlings::Event& e) { events.push_back(e); });

    MockSearchCapability search;
    auto result = search.execute(R"({"query":"gcc"})", stream);

    // 验证事件流
    ASSERT_EQ(events.size(), 3);
    EXPECT_TRUE(std::holds_alternative<xlings::LogEvent>(events[0]));
    EXPECT_TRUE(std::holds_alternative<xlings::DataEvent>(events[1]));
    EXPECT_TRUE(std::holds_alternative<xlings::CompletedEvent>(events[2]));
    EXPECT_EQ(result, R"({"count":2})");
}

TEST(Capability, ExecuteWithPrompt) {
    xlings::EventStream stream;

    // 自动响应 prompt
    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, "y");
        }
    });

    MockInstallCapability install;
    auto result = install.execute(R"({"name":"gcc"})", stream);
    EXPECT_EQ(result, R"({"status":"ok"})");
}

TEST(Capability, ExecutePromptCancelled) {
    xlings::EventStream stream;

    stream.on_event([&](const xlings::Event& e) {
        if (auto* p = std::get_if<xlings::PromptEvent>(&e)) {
            stream.respond(p->id, "n");
        }
    });

    MockInstallCapability install;
    auto result = install.execute(R"({"name":"gcc"})", stream);
    EXPECT_EQ(result, R"({"status":"cancelled"})");
}
```

**Step 2: 运行测试确认失败**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Capability.*"
```

Expected: 编译失败

**Step 3: 实现 Capability + Registry**

创建 `core/capability.cppm`：

```cpp
module;

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

export module xlings.capability;

import xlings.event;
import xlings.event_stream;

namespace xlings::capability {

export using Params = std::string;
export using Result = std::string;

export struct CapabilitySpec {
    std::string name;
    std::string description;
    std::string input_schema;
    std::string output_schema;
    bool destructive = false;
    bool async_capable = true;
};

export struct Capability {
    virtual ~Capability() = default;
    virtual auto spec() const -> CapabilitySpec = 0;
    virtual auto execute(Params params, EventStream& stream) -> Result = 0;
};

export class Registry {
public:
    void register_capability(std::unique_ptr<Capability> cap) {
        auto name = cap->spec().name;
        capabilities_[std::move(name)] = std::move(cap);
    }

    auto get(std::string_view name) -> Capability* {
        auto it = capabilities_.find(std::string(name));
        return it != capabilities_.end() ? it->second.get() : nullptr;
    }

    auto list_all() -> std::vector<CapabilitySpec> {
        std::vector<CapabilitySpec> specs;
        specs.reserve(capabilities_.size());
        for (auto& [_, cap] : capabilities_) {
            specs.push_back(cap->spec());
        }
        return specs;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Capability>> capabilities_;
};

}  // namespace xlings::capability
```

**Step 4: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Capability.*"
```

Expected: 5 tests PASS

**Step 5: 提交**

```bash
git add core/capability.cppm tests/unit/test_main.cpp
git commit -m "feat: add Capability interface and Registry (xlings.capability)"
```

---

## Task 5: TaskManager 模块

**Files:**
- Create: `core/task.cppm`
- Modify: `tests/unit/test_main.cpp`

**Step 1: 写测试**

```cpp
import xlings.task;

TEST(TaskManager, SubmitAndComplete) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm(reg);
    auto tid = tm.submit("search_packages", R"({"query":"gcc"})");

    EXPECT_FALSE(tid.empty());

    // 等待任务完成（search 是瞬时的）
    for (int i = 0; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto info = tm.info(tid);
    EXPECT_EQ(info.status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(info.capability_name, "search_packages");
}

TEST(TaskManager, EventsRetrieval) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm(reg);
    auto tid = tm.submit("search_packages", R"({"query":"gcc"})");

    // 等待完成
    for (int i = 0; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto events = tm.events(tid);
    EXPECT_GE(events.size(), 3);  // LogEvent + DataEvent + CompletedEvent

    // 增量拉取
    auto events2 = tm.events(tid, events.size());
    EXPECT_EQ(events2.size(), 0);
}

TEST(TaskManager, PromptHandling) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockInstallCapability>());

    xlings::task::TaskManager tm(reg);
    auto tid = tm.submit("install_package", R"({"name":"gcc"})");

    // 轮询直到出现 prompt
    bool found_prompt = false;
    std::string prompt_id;
    for (int i = 0; i < 100; ++i) {
        auto info = tm.info(tid);
        if (info.status == xlings::task::TaskStatus::waiting_prompt) {
            auto evts = tm.events(tid);
            for (auto& rec : evts) {
                if (auto* p = std::get_if<xlings::PromptEvent>(&rec.event)) {
                    prompt_id = p->id;
                    found_prompt = true;
                }
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(found_prompt);

    // 响应 prompt
    tm.respond(tid, prompt_id, "y");

    // 等待任务完成
    for (int i = 0; i < 100; ++i) {
        if (tm.info(tid).status == xlings::task::TaskStatus::completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(tm.info(tid).status, xlings::task::TaskStatus::completed);
}

TEST(TaskManager, ConcurrentTasks) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm(reg);
    auto t1 = tm.submit("search_packages", R"({"query":"gcc"})");
    auto t2 = tm.submit("search_packages", R"({"query":"cmake"})");
    auto t3 = tm.submit("search_packages", R"({"query":"ninja"})");

    // 等待全部完成
    for (int i = 0; i < 200; ++i) {
        if (!tm.has_active_tasks()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(tm.has_active_tasks());
    EXPECT_EQ(tm.info(t1).status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(tm.info(t2).status, xlings::task::TaskStatus::completed);
    EXPECT_EQ(tm.info(t3).status, xlings::task::TaskStatus::completed);
}

TEST(TaskManager, StatusAll) {
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm(reg);
    tm.submit("search_packages", R"({})");
    tm.submit("search_packages", R"({})");

    auto all = tm.info_all();
    EXPECT_EQ(all.size(), 2);
}
```

**Step 2: 运行测试确认失败**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="TaskManager.*"
```

Expected: 编译失败

**Step 3: 实现 TaskManager**

创建 `core/task.cppm`：

```cpp
module;

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

export module xlings.task;

import xlings.event;
import xlings.event_stream;
import xlings.capability;

namespace xlings::task {

export using TaskId = std::string;

export enum class TaskStatus {
    pending,
    running,
    waiting_prompt,
    completed,
    failed,
    cancelled
};

export struct EventRecord {
    std::size_t index;
    Event event;
};

export struct TaskInfo {
    TaskId id;
    std::string capability_name;
    TaskStatus status;
    float progress_pct = 0.0f;
    std::string current_phase;
    std::size_t event_count = 0;
    std::size_t pending_prompt_count = 0;
};

// ─── Internal Task State ───
struct TaskState {
    TaskId id;
    std::string capability_name;
    std::atomic<TaskStatus> status{TaskStatus::pending};
    std::atomic<float> progress_pct{0.0f};
    std::string current_phase;

    EventStream stream;
    std::vector<EventRecord> event_buffer;
    std::mutex buffer_mutex;

    std::size_t pending_prompt_count = 0;

    std::thread thread;
};

export class TaskManager {
public:
    explicit TaskManager(capability::Registry& registry)
        : registry_(registry) {}

    ~TaskManager() {
        // 等待所有任务线程结束
        for (auto& [_, state] : tasks_) {
            if (state->thread.joinable()) {
                state->thread.join();
            }
        }
    }

    auto submit(std::string_view capability_name, capability::Params params) -> TaskId {
        auto* cap = registry_.get(capability_name);
        if (!cap) return "";

        auto id = generate_id_();
        auto state = std::make_shared<TaskState>();
        state->id = id;
        state->capability_name = std::string(capability_name);
        state->status.store(TaskStatus::pending);

        // 注册 AgentConsumer：缓冲事件 + 追踪状态
        state->stream.on_event([s = state.get()](const Event& e) {
            {
                std::lock_guard lock(s->buffer_mutex);
                s->event_buffer.push_back(EventRecord{
                    .index = s->event_buffer.size(),
                    .event = e
                });
            }

            // 追踪进度
            if (auto* p = std::get_if<ProgressEvent>(&e)) {
                s->progress_pct.store(p->percent);
            }
            // 追踪 prompt
            if (std::holds_alternative<PromptEvent>(e)) {
                std::lock_guard lock(s->buffer_mutex);
                s->pending_prompt_count++;
                s->status.store(TaskStatus::waiting_prompt);
            }
        });

        // 在新线程中执行
        auto params_copy = std::string(params);
        state->thread = std::thread([cap, params_copy, s = state.get()]() {
            s->status.store(TaskStatus::running);
            try {
                cap->execute(params_copy, s->stream);
                s->status.store(TaskStatus::completed);
            } catch (...) {
                s->status.store(TaskStatus::failed);
            }
        });

        std::lock_guard lock(tasks_mutex_);
        tasks_[id] = std::move(state);
        return id;
    }

    void cancel(TaskId id) {
        std::lock_guard lock(tasks_mutex_);
        auto it = tasks_.find(id);
        if (it != tasks_.end()) {
            it->second->status.store(TaskStatus::cancelled);
        }
    }

    auto info(TaskId id) -> TaskInfo {
        std::lock_guard lock(tasks_mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return {};

        auto& s = it->second;
        std::lock_guard buf_lock(s->buffer_mutex);
        return TaskInfo{
            .id = s->id,
            .capability_name = s->capability_name,
            .status = s->status.load(),
            .progress_pct = s->progress_pct.load(),
            .event_count = s->event_buffer.size(),
            .pending_prompt_count = s->pending_prompt_count
        };
    }

    auto info_all() -> std::vector<TaskInfo> {
        std::vector<TaskInfo> result;
        std::lock_guard lock(tasks_mutex_);
        for (auto& [id, _] : tasks_) {
            result.push_back(info_unlocked_(id));
        }
        return result;
    }

    bool has_active_tasks() const {
        std::lock_guard lock(tasks_mutex_);
        for (auto& [_, s] : tasks_) {
            auto st = s->status.load();
            if (st == TaskStatus::pending || st == TaskStatus::running
                || st == TaskStatus::waiting_prompt) {
                return true;
            }
        }
        return false;
    }

    auto events(TaskId id, std::size_t since_index = 0) -> std::vector<EventRecord> {
        std::lock_guard lock(tasks_mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return {};

        auto& s = it->second;
        std::lock_guard buf_lock(s->buffer_mutex);

        if (since_index >= s->event_buffer.size()) return {};
        return {s->event_buffer.begin() + since_index, s->event_buffer.end()};
    }

    void respond(TaskId task_id, std::string_view prompt_id, std::string_view response) {
        std::lock_guard lock(tasks_mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) return;

        auto& s = it->second;
        {
            std::lock_guard buf_lock(s->buffer_mutex);
            if (s->pending_prompt_count > 0) s->pending_prompt_count--;
        }
        s->stream.respond(prompt_id, response);
        // 如果没有更多 pending prompt，恢复 running 状态
        {
            std::lock_guard buf_lock(s->buffer_mutex);
            if (s->pending_prompt_count == 0
                && s->status.load() == TaskStatus::waiting_prompt) {
                s->status.store(TaskStatus::running);
            }
        }
    }

private:
    capability::Registry& registry_;
    std::unordered_map<std::string, std::shared_ptr<TaskState>> tasks_;
    mutable std::mutex tasks_mutex_;
    std::atomic<int> next_id_{1};

    auto generate_id_() -> TaskId {
        return "task_" + std::to_string(next_id_.fetch_add(1));
    }

    auto info_unlocked_(const TaskId& id) -> TaskInfo {
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return {};
        auto& s = it->second;
        std::lock_guard buf_lock(s->buffer_mutex);
        return TaskInfo{
            .id = s->id,
            .capability_name = s->capability_name,
            .status = s->status.load(),
            .progress_pct = s->progress_pct.load(),
            .event_count = s->event_buffer.size(),
            .pending_prompt_count = s->pending_prompt_count
        };
    }
};

}  // namespace xlings::task
```

**Step 4: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="TaskManager.*"
```

Expected: 5 tests PASS

**Step 5: 提交**

```bash
git add core/task.cppm tests/unit/test_main.cpp
git commit -m "feat: add TaskManager with async execution and prompt handling (xlings.task)"
```

---

## Task 6: 集成验证 — 端到端场景测试

验证三层（EventStream + Capability + TaskManager）协同工作的完整场景。

**Files:**
- Modify: `tests/unit/test_main.cpp`

**Step 1: 写测试**

```cpp
// ═══════════════════════════════════════════════════════════════
//  Integration: EventStream + Capability + TaskManager
// ═══════════════════════════════════════════════════════════════

TEST(Integration, TuiPathSynchronous) {
    // 模拟 CLI/TUI 路径：同步调用，Consumer 直接处理
    xlings::EventStream stream;
    std::vector<std::string> rendered;

    stream.on_event([&](const xlings::Event& e) {
        std::visit([&](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, xlings::ProgressEvent>) {
                rendered.push_back("progress:" + std::to_string(ev.percent));
            } else if constexpr (std::is_same_v<T, xlings::LogEvent>) {
                rendered.push_back("log:" + ev.message);
            } else if constexpr (std::is_same_v<T, xlings::PromptEvent>) {
                rendered.push_back("prompt:" + ev.question);
                stream.respond(ev.id, "y");
            } else if constexpr (std::is_same_v<T, xlings::DataEvent>) {
                rendered.push_back("data:" + ev.kind);
            } else if constexpr (std::is_same_v<T, xlings::CompletedEvent>) {
                rendered.push_back("completed:" + ev.summary);
            }
        }, e);
    });

    MockSearchCapability search;
    search.execute(R"({})", stream);

    ASSERT_EQ(rendered.size(), 3);
    EXPECT_EQ(rendered[0], "log:Searching...");
    EXPECT_EQ(rendered[1], "data:search_results");
    EXPECT_EQ(rendered[2], "completed:Found 2 packages");
}

TEST(Integration, AgentPathConcurrentWithPrompt) {
    // 模拟 Agent 路径：并发任务 + prompt 处理
    xlings::capability::Registry reg;
    reg.register_capability(std::make_unique<MockInstallCapability>());
    reg.register_capability(std::make_unique<MockSearchCapability>());

    xlings::task::TaskManager tm(reg);

    // 并发提交
    auto t_search = tm.submit("search_packages", R"({})");
    auto t_install = tm.submit("install_package", R"({"name":"gcc"})");

    // Agent 主循环：轮询事件，处理 prompt
    bool install_done = false;
    for (int i = 0; i < 200 && !install_done; ++i) {
        // 检查 install 任务的 prompt
        auto install_info = tm.info(t_install);
        if (install_info.status == xlings::task::TaskStatus::waiting_prompt) {
            auto evts = tm.events(t_install);
            for (auto& rec : evts) {
                if (auto* p = std::get_if<xlings::PromptEvent>(&rec.event)) {
                    tm.respond(t_install, p->id, "y");
                }
            }
        }
        if (install_info.status == xlings::task::TaskStatus::completed) {
            install_done = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(install_done);
    EXPECT_EQ(tm.info(t_search).status, xlings::task::TaskStatus::completed);

    // 验证事件流内容
    auto search_events = tm.events(t_search);
    EXPECT_GE(search_events.size(), 3);

    auto install_events = tm.events(t_install);
    EXPECT_GE(install_events.size(), 2);  // ProgressEvent + PromptEvent + CompletedEvent
}
```

**Step 2: 运行测试确认通过**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Integration.*"
```

Expected: 2 tests PASS

**Step 3: 运行全部新增测试**

```bash
xmake build xlings_tests && xmake run xlings_tests --gtest_filter="Event.*:EventStream.*:Capability.*:TaskManager.*:Integration.*"
```

Expected: 全部 PASS（约 17 个测试）

**Step 4: 提交**

```bash
git add tests/unit/test_main.cpp
git commit -m "test: add integration tests for EventStream + Capability + TaskManager"
```

---

## Task 7: 验证现有测试不被破坏

新增模块不应影响现有代码（Core 模块尚未改造）。

**Step 1: 运行全部单元测试**

```bash
xmake build xlings_tests && xmake run xlings_tests
```

Expected: 所有现有测试 + 新增测试全部 PASS

**Step 2: 构建主二进制**

```bash
xmake build xlings
```

Expected: 编译成功，新模块被编译但未被 cli.cppm 引用（无功能影响）

**Step 3: 提交（如有 xmake.lua 调整）**

如果 xmake.lua 需要调整以包含新 .cppm 文件（通常 `core/**.cppm` glob 已覆盖），则：

```bash
git add xmake.lua
git commit -m "build: ensure new event/capability/task modules are included"
```

---

## 后续 Tasks（本轮不实施，记录待办）

以下任务在基础架构验证通过后单独计划：

### Phase 2: Core 改造（TuiConsumer 对接）
- Task 8: 实现 TuiConsumer（包装现有 ftxui/theme 渲染）
- Task 9: 改造 `xim/installer.cppm` — onStatus 回调 → EventStream 发射
- Task 10: 改造 `xim/downloader.cppm` — onProgress 回调 → EventStream 发射
- Task 11: 改造 `cli.cppm` — 命令入口创建 EventStream + 注册 TuiConsumer
- Task 12: 改造 `log.cppm` — 提供 EventStream 转发模式

### Phase 3: Capability 实现
- Task 13: 实现 `search_packages` 真实 Capability（包装 xim::cmd_search）
- Task 14: 实现 `install_package` 真实 Capability（包装 xim::cmd_install）
- Task 15: 实现 `get_system_status` Capability
- Task 16: CLI 命令通过 Capability Registry 分发

### Phase 4: Agent Runtime
- Task 17: `xlings agent` CLI 子命令注册
- Task 18: Agent Loop 骨架（LLM client 集成）
- Task 19: Agent 子命令：`xlings agent setup`

### Phase 5: MCP Server
- Task 20: CapabilitySpec → MCP tool schema 转换
- Task 21: `xlings mcp-server` stdio 入口
