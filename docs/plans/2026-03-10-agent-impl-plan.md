# xlings Agent 实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 实现 xlings agent 模块——LLM 驱动的包管理/环境构建 agent，从 MVP 到完整功能分 6 个 Phase 交付。

**Architecture:** Agent 是 EventStream 的消费者。LLM 通过 ToolBridge 调用 Capability Registry 中的工具，工具执行产生事件流。`.agents/` 目录存储身份、配置、记忆、会话。通用基础设施模块放 `src/libs/` 下（未来独立发布 mcpplibs）。

**Tech Stack:** C++23 modules (.cppm), GCC 15, xmake, llmapi (mcpplibs), ftxui 6.1.9, gtest 1.15.2, nlohmann/json

---

## 现有代码基础

| 组件 | 状态 | 文件 |
|------|------|------|
| Event 系统 | ✅ 完成 | `src/runtime/event.cppm`, `event_stream.cppm` |
| Capability 接口 + Registry | ✅ 完成 | `src/runtime/capability.cppm` |
| 8 个 Capability 实现 | ✅ 完成 | `src/capabilities.cppm` |
| TaskManager | ✅ 完成 | `src/runtime/task.cppm` |
| CLI 路由 | ✅ 完成 | `src/cli.cppm`（无 agent 子命令） |
| llmapi | ✅ 外部可用 | `/home/speak/workspace/github/mcpplibs/llmapi` |
| Agent 模块 | ❌ 未开始 | - |

### llmapi 关键接口（实现时参考）

```cpp
// types: ToolDef{name, description, inputSchema}, ToolCall{id, name, arguments}
//        ToolUseContent{id, name, inputJson}, ToolResultContent{toolUseId, content, isError}
//        StopReason::ToolUse / EndOfTurn
//        ChatResponse::tool_calls() -> vector<ToolCall>
//        Conversation{messages, push(), save(), load()}

// client: Client<P>(config).system(prompt).chat_stream(msg, callback) -> ChatResponse
//         Client manages conversation internally
//         For tool loop: use provider_.chat() directly + manage conversation manually
```

---

## Phase 1: Agent MVP（基础 Loop + CLI 入口）

目标：`xlings agent` 可以与 LLM 对话，调用 xlings 工具，终端流式输出。

### Task 1: 添加 llmapi 依赖

**Files:**
- Modify: `xmake.lua`

**Step 1: 修改 xmake.lua**

在 `add_requires` 区域追加：

```lua
add_requires("mcpplibs-llmapi", {system = false})
```

在 `target("xlings")` 的 `add_packages` 行追加 `"mcpplibs-llmapi"`。
在 `target("xlings_tests")` 的 `add_packages` 行同样追加。

> **注意**: 如果 mcpplibs-index 尚未收录 llmapi，需要用本地路径：
> ```lua
> add_requires("mcpplibs-llmapi", {system = false, configs = {path = "/home/speak/workspace/github/mcpplibs/llmapi"}})
> ```
> 或使用 xmake 的 `includes` + `add_deps` 模式引用本地包。具体方式需要检查 mcpplibs-index 是否已有 llmapi 条目。

**Step 2: 验证构建**

Run: `xmake f -c && xmake build xlings`
Expected: 编译成功，无新增错误

**Step 3: Commit**

```bash
git add xmake.lua
git commit -m "build: add llmapi dependency for agent module"
```

---

### Task 2: LLM 配置模块

**Files:**
- Create: `src/agent/llm_config.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

**Step 1: 写测试**

```cpp
// ═══════════════════════════════════════════════════════════════
//  Agent: LLM Config
// ═══════════════════════════════════════════════════════════════

TEST(AgentLlmConfig, InferProviderAnthropic) {
    EXPECT_EQ(xlings::agent::infer_provider("claude-sonnet-4-20250514"), "anthropic");
    EXPECT_EQ(xlings::agent::infer_provider("claude-opus-4-6"), "anthropic");
}

TEST(AgentLlmConfig, InferProviderOpenAI) {
    EXPECT_EQ(xlings::agent::infer_provider("gpt-4o"), "openai");
    EXPECT_EQ(xlings::agent::infer_provider("gpt-4-turbo"), "openai");
}

TEST(AgentLlmConfig, InferProviderDeepSeek) {
    EXPECT_EQ(xlings::agent::infer_provider("deepseek-chat"), "openai");
}

TEST(AgentLlmConfig, InferProviderUnknownDefaultsOpenAI) {
    EXPECT_EQ(xlings::agent::infer_provider("llama3"), "openai");
}

TEST(AgentLlmConfig, DefaultConfig) {
    auto cfg = xlings::agent::default_llm_config();
    EXPECT_FALSE(cfg.model.empty());
    EXPECT_GT(cfg.max_tokens, 0);
    EXPECT_GT(cfg.temperature, 0.0f);
}
```

**Step 2: 运行测试确认失败**

Run: `xmake build xlings_tests`
Expected: 编译失败，`xlings::agent::infer_provider` 未定义

**Step 3: 实现**

```cpp
// src/agent/llm_config.cppm
export module xlings.agent.llm_config;

import std;

namespace xlings::agent {

export struct LlmConfig {
    std::string provider;       // "anthropic" / "openai"
    std::string model;
    std::string api_key;
    std::string base_url;
    float temperature { 0.3f };
    int max_tokens { 8192 };
};

export auto infer_provider(std::string_view model) -> std::string {
    if (model.starts_with("claude"))    return "anthropic";
    if (model.starts_with("gpt"))       return "openai";
    if (model.starts_with("deepseek"))  return "openai";
    return "openai";  // 默认走 OpenAI 兼容协议
}

export auto default_llm_config() -> LlmConfig {
    return LlmConfig{
        .provider = "anthropic",
        .model = "claude-sonnet-4-20250514",
        .temperature = 0.3f,
        .max_tokens = 8192,
    };
}

// 三级优先级解析：flags > env > default
export auto resolve_llm_config(
    std::string_view flag_model,
    std::string_view flag_base_url
) -> LlmConfig {
    auto cfg = default_llm_config();

    // L3: 环境变量
    if (auto* v = std::getenv("XLINGS_LLM_MODEL"))    cfg.model = v;
    if (auto* v = std::getenv("XLINGS_LLM_BASE_URL")) cfg.base_url = v;

    // L2: 环境变量 API key（按 provider）
    cfg.provider = infer_provider(cfg.model);
    if (cfg.provider == "anthropic") {
        if (auto* v = std::getenv("ANTHROPIC_API_KEY")) cfg.api_key = v;
    } else {
        if (auto* v = std::getenv("OPENAI_API_KEY")) cfg.api_key = v;
    }

    // L1: CLI flags（最高优先级）
    if (!flag_model.empty()) {
        cfg.model = std::string(flag_model);
        cfg.provider = infer_provider(cfg.model);
    }
    if (!flag_base_url.empty()) cfg.base_url = std::string(flag_base_url);

    return cfg;
}

} // namespace xlings::agent
```

**Step 4: 运行测试确认通过**

Run: `xmake build xlings_tests && xmake run xlings_tests --gtest_filter='AgentLlmConfig.*'`
Expected: 5 tests PASS

**Step 5: Commit**

```bash
git add src/agent/llm_config.cppm tests/unit/test_main.cpp
git commit -m "feat(agent): add LLM config module with provider inference"
```

---

### Task 3: Tool Bridge 基础版

**Files:**
- Create: `src/agent/tool_bridge.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

**Step 1: 写测试**

```cpp
// ═══════════════════════════════════════════════════════════════
//  Agent: Tool Bridge
// ═══════════════════════════════════════════════════════════════

TEST(AgentToolBridge, CapabilityToToolDef) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);

    auto tools = bridge.tool_definitions();
    EXPECT_GE(tools.size(), 8u);

    // 验证每个 tool 都有 name, description, inputSchema
    for (const auto& tool : tools) {
        EXPECT_FALSE(tool.name.empty()) << "tool name is empty";
        EXPECT_FALSE(tool.description.empty()) << tool.name << " missing description";
        EXPECT_FALSE(tool.inputSchema.empty()) << tool.name << " missing schema";
    }
}

TEST(AgentToolBridge, ToolInfoLookup) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);

    auto info = bridge.tool_info("search_packages");
    EXPECT_EQ(info.name, "search_packages");
    EXPECT_EQ(info.source, "builtin");
    EXPECT_FALSE(info.destructive);
}

TEST(AgentToolBridge, ToolInfoDestructive) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);

    auto info = bridge.tool_info("install_packages");
    EXPECT_TRUE(info.destructive);
}

TEST(AgentToolBridge, ExecuteRoutes) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);

    // search_packages with empty keyword — should not crash
    xlings::EventStream stream;
    auto result = bridge.execute("search_packages", R"({"keyword":"__nonexistent__"})", stream);
    EXPECT_FALSE(result.content.empty());
}
```

**Step 2: 运行测试确认失败**

Run: `xmake build xlings_tests`
Expected: 编译失败

**Step 3: 实现**

```cpp
// src/agent/tool_bridge.cppm
export module xlings.agent.tool_bridge;

import std;
import xlings.runtime.capability;
import xlings.runtime.event_stream;

// llmapi types 需要手动定义兼容结构（避免循环依赖）
// 或直接 import mcpplibs.llmapi 并使用其 ToolDef/ToolResultContent

namespace xlings::agent {

export struct ToolDef {
    std::string name;
    std::string description;
    std::string inputSchema;
};

export struct ToolResult {
    std::string content;
    bool isError { false };
};

export struct ToolInfo {
    std::string name;
    std::string source;       // "builtin" or "pkg:<name>" or "mcp:<server>"
    bool destructive { false };
    int tier { 0 };           // 0=T0, 1=T1, 2=T2
};

export class ToolBridge {
    capability::Registry& registry_;

public:
    explicit ToolBridge(capability::Registry& registry) : registry_(registry) {}

    auto tool_definitions() const -> std::vector<ToolDef> {
        std::vector<ToolDef> tools;
        for (const auto& spec : registry_.list_all()) {
            tools.push_back(ToolDef{
                .name = spec.name,
                .description = spec.description,
                .inputSchema = spec.inputSchema,
            });
        }
        return tools;
    }

    auto execute(
        std::string_view name,
        std::string_view arguments,
        EventStream& stream
    ) -> ToolResult {
        auto* cap = registry_.get(name);
        if (!cap) {
            return ToolResult{
                .content = R"({"error":"unknown tool: )" + std::string(name) + R"("})",
                .isError = true,
            };
        }
        try {
            auto result = cap->execute(std::string(arguments), stream);
            return ToolResult{.content = result};
        } catch (const std::exception& e) {
            return ToolResult{
                .content = std::string(R"({"error":")") + e.what() + R"("})",
                .isError = true,
            };
        }
    }

    auto tool_info(std::string_view name) const -> ToolInfo {
        for (const auto& spec : registry_.list_all()) {
            if (spec.name == name) {
                return ToolInfo{
                    .name = spec.name,
                    .source = "builtin",
                    .destructive = spec.destructive,
                    .tier = 0,
                };
            }
        }
        return ToolInfo{.name = std::string(name), .source = "unknown"};
    }
};

} // namespace xlings::agent
```

**Step 4: 运行测试确认通过**

Run: `xmake build xlings_tests && xmake run xlings_tests --gtest_filter='AgentToolBridge.*'`
Expected: 4 tests PASS

**Step 5: Commit**

```bash
git add src/agent/tool_bridge.cppm tests/unit/test_main.cpp
git commit -m "feat(agent): add ToolBridge with Capability → ToolDef mapping"
```

---

### Task 4: Agent Loop 核心循环

**Files:**
- Create: `src/agent/loop.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

**Step 1: 写测试**

```cpp
// ═══════════════════════════════════════════════════════════════
//  Agent: Loop (unit-level, no real LLM call)
// ═══════════════════════════════════════════════════════════════

TEST(AgentLoop, BuildSystemPrompt) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);
    auto prompt = xlings::agent::build_system_prompt(bridge);

    // prompt 应包含工具列表
    EXPECT_NE(prompt.find("search_packages"), std::string::npos);
    EXPECT_NE(prompt.find("install_packages"), std::string::npos);
}

TEST(AgentLoop, ConvertToolDefsToLlmapi) {
    auto reg = xlings::capabilities::build_registry();
    xlings::agent::ToolBridge bridge(reg);
    auto llm_tools = xlings::agent::to_llmapi_tools(bridge);

    EXPECT_GE(llm_tools.size(), 8u);
    for (const auto& t : llm_tools) {
        EXPECT_FALSE(t.name.empty());
        EXPECT_FALSE(t.inputSchema.empty());
    }
}
```

**Step 2: 运行测试确认失败**

**Step 3: 实现**

```cpp
// src/agent/loop.cppm
export module xlings.agent.loop;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.agent.llm_config;
import xlings.runtime.event_stream;
import xlings.runtime.capability;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// 将 ToolBridge 的 ToolDef 转换为 llmapi::ToolDef
export auto to_llmapi_tools(const ToolBridge& bridge) -> std::vector<llm::ToolDef> {
    std::vector<llm::ToolDef> tools;
    for (const auto& td : bridge.tool_definitions()) {
        tools.push_back(llm::ToolDef{
            .name = td.name,
            .description = td.description,
            .inputSchema = td.inputSchema,
        });
    }
    return tools;
}

// 构建 system prompt
export auto build_system_prompt(const ToolBridge& bridge) -> std::string {
    std::string prompt = R"(You are xlings-agent, an AI assistant specialized in package management and environment setup.

You have access to the following tools to manage packages and versions:

)";
    for (const auto& td : bridge.tool_definitions()) {
        prompt += "- **" + td.name + "**: " + td.description + "\n";
    }
    prompt += R"(
When the user asks you to install, search, or manage packages, use the appropriate tools.
Always explain what you're about to do before calling a tool.
After a tool completes, summarize the result for the user.
)";
    return prompt;
}

// 处理单个 tool call
auto handle_tool_call(
    const llm::ToolCall& call,
    ToolBridge& bridge,
    EventStream& stream
) -> llm::ToolResultContent {
    auto result = bridge.execute(call.name, call.arguments, stream);
    return llm::ToolResultContent{
        .toolUseId = call.id,
        .content = result.content,
        .isError = result.isError,
    };
}

// Agent Loop 配置
export struct AgentLoopConfig {
    LlmConfig llm;
    bool streaming { true };
};

// 核心循环：用户输入 → LLM → Tool → LLM → ... → 最终回复
// 返回 assistant 的最终文本回复
export auto run_one_turn(
    llm::Conversation& conversation,
    std::string_view user_input,
    const std::string& system_prompt,
    const std::vector<llm::ToolDef>& tools,
    ToolBridge& bridge,
    EventStream& stream,
    const LlmConfig& cfg,
    std::function<void(std::string_view)> on_stream_chunk
) -> std::string {

    // 添加用户消息
    conversation.push(llm::Message::user(user_input));

    llm::ChatParams params;
    params.tools = tools;
    params.temperature = cfg.temperature;
    params.maxTokens = cfg.max_tokens;

    // 循环处理 tool calls
    constexpr int MAX_ITERATIONS = 20;
    for (int i = 0; i < MAX_ITERATIONS; ++i) {

        llm::ChatResponse response;

        if (cfg.provider == "anthropic") {
            llm::anthropic::Config acfg{
                .apiKey = cfg.api_key,
                .model = cfg.model,
            };
            if (!cfg.base_url.empty()) acfg.baseUrl = cfg.base_url;
            llm::anthropic::Anthropic provider(std::move(acfg));

            // 注入 system prompt 作为第一条消息（如果尚未存在）
            auto msgs = conversation.messages;
            if (msgs.empty() || msgs[0].role != llm::Role::System) {
                msgs.insert(msgs.begin(), llm::Message::system(system_prompt));
            }

            if (on_stream_chunk) {
                response = provider.chat_stream(msgs, params, on_stream_chunk);
            } else {
                response = provider.chat(msgs, params);
            }
        } else {
            llm::openai::Config ocfg{
                .apiKey = cfg.api_key,
                .model = cfg.model,
            };
            if (!cfg.base_url.empty()) ocfg.baseUrl = cfg.base_url;
            llm::openai::OpenAI provider(std::move(ocfg));

            auto msgs = conversation.messages;
            if (msgs.empty() || msgs[0].role != llm::Role::System) {
                msgs.insert(msgs.begin(), llm::Message::system(system_prompt));
            }

            if (on_stream_chunk) {
                response = provider.chat_stream(msgs, params, on_stream_chunk);
            } else {
                response = provider.chat(msgs, params);
            }
        }

        // 将 assistant 响应加入对话
        llm::Message assistant_msg;
        assistant_msg.role = llm::Role::Assistant;
        assistant_msg.content = response.content;
        conversation.push(std::move(assistant_msg));

        // 检查是否需要执行 tool calls
        if (response.stopReason != llm::StopReason::ToolUse) {
            return response.text();
        }

        auto calls = response.tool_calls();
        if (calls.empty()) {
            return response.text();
        }

        // 执行每个 tool call
        for (const auto& call : calls) {
            auto result = handle_tool_call(call, bridge, stream);

            // 将 tool result 加入对话
            llm::Message tool_msg;
            tool_msg.role = llm::Role::Tool;
            tool_msg.content = std::vector<llm::ContentPart>{result};
            conversation.push(std::move(tool_msg));
        }

        // 继续循环，让 LLM 看到 tool results
    }

    return "[agent: max iterations reached]";
}

} // namespace xlings::agent
```

**Step 4: 运行测试确认通过**

Run: `xmake build xlings_tests && xmake run xlings_tests --gtest_filter='AgentLoop.*'`
Expected: 2 tests PASS（build_system_prompt 和 to_llmapi_tools 是纯函数测试，不需要网络）

**Step 5: Commit**

```bash
git add src/agent/loop.cppm tests/unit/test_main.cpp
git commit -m "feat(agent): implement core LLM ↔ Tool loop"
```

---

### Task 5: Agent 模块入口

**Files:**
- Create: `src/agent/agent.cppm`

**Step 1: 实现 re-export 模块**

```cpp
// src/agent/agent.cppm
export module xlings.agent;

export import xlings.agent.llm_config;
export import xlings.agent.tool_bridge;
export import xlings.agent.loop;
```

**Step 2: 验证构建**

Run: `xmake build xlings`
Expected: 编译成功

**Step 3: Commit**

```bash
git add src/agent/agent.cppm
git commit -m "feat(agent): add agent module entry point"
```

---

### Task 6: CLI 集成 `xlings agent` 子命令

**Files:**
- Modify: `src/cli.cppm`

**Step 1: 定位修改点**

在 `cli.cppm` 中找到 `known_cmds` 列表（约 line 632），添加 `"agent"`。

在 subcmd 路由区域（main app.run 之前），添加 agent 处理分支。

**Step 2: 实现**

在 cli.cppm 中添加：

```cpp
// 1. 在 known_cmds 列表中加入 "agent"

// 2. 在命令路由中添加 agent 分支：
if (subcmd == "agent") {
    // 解析 agent 专用 flags
    std::string flag_model;
    std::string flag_base_url;
    bool flag_auto_approve = false;

    for (int i = 2; i < fargc; ++i) {
        std::string_view a{fargv[i]};
        if (a == "--model" && i + 1 < fargc)    { flag_model = fargv[++i]; continue; }
        if (a == "--base-url" && i + 1 < fargc)  { flag_base_url = fargv[++i]; continue; }
        if (a == "-y" || a == "--auto-approve")   { flag_auto_approve = true; continue; }
        // mcp 子命令留待 Phase 6
        if (a == "mcp") {
            std::println("xlings agent mcp: not yet implemented");
            return 0;
        }
    }

    auto cfg = agent::resolve_llm_config(flag_model, flag_base_url);

    if (cfg.api_key.empty()) {
        std::println(stderr, "Error: API key not set.");
        std::println(stderr, "  Set ANTHROPIC_API_KEY or OPENAI_API_KEY environment variable.");
        return 1;
    }

    auto bridge = agent::ToolBridge(registry);
    auto system_prompt = agent::build_system_prompt(bridge);
    auto tools = agent::to_llmapi_tools(bridge);

    mcpplibs::llmapi::Conversation conversation;

    std::println("xlings agent | {} | type 'exit' to quit\n", cfg.model);

    // 简单 REPL（Phase 3 替换为 TUI）
    std::string input;
    while (true) {
        std::print("> ");
        if (!std::getline(std::cin, input) || input == "exit" || input == "quit") break;
        if (input.empty()) continue;

        std::println("");
        auto reply = agent::run_one_turn(
            conversation, input, system_prompt, tools, bridge, stream, cfg,
            [](std::string_view chunk) { std::print("{}", chunk); }
        );
        std::println("\n");
    }

    return 0;
}
```

**Step 3: 验证构建和基本运行**

Run: `xmake build xlings`
Expected: 编译成功

Run: `./build/linux/x86_64/release/xlings agent -h`（暂时不需要 API key 的验证）

**Step 4: Commit**

```bash
git add src/cli.cppm
git commit -m "feat(agent): wire agent subcommand into CLI"
```

---

### Task 7: E2E 冒烟测试

**Files:**
- Create: `tests/e2e/agent_smoke_test.sh`

**Step 1: 写测试**

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$ROOT_DIR/build/linux/x86_64/release/xlings"

log() { echo "[agent_smoke] $*"; }
fail() { echo "[agent_smoke] FAIL: $*" >&2; exit 1; }

[[ -f "$BIN" ]] || fail "binary not found at $BIN, run xmake build first"

# Test 1: agent without API key should fail gracefully
set +e
OUT=$("$BIN" agent 2>&1 <<< "hello")
RC=$?
set -e
echo "$OUT" | grep -qi "api key\|API key\|not set" || fail "agent should report missing API key"
log "PASS: agent reports missing API key"

# Test 2: agent --help or unknown flag should not crash
set +e
OUT=$("$BIN" agent mcp 2>&1)
set -e
echo "$OUT" | grep -qi "not yet implemented\|mcp" || fail "agent mcp should report not implemented"
log "PASS: agent mcp placeholder works"

log "All agent smoke tests passed"
```

**Step 2: 运行**

Run: `bash tests/e2e/agent_smoke_test.sh`
Expected: 2 PASS

**Step 3: Commit**

```bash
git add tests/e2e/agent_smoke_test.sh
git commit -m "test(agent): add e2e smoke test for agent subcommand"
```

---

## Phase 2: .agents/ 文件系统 + Soul + Approval

目标：agent 有持久化身份、配置、审计日志，工具调用有安全审批。

### Task 8: flock 模块（跨平台文件锁）

**Files:**
- Create: `src/libs/flock/flock.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

实现 RAII 文件锁：`FileLock` 构造加锁、析构解锁、支持 shared/exclusive、try_lock。
Linux 用 `::flock()`，macOS 同理，Windows 用 `LockFileEx()`。

测试要点：
- 基本 exclusive lock + unlock
- 两个 FileLock 同文件互斥
- shared lock 并发读
- try_lock 非阻塞

---

### Task 9: AgentFS 模块

**Files:**
- Create: `src/libs/agentfs/agentfs.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

实现 `.agents/` 目录管理：
- `ensure_initialized()` — 创建完整目录树
- 路径访问（soul_path, config_path, llm_config_path, ...）
- `read_json()` / `write_json()` / `append_jsonl()` — 原子读写
- `clean_tmp()` / `clean_cache()`
- `version.json` 管理和 schema 迁移

测试要点：
- 在 tmp 目录初始化 → 验证目录结构完整
- 读写 JSON 文件
- 追加 JSONL
- 重复初始化是幂等的

---

### Task 10: Soul 模块

**Files:**
- Create: `src/libs/soul/soul.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

实现：
- `Soul` struct（persona, boundaries, scope）
- `SoulManager`（create_default, load, is_capability_allowed, is_action_forbidden）
- 首次启动生成默认 soul.seed.json

测试要点：
- 创建默认 soul → 验证 ID 非空
- allowed_capabilities ["*"] → 全部允许
- denied_capabilities ["install_packages"] → install 被拒
- forbidden_actions 匹配

---

### Task 11: Approval 模块

**Files:**
- Create: `src/agent/approval.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

实现：
- `ApprovalPolicy(Soul)` — 根据 soul.boundaries 决定审批
- `check(CapabilitySpec, params)` → Approved / Denied / NeedConfirm

测试要点：
- trust_level="auto" → 全部 Approved
- trust_level="confirm" + destructive → NeedConfirm
- trust_level="readonly" + destructive → Denied
- denied_capabilities → Denied
- forbidden_actions → Denied

---

### Task 12: Journal 模块

**Files:**
- Create: `src/libs/agent_journal/journal.cppm`
- Test: `tests/unit/test_main.cpp` (追加)

实现：
- JSONL 追加写（带文件锁）
- `log_llm_turn`, `log_tool_call`, `log_tool_result`
- `read_today()` 读回

测试要点：
- 写入 → 读回 → 验证字段
- 多次追加不覆盖

---

### Task 13: LLM 配置持久化（llm.json）

**Files:**
- Modify: `src/agent/llm_config.cppm`

扩展 `resolve_llm_config` 读取 `.agents/llm.json`：
- 读取 default 配置 + profiles
- 支持 `--profile fast` flag 切换

---

### Task 14: 集成 Approval 到 Agent Loop

**Files:**
- Modify: `src/agent/loop.cppm`

在 `handle_tool_call` 前插入 approval 检查：
- Denied → 返回错误 ToolResult
- NeedConfirm → 通过回调询问用户
- Approved → 执行

---

### Task 15: 集成 AgentFS + Soul + Journal 到启动流程

**Files:**
- Modify: `src/cli.cppm`

agent 启动时：
1. `agentfs.ensure_initialized()`
2. `soul_manager.load()` or `create_default()`
3. 从 `llm.json` 读取配置
4. 创建 Journal 实例
5. 将 Journal 注入 loop

---

## Phase 3: TUI 界面 + 会话管理

目标：从简单 REPL 升级为 ftxui TUI，支持会话持久化。

### Task 16: Session 模块

**Files:**
- Create: `src/agent/session.cppm`

实现：
- `SessionManager(AgentFS)` — create, load, list, remove, clean
- `Session` — 封装 llmapi::Conversation + 元数据
- `save()` / `load()` — 基于 `conversation.save()`

---

### Task 17: Agent TUI

**Files:**
- Create: `src/agent/tui.cppm`

基于 ftxui 实现：
- 底部固定输入框
- 上方滚动输出区
- tool call 审批确认 UI（`[Y/n]` 提示）
- 进度条内联展示（来自 EventStream）
- 顶部状态栏（model 名 + session ID）

---

### Task 18: 会话恢复

**Files:**
- Modify: `src/cli.cppm`

支持 `--session <id>` 恢复历史会话。

---

## Phase 4: Prompt Builder + Skill 系统

目标：4 层 prompt 组装，YAML Skill 定义和匹配。

### Task 19: Prompt Builder

**Files:**
- Create: `src/agent/prompt_builder.cppm`

实现 4 层：
1. Core prompt + Soul persona + boundaries + T0 tools
2. Active skills prompt
3. Dynamic context（Resource Index + 记忆 + project context）
4. User prompt (`prompt/user.md`)

---

### Task 20: Skill 模块

**Files:**
- Create: `src/libs/agent_skill/skill.cppm`

实现：
- YAML 解析（skill 格式）
- `SkillManager` — load_all, match, build_prompt
- builtin/user 分离
- trigger 关键词匹配

---

## Phase 5: 统一资源系统 + 包即工具

目标：统一缓存、T0/T1/T2 分层、包的 agent_tools 动态注册。

### Task 21: ResourceCache 模块

**Files:**
- Create: `src/agent/resource_cache.cppm`

实现：
- `ResourceKind` enum（Package, Tool, Memory, Environment, Session, Skill, Index）
- `ResourceMeta` struct（id, kind, summary, ttl, age, stale...）
- `ResourceCache` — get, put, search, build_resource_index, invalidate
- TTL 按 kind 配置
- 缓存结果携带元信息（cache.hit, cached_at, age_seconds）

---

### Task 22: search_resources + load_resource T0 tools

**Files:**
- Create: `src/agent/resource_tools.cppm`
- Modify: `src/agent/tool_bridge.cppm`

将 search_resources 和 load_resource 注册为 T0 内置 tools。

---

### Task 23: 包即工具（xpkg agent_tools）

**Files:**
- Modify: `src/agent/tool_bridge.cppm`

实现：
- 扫描已安装包的 agent_tools 定义
- 动态注册到 ToolBridge
- install/uninstall 事件触发 `on_package_change()`
- 包工具 tier 分层（默认 T1）

---

### Task 24: Resource Index 集成到 Prompt

**Files:**
- Modify: `src/agent/prompt_builder.cppm`

在 L3 层注入 Resource Index（~500 tokens 的环境/包/工具/记忆摘要）。

---

## Phase 6: 语义记忆 + MCP

目标：语义记忆存储与检索，MCP server + client。

### Task 25: Embedding Engine

**Files:**
- Create: `src/libs/semantic_memory/embedding.cppm`

实现：
- 通过 llmapi 的 EmbeddableProvider 获取向量
- 本地缓存（.agents/cache/embeddings/）
- cosine_similarity 计算

---

### Task 26: MemoryStore

**Files:**
- Create: `src/libs/semantic_memory/memory.cppm`

实现：
- remember, recall, forget
- 向量索引（brute-force cosine）
- entries/ 目录持久化

---

### Task 27: MCP Server

**Files:**
- Create: `src/libs/mcp_server/server.cppm`
- Create: `src/libs/mcp_server/stdio_transport.cppm`

实现：
- JSON-RPC 2.0 over stdio
- tools/list, tools/call
- 注册 xlings 能力 + agent tools + memory tools

---

### Task 28: MCP Client

**Files:**
- Create: `src/agent/mcp/client.cppm`
- Create: `src/agent/mcp/config.cppm`

实现：
- 从 `.agents/mcps/` 加载外部 MCP server 配置
- 连接外部 server（stdio spawn）
- tools/list → 注册到 ToolBridge
- tools/call → 路由执行

---

### Task 29: resource-lock 模块

**Files:**
- Create: `src/libs/resource_lock/resource_lock.cppm`

实现：
- 命名资源锁（基于 flock）
- shared/exclusive
- stale PID 检测

---

## Phase 依赖关系

```
Phase 1 (MVP)
  │
  ├──→ Phase 2 (.agents/ + Soul + Approval)
  │      │
  │      ├──→ Phase 3 (TUI + Session)
  │      │
  │      └──→ Phase 4 (Prompt + Skills)
  │             │
  │             └──→ Phase 5 (Resource System + Package as Tool)
  │
  └──→ Phase 6 (Memory + MCP)  ← 可与 Phase 3-5 并行
```

Phase 1 完成后即可用（终端 REPL + 工具调用），后续 Phase 逐步增强。

---

## 验证清单

每个 Phase 完成后验证：

- [ ] `xmake build xlings` — 主二进制编译通过
- [ ] `xmake build xlings_tests && xmake run xlings_tests` — 所有单元测试通过
- [ ] `bash tests/e2e/project_e2e_test.sh` — 已有 E2E 测试不退化
- [ ] `bash tests/e2e/agent_smoke_test.sh` — Agent 冒烟测试通过
- [ ] Phase 1+: 手动 `ANTHROPIC_API_KEY=xxx xlings agent` 对话验证
