export module xlings.agent.loop;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.agent.llm_config;
import xlings.agent.approval;
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.agent.behavior_tree;
import xlings.agent.tui;
import xlings.agent.lua_engine;
import xlings.libs.soul;
import xlings.libs.json;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.runtime.cancellation;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// ─── execute_lua tool definition (virtual — executed by LuaSandbox) ───

auto execute_lua_tool_def() -> llm::ToolDef {
    return llm::ToolDef{
        .name = "execute_lua",
        .description = "Execute a Lua code snippet in a sandboxed environment. "
                       "Use this to perform multi-step operations efficiently in a single call. "
                       "Available APIs: pkg.search(query), pkg.install(name [,version]), "
                       "pkg.remove(name), pkg.list(), pkg.info(name), pkg.update(name), "
                       "sys.status(), sys.run(command), ver.use(name, version). "
                       "Return a table with results.",
        .inputSchema = R"JSON({
  "type": "object",
  "properties": {
    "code": {
      "type": "string",
      "description": "Lua code to execute in the sandbox. Use the pkg/sys/ver APIs to interact with the system. Return a table with results."
    }
  },
  "required": ["code"]
})JSON",
    };
}

// ─── decide tool definition (virtual — used in ask_decision) ───

auto decide_tool_def() -> llm::ToolDef {
    return llm::ToolDef{
        .name = "decide",
        .description = "Decide how to handle the current task: execute it directly with tools, "
                       "or decompose it into subtasks. For atomic operations with known tool and args, "
                       "specify tool+args in subtasks so the system executes them directly without LLM overhead.",
        .inputSchema = R"JSON({
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "enum": ["execute", "decompose"],
      "description": "execute: handle this task directly with tool calls. decompose: split into subtasks."
    },
    "subtasks": {
      "type": "array",
      "description": "Required when action=decompose. List subtasks in execution order.",
      "items": {
        "type": "object",
        "properties": {
          "title": { "type": "string", "description": "Subtask title" },
          "description": { "type": "string", "description": "Subtask detailed description" },
          "tool": { "type": "string", "description": "Optional: tool name for direct execution. System calls it automatically." },
          "args": { "type": "object", "description": "Optional: tool arguments JSON. Required when tool is specified." }
        },
        "required": ["title"]
      }
    }
  },
  "required": ["action"]
})JSON",
    };
}

// Convert ToolBridge ToolDefs to llmapi ToolDefs (real tools only, no virtual tools)
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

export struct MemorySummary {
    std::string content;
    std::string category;
};

// Build base system prompt (L1 + memories)
export auto build_system_prompt(
    [[maybe_unused]] const ToolBridge& bridge,
    const std::vector<MemorySummary>& memories = {}
) -> std::string {
    std::string prompt = R"(You are xlings-agent, an AI assistant specialized in package management and environment setup.

## CRITICAL Rules

1. **ALWAYS use built-in tools for package/version operations.** For example:
   - To switch Node.js version → use `use_version`, NEVER `nvm use` via run_command
   - To install a package → use `install_packages`, NEVER `apt install` via run_command
   - To search packages → use `search_packages`, NEVER shell commands
   - To list installed packages → use `list_packages`
   - To check system info → use `system_status`

2. **NEVER use `run_command` or `execute_lua` for any task that a built-in tool can handle.** These are LAST RESORT tools — ONLY use them when NO other built-in tool can accomplish the task (e.g., checking disk space, reading a file, running a user's custom script, or batch operations with complex conditional logic).

3. Before calling a tool, briefly explain what you're about to do.
4. After a tool completes, summarize the result for the user.

## Output Format
- Do NOT use markdown formatting (no headers, bold, lists, code blocks, etc.)
- Keep responses as short as possible — one or two sentences when sufficient
- Use plain text only

## Lua Execution Engine

You can use the `execute_lua` tool to run multi-step operations in a single call.

### Available APIs

```lua
-- Package management
pkg.search(query)            -- returns {found=bool, packages=[{name, version, description}]}
pkg.install(name [,version]) -- returns {success=bool, message=string}
pkg.remove(name)             -- returns {success=bool, message=string}
pkg.list()                   -- returns {packages=[{name, version, status}]}
pkg.info(name)               -- returns {name, version, description, installed=bool, ...}
pkg.update(name)             -- returns {success=bool, message=string}

-- System
sys.status()                 -- returns {os, arch, hostname, ...}
sys.run(command)             -- returns {exit_code=int, stdout=string, stderr=string}

-- Version management
ver.use(name, version)       -- returns {success=bool, message=string}
```

### When to use execute_lua vs individual tools
- **ALWAYS prefer individual tools** — execute_lua is a LAST RESORT, only for tasks that cannot be done with built-in tools alone
- **Use execute_lua ONLY when**: you need complex conditional logic across multiple operations, or data aggregation that individual tools cannot express
- **Use individual tools when**: the task can be accomplished by calling one or more built-in tools directly

### Example
```lua
local results = {}
for _, name in ipairs({"vim", "git", "curl"}) do
    local info = pkg.info(name)
    if not info.installed then
        results[name] = pkg.install(name)
    else
        results[name] = {success=true, message="already installed"}
    end
end
return results
```)";

    if (!memories.empty()) {
        prompt += "\n## Remembered Context\n\n";
        prompt += "You have " + std::to_string(memories.size()) + " memories from previous sessions:\n";
        int count = 0;
        for (auto& m : memories) {
            if (count >= 20) {
                prompt += "... and " + std::to_string(static_cast<int>(memories.size()) - 20) + " more (use search_memory to find them)\n";
                break;
            }
            std::string brief = m.content.size() > 100 ? m.content.substr(0, 100) + "..." : m.content;
            prompt += "- [" + m.category + "] " + brief + "\n";
            ++count;
        }
        prompt += "\nUse save_memory/search_memory/forget_memory to manage your long-term memory.\n";
    }

    return prompt;
}

// ─── Callback types ───

export using ConfirmCallback = std::function<bool(std::string_view tool_name, std::string_view arguments)>;
export using ToolCallCallback = std::function<void(int action_id, std::string_view name, std::string_view args)>;
export using ToolResultCallback = std::function<void(int action_id, std::string_view name, bool is_error)>;
export using AutoCompactCallback = std::function<void(int evicted_turns, int freed_tokens)>;
export using TokenUpdateCallback = std::function<void(int input_tokens, int output_tokens)>;

// ─── Subtask definition from LLM decide response ───

struct SubtaskDef {
    std::string title;
    std::string description;
    std::string tool;       // non-empty = DirectExec
    std::string tool_args;  // JSON string
};

struct Decision {
    bool is_execute {true};
    std::vector<SubtaskDef> subtasks;
};

// ─── Node context for scoped prompts ───

struct NodeContext {
    std::string root_name;  // original user request
    std::vector<std::string> ancestor_path;  // names from root to parent
    std::vector<std::pair<std::string, std::string>> sibling_results;  // {name, summary}
};

// ─── Node result from run_execute / run_tool_loop ───

struct NodeResult {
    std::string reply;
    int input_tokens {0};
    int output_tokens {0};
    int cache_read_tokens {0};
    int cache_write_tokens {0};
    bool failed {false};
};

// ─── TreeConfig — input to run_task_tree ───

export struct TreeConfig {
    std::string user_input;
    const std::string& base_system_prompt;  // L1 + L4
    const std::vector<llm::ToolDef>& tools; // real tools (no decide/manage_tree)
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    llm::Conversation* conversation {nullptr};  // shared conversation for cross-turn context
    ApprovalPolicy* policy {nullptr};
    ConfirmCallback confirm_cb;
    CancellationToken* cancel {nullptr};
    ABehaviorTree* tree {nullptr};
    IdAllocator* id_alloc {nullptr};
    TokenTracker* tracker {nullptr};
    ContextManager* ctx_mgr {nullptr};
    LuaSandbox* lua_sandbox {nullptr};
    // TUI callbacks
    std::function<void(std::string_view)> on_stream_chunk;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    TokenUpdateCallback on_token_update;
    AutoCompactCallback on_auto_compact;
};

// ─── TreeResult — output from run_task_tree ───

export struct TreeResult {
    std::string reply;
    int input_tokens {0};
    int output_tokens {0};
    int cache_read_tokens {0};
    int cache_write_tokens {0};
};

// ─── Keep TurnConfig + TurnResult for backward compat ───

export struct TurnConfig {
    llm::Conversation& conversation;
    std::string_view user_input;
    const std::string& system_prompt;
    const std::vector<llm::ToolDef>& tools;
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    std::function<void(std::string_view)> on_stream_chunk;
    ApprovalPolicy* policy {nullptr};
    ConfirmCallback confirm_cb;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    ContextManager* ctx_mgr {nullptr};
    TokenTracker* tracker {nullptr};
    AutoCompactCallback on_auto_compact;
    CancellationToken* cancel {nullptr};
    ABehaviorTree* behavior_tree {nullptr};
    IdAllocator* id_alloc {nullptr};
    TokenUpdateCallback on_token_update;
    LuaSandbox* lua_sandbox {nullptr};
};

// ─── Handle a single tool call with optional approval ───

auto handle_tool_call(
    const llm::ToolCall& call,
    ToolBridge& bridge,
    EventStream& stream,
    ApprovalPolicy* policy,
    ConfirmCallback confirm_cb,
    CancellationToken* cancel = nullptr
) -> llm::ToolResultContent {

    // Check cancellation/pause before approval/execution
    if (cancel && !cancel->is_active()) {
        if (cancel->is_paused()) throw PausedException{};
        return llm::ToolResultContent{
            .toolUseId = call.id,
            .content = "Operation cancelled.",
            .isError = true,
        };
    }

    // Approval check
    if (policy) {
        auto info = bridge.tool_info(call.name);
        capability::CapabilitySpec spec;
        spec.name = info.name;
        spec.destructive = info.destructive;

        auto result = policy->check(spec, call.arguments);

        if (result == ApprovalResult::Denied) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = "Tool call denied by approval policy.",
                .isError = true,
            };
        }

        if (result == ApprovalResult::NeedConfirm) {
            if (confirm_cb) {
                if (!confirm_cb(call.name, call.arguments)) {
                    return llm::ToolResultContent{
                        .toolUseId = call.id,
                        .content = "Tool call denied by user.",
                        .isError = true,
                    };
                }
            }
            // No confirm callback but NeedConfirm → deny for safety
            else {
                return llm::ToolResultContent{
                    .toolUseId = call.id,
                    .content = "Tool call requires confirmation but no confirm callback available.",
                    .isError = true,
                };
            }
        }
    }

    auto exec_result = bridge.execute(call.name, call.arguments, stream, cancel);
    return llm::ToolResultContent{
        .toolUseId = call.id,
        .content = exec_result.content,
        .isError = exec_result.isError,
    };
}

// ─── Cancellable LLM call worker template ───

template<typename Provider, typename ProviderConfig>
auto llm_call_worker(
    ProviderConfig pcfg,
    std::vector<llm::Message> msgs,
    llm::ChatParams& params,
    std::function<void(std::string_view)> on_chunk,
    bool has_stream_cb,
    CancellationToken* cancel
) -> llm::ChatResponse {
    auto abandoned  = std::make_shared<std::atomic<bool>>(false);
    auto done_flag  = std::make_shared<std::atomic<bool>>(false);
    auto resp_ptr   = std::make_shared<llm::ChatResponse>();
    auto err_ptr    = std::make_shared<std::exception_ptr>();
    auto cv_mtx     = std::make_shared<std::mutex>();
    auto cv_done    = std::make_shared<std::condition_variable>();

    // Wrap on_chunk with abandoned-flag guard
    auto safe_chunk = [abandoned, on_chunk = std::move(on_chunk)](std::string_view chunk) {
        if (abandoned->load(std::memory_order_acquire)) throw CancelledException{};
        if (on_chunk) on_chunk(chunk);
    };

    std::thread worker([done_flag, resp_ptr, err_ptr, cv_mtx, cv_done,
                        provider = Provider(std::move(pcfg)),
                        call_msgs = std::move(msgs), &params,
                        safe_chunk, has_stream_cb]() mutable {
        try {
            if (has_stream_cb) {
                *resp_ptr = provider.chat_stream(call_msgs, params, safe_chunk);
            } else {
                *resp_ptr = provider.chat(call_msgs, params);
            }
        } catch (...) {
            *err_ptr = std::current_exception();
        }
        done_flag->store(true, std::memory_order_release);
        cv_done->notify_all();
    });

    {
        std::unique_lock lk(*cv_mtx);
        while (!done_flag->load(std::memory_order_acquire)) {
            if (cancel && !cancel->is_active()) {
                abandoned->store(true, std::memory_order_release);
                worker.detach();
                if (cancel->is_paused()) throw PausedException{};
                throw CancelledException{};
            }
            cv_done->wait_for(lk, std::chrono::milliseconds{200});
        }
    }
    worker.join();

    if (*err_ptr) std::rethrow_exception(*err_ptr);
    return std::move(*resp_ptr);
}

// ─── Make LLM call (provider dispatch) ───

auto do_llm_call(
    const std::vector<llm::Message>& msgs,
    llm::ChatParams& params,
    const LlmConfig& cfg,
    std::function<void(std::string_view)> on_chunk,
    CancellationToken* cancel
) -> llm::ChatResponse {
    bool has_stream_cb = static_cast<bool>(on_chunk);
    if (cfg.provider == "anthropic") {
        llm::anthropic::Config acfg{
            .apiKey = cfg.api_key,
            .model = cfg.model,
        };
        if (!cfg.base_url.empty()) acfg.baseUrl = cfg.base_url;
        return llm_call_worker<llm::anthropic::Anthropic>(
            std::move(acfg), msgs, params, std::move(on_chunk), has_stream_cb, cancel);
    } else {
        llm::openai::Config ocfg{
            .apiKey = cfg.api_key,
            .model = cfg.model,
        };
        if (!cfg.base_url.empty()) ocfg.baseUrl = cfg.base_url;
        return llm_call_worker<llm::openai::OpenAI>(
            std::move(ocfg), msgs, params, std::move(on_chunk), has_stream_cb, cancel);
    }
}

// ─── Build scoped prompt for a node ───

auto build_scoped_prompt(
    const BehaviorNode& node,
    const NodeContext& ctx,
    const std::string& base_prompt,
    const std::vector<llm::ToolDef>& tools,
    bool decision_mode
) -> std::string {
    std::string prompt = base_prompt;

    prompt += "\n\n## Task Context\n";
    prompt += "User request: " + ctx.root_name + "\n";

    if (!ctx.ancestor_path.empty()) {
        prompt += "Task path: ";
        for (std::size_t i = 0; i < ctx.ancestor_path.size(); ++i) {
            if (i > 0) prompt += " > ";
            prompt += ctx.ancestor_path[i];
        }
        prompt += " > " + node.name + "\n";
    }

    if (!ctx.sibling_results.empty()) {
        prompt += "\n## Completed Sibling Tasks\n";
        prompt += "IMPORTANT: Review these results before acting — do NOT repeat work already done.\n";
        for (auto& [name, summary] : ctx.sibling_results) {
            prompt += "- " + name + ": " + summary + "\n";
        }
    }

    prompt += "\n## Current Task\n";
    prompt += node.name + "\n";
    if (!node.detail.empty()) {
        prompt += node.detail + "\n";
    }

    if (decision_mode) {
        // List available tool names so LLM can specify correct DirectExec tools
        prompt += "\n## Available Tools\n";
        for (auto& t : tools) {
            prompt += "- " + t.name + "\n";
        }
        prompt += R"(
Decide how to handle this task. Call the decide tool:

**execute** — Handle this task yourself with tool calls and reasoning. Best when:
  - The task is simple (1-3 tool calls)
  - Steps depend on previous results (conditional logic, e.g. "check then act")
  - You need to interpret results before deciding next action

**decompose** — Split into subtasks. Best when:
  - The task has multiple independent parts
  - Subtasks can include:
    - tool+args: system executes directly, zero LLM cost (use EXACT tool name from list above)
    - title only: LLM handles with reasoning (for steps needing judgment or conditional logic)
  - Mix both types freely. Use title-only for steps that depend on earlier results.
)";
    } else {
        if (!ctx.sibling_results.empty()) {
            prompt += R"(
Use sibling task results above as context — do NOT re-check what is already known.
Use tools to complete this task efficiently with minimal calls.
Give a brief result summary (1-2 sentences) when done.
)";
        } else {
            prompt += R"(
Use the available tools to complete this task efficiently with minimal calls.
Give a brief result summary (1-2 sentences) when done.
)";
        }
    }

    return prompt;
}

// ─── Synthesize children results into parent summary ───

auto synthesize_children(const BehaviorNode& node) -> std::string {
    // Build a human-readable summary from child names + status
    std::string summary;
    int done_count = 0;
    int failed_count = 0;
    int total = 0;
    for (auto& child : node.children) {
        if (child.type == BehaviorNode::TypeToolCall ||
            child.type == BehaviorNode::TypeResponse) continue;
        ++total;
        if (child.state == BehaviorNode::Done) ++done_count;
        if (child.state == BehaviorNode::Failed) ++failed_count;
    }

    // For Execute nodes (LLM-generated reply), use their result_summary
    // For DirectExec/Decompose nodes, use name + status (raw tool output is not user-friendly)
    for (auto& child : node.children) {
        if (child.type == BehaviorNode::TypeToolCall ||
            child.type == BehaviorNode::TypeResponse) continue;
        if (child.type == BehaviorNode::TypeExecute && !child.result_summary.empty()) {
            // Execute nodes have LLM-generated summaries — use them
            if (!summary.empty()) summary += "; ";
            summary += child.result_summary;
        }
    }

    if (!summary.empty()) {
        return summary.size() > 200 ? summary.substr(0, 200) : summary;
    }

    // Fallback: count-based summary
    if (failed_count > 0) {
        summary = std::to_string(done_count) + " completed, " +
                  std::to_string(failed_count) + " failed out of " +
                  std::to_string(total) + " tasks";
    } else {
        summary = std::to_string(done_count) + "/" + std::to_string(total) + " tasks completed";
    }
    return summary;
}

// ─── run_tool_loop — extracted LLM ↔ tool inner loop ───
// Used by both run_execute (new) and run_one_turn (compat)

struct ToolLoopConfig {
    llm::Conversation& conversation;
    const std::string& system_prompt;
    const std::vector<llm::ToolDef>& tools;
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    std::function<void(std::string_view)> on_stream_chunk;
    ApprovalPolicy* policy {nullptr};
    ConfirmCallback confirm_cb;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    CancellationToken* cancel {nullptr};
    int parent_node_id {0};  // for begin_tool/end_tool
    ABehaviorTree* tree {nullptr};
    IdAllocator* id_alloc {nullptr};
    LuaSandbox* lua_sandbox {nullptr};
    TokenUpdateCallback on_token_update;
    ContextManager* ctx_mgr {nullptr};
    TokenTracker* tracker {nullptr};
    AutoCompactCallback on_auto_compact;
};

auto run_tool_loop(ToolLoopConfig& lc) -> NodeResult {
    llm::ChatParams params;
    params.tools = lc.tools;
    params.temperature = lc.cfg.temperature;
    params.maxTokens = lc.cfg.max_tokens;

    NodeResult result;
    int action_counter = 0;
    int last_input_tokens = 0;  // tracks this loop's latest LLM input tokens

    constexpr int MAX_ITERATIONS = 50;
    constexpr int TOOL_ONLY_LIMIT = 40;
    int consecutive_tool_only = 0;

    for (int i = 0; i < MAX_ITERATIONS; ++i) {

        // Check cancellation/pause before each LLM call
        if (lc.cancel && !lc.cancel->is_active()) {
            if (lc.cancel->is_paused()) throw PausedException{};
            throw CancelledException{};
        }

        // Context budget check — use this node's last input tokens (not stale session tracker)
        if (last_input_tokens > 0) {
            int ctx_limit = TokenTracker::context_limit(lc.cfg.model);
            if (last_input_tokens > static_cast<int>(ctx_limit * 0.92)) {
                // Auto-compact this node's conversation before giving up
                if (lc.ctx_mgr && lc.tracker) {
                    if (lc.ctx_mgr->maybe_auto_compact(lc.conversation, *lc.tracker)) {
                        if (lc.on_auto_compact) lc.on_auto_compact(0, 0);
                        continue;  // retry after compact
                    }
                }
                result.reply = "[approaching context limit, stopping]";
                return result;
            }
        }

        // Build messages snapshot
        auto msgs = lc.conversation.messages;
        if (msgs.empty() || msgs[0].role != llm::Role::System) {
            msgs.insert(msgs.begin(), llm::Message::system(lc.system_prompt));
        }

        auto response = do_llm_call(msgs, params, lc.cfg, lc.on_stream_chunk, lc.cancel);

        // Accumulate tokens
        last_input_tokens = response.usage.inputTokens;
        result.input_tokens += response.usage.inputTokens;
        result.output_tokens += response.usage.outputTokens;
        result.cache_read_tokens += response.usage.cacheReadTokens;
        result.cache_write_tokens += response.usage.cacheCreationTokens;

        if (lc.on_token_update) {
            lc.on_token_update(result.input_tokens, result.output_tokens);
        }

        // Add assistant response to conversation
        llm::Message assistant_msg;
        assistant_msg.role = llm::Role::Assistant;
        assistant_msg.content = response.content;
        lc.conversation.push(std::move(assistant_msg));

        // No tool use → done
        if (response.stopReason != llm::StopReason::ToolUse) {
            result.reply = response.text();
            return result;
        }

        auto calls = response.tool_calls();
        if (calls.empty()) {
            result.reply = response.text();
            return result;
        }

        // Execute each tool call
        for (const auto& call : calls) {
            ++action_counter;

            if (lc.cancel && !lc.cancel->is_active()) {
                if (lc.cancel->is_paused()) throw PausedException{};
                throw CancelledException{};
            }

            // Intercept execute_lua virtual tool
            if (call.name == "execute_lua" && lc.lua_sandbox) {
                if (lc.on_tool_call) {
                    lc.on_tool_call(action_counter, call.name, call.arguments);
                }

                auto json = nlohmann::json::parse(call.arguments, nullptr, false);
                auto code = json.is_discarded() ? "" : json.value("code", "");

                llm::ToolResultContent lua_result;
                if (code.empty()) {
                    lua_result = llm::ToolResultContent{
                        .toolUseId = call.id,
                        .content = R"({"error":"'code' parameter is required"})",
                        .isError = true,
                    };
                } else {
                    Action decision_action;
                    decision_action.layer = LayerDecision;
                    decision_action.name = "execute_lua";
                    decision_action.detail = code;

                    auto exec_log = lc.lua_sandbox->execute(code, decision_action);
                    lua_result = llm::ToolResultContent{
                        .toolUseId = call.id,
                        .content = exec_log.to_json(),
                        .isError = (exec_log.status != "completed"),
                    };
                }

                if (lc.on_tool_result) {
                    lc.on_tool_result(action_counter, call.name, lua_result.isError);
                }

                llm::Message tool_msg;
                tool_msg.role = llm::Role::Tool;
                tool_msg.content = std::vector<llm::ContentPart>{lua_result};
                lc.conversation.push(std::move(tool_msg));
                continue;
            }

            // Notify TUI of tool call
            if (lc.on_tool_call) {
                lc.on_tool_call(action_counter, call.name, call.arguments);
            }

            auto tool_result = handle_tool_call(call, lc.bridge, lc.stream,
                                                 lc.policy, lc.confirm_cb, lc.cancel);

            // Notify TUI of tool result
            if (lc.on_tool_result) {
                lc.on_tool_result(action_counter, call.name, tool_result.isError);
            }

            llm::Message tool_msg;
            tool_msg.role = llm::Role::Tool;
            tool_msg.content = std::vector<llm::ContentPart>{tool_result};
            lc.conversation.push(std::move(tool_msg));
        }

        // Runaway detection
        if (response.text().empty()) {
            ++consecutive_tool_only;
        } else {
            consecutive_tool_only = 0;
        }
        if (consecutive_tool_only > TOOL_ONLY_LIMIT) {
            result.reply = "[agent: too many tool-only iterations, stopping]";
            return result;
        }
    }

    result.reply = "[agent: max iterations reached]";
    return result;
}

// ─── ask_decision — LLM decides execute or decompose ───

inline constexpr int MAX_DEPTH = 5;

auto ask_decision(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    const NodeContext& ctx,
    TreeResult& accum
) -> Decision {
    if (depth >= MAX_DEPTH) return Decision{.is_execute = true};

    auto prompt = build_scoped_prompt(node, ctx, tc.base_system_prompt, tc.tools, /*decision_mode=*/true);
    auto tools = std::vector<llm::ToolDef>{ decide_tool_def() };

    llm::ChatParams params;
    params.tools = tools;
    params.temperature = tc.cfg.temperature;
    params.maxTokens = tc.cfg.max_tokens;

    llm::Conversation conv;
    conv.push(llm::Message::system(prompt));

    // Inject shared conversation history only at root level (depth 0)
    // Nested nodes use sibling_results + ancestor_path for context
    if (depth == 0 && tc.conversation) {
        for (auto& msg : tc.conversation->messages) {
            if (msg.role == llm::Role::System) continue;
            conv.push(msg);
        }
    }

    conv.push(llm::Message::user("Task: " + node.name +
        (node.detail.empty() ? "" : "\n" + node.detail)));

    if (tc.cancel && !tc.cancel->is_active()) {
        if (tc.cancel->is_paused()) throw PausedException{};
        throw CancelledException{};
    }

    auto msgs = conv.messages;
    auto response = do_llm_call(msgs, params, tc.cfg, nullptr, tc.cancel);

    // Accumulate tokens
    accum.input_tokens += response.usage.inputTokens;
    accum.output_tokens += response.usage.outputTokens;
    accum.cache_read_tokens += response.usage.cacheReadTokens;
    accum.cache_write_tokens += response.usage.cacheCreationTokens;

    if (tc.on_token_update) {
        tc.on_token_update(accum.input_tokens, accum.output_tokens);
    }

    // Parse decide tool call
    auto calls = response.tool_calls();
    if (calls.empty() || calls[0].name != "decide") {
        return Decision{.is_execute = true};  // implicit execute
    }

    auto json = nlohmann::json::parse(calls[0].arguments, nullptr, false);
    if (json.is_discarded()) return Decision{.is_execute = true};

    auto action = json.value("action", "execute");
    if (action == "decompose" && json.contains("subtasks") && json["subtasks"].is_array()) {
        Decision d{.is_execute = false};
        for (auto& s : json["subtasks"]) {
            SubtaskDef sub;
            sub.title = s.value("title", "untitled");
            sub.description = s.value("description", "");
            if (s.contains("tool") && s["tool"].is_string() && !s["tool"].get<std::string>().empty()) {
                auto tool_name = s["tool"].get<std::string>();
                // Validate tool name against registered tools
                bool valid = (tc.bridge.tool_info(tool_name).source != "unknown");
                if (valid) {
                    sub.tool = tool_name;
                    sub.tool_args = s.contains("args") ? s["args"].dump() : "{}";
                }
                // Invalid tool name → fall back to Execute (LLM will reason about it)
            }
            d.subtasks.push_back(std::move(sub));
        }
        if (d.subtasks.empty()) return Decision{.is_execute = true};
        return d;
    }
    return Decision{.is_execute = true};
}

// ─── run_execute — LLM tool-use loop for an Execute node ───

auto run_execute(
    BehaviorNode& node,
    TreeConfig& tc,
    const NodeContext& ctx,
    TreeResult& accum,
    int depth
) -> NodeResult {
    auto prompt = build_scoped_prompt(node, ctx, tc.base_system_prompt, tc.tools, /*decision_mode=*/false);

    llm::Conversation conv;
    conv.push(llm::Message::system(prompt));

    // Inject shared conversation history only at root level (depth 0)
    if (depth == 0 && tc.conversation) {
        for (auto& msg : tc.conversation->messages) {
            if (msg.role == llm::Role::System) continue;
            conv.push(msg);
        }
    }

    conv.push(llm::Message::user(node.name +
        (node.detail.empty() ? "" : "\n" + node.detail)));

    // Build tool list: real tools + execute_lua
    auto exec_tools = tc.tools;
    exec_tools.push_back(execute_lua_tool_def());

    ToolLoopConfig lc{
        .conversation = conv,
        .system_prompt = prompt,
        .tools = exec_tools,
        .bridge = tc.bridge,
        .stream = tc.stream,
        .cfg = tc.cfg,
        .on_stream_chunk = tc.on_stream_chunk,
        .policy = tc.policy,
        .confirm_cb = tc.confirm_cb,
        .on_tool_call = [&](int id, std::string_view name, std::string_view args) {
            // Update behavior tree for TUI
            if (tc.tree) {
                auto call_start = steady_now_ms();
                int term_w = 80;  // reasonable default
                tc.tree->flush_as_response(node.id, term_w);
                auto n = std::string(name);
                auto a = std::string(args);
                if (a.size() > 60) a = a.substr(0, 60) + "...";
                tc.tree->begin_tool(node.id, id, n, a, call_start);
            }
            if (tc.on_tool_call) tc.on_tool_call(id, name, args);
        },
        .on_tool_result = [&](int id, std::string_view name, bool is_error) {
            if (tc.tree) {
                tc.tree->end_tool(id, is_error, steady_now_ms());
            }
            if (tc.on_tool_result) tc.on_tool_result(id, name, is_error);
        },
        .cancel = tc.cancel,
        .parent_node_id = node.id,
        .tree = tc.tree,
        .id_alloc = tc.id_alloc,
        .lua_sandbox = tc.lua_sandbox,
        .on_token_update = [&](int in_tok, int out_tok) {
            accum.input_tokens += in_tok;
            accum.output_tokens += out_tok;
            if (tc.on_token_update) tc.on_token_update(accum.input_tokens, accum.output_tokens);
        },
        .ctx_mgr = tc.ctx_mgr,
        .tracker = tc.tracker,
        .on_auto_compact = tc.on_auto_compact,
    };

    return run_tool_loop(lc);
}

// ─── process_node — recursive DFS traversal ───

void process_node(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    TreeResult& accum,
    const NodeContext& ctx
) {
    if (tc.cancel && !tc.cancel->is_active()) {
        if (tc.cancel->is_paused()) throw PausedException{};
        throw CancelledException{};
    }

    auto now = steady_now_ms();
    node.start_ms = now;
    if (tc.tree) {
        tc.tree->set_state(node.id, BehaviorNode::Running, now);
        tc.tree->set_active(node.id);
    }

    // ── DirectExec: system direct tool call, zero LLM ──
    if (node.is_direct_exec()) {
        node.type = BehaviorNode::TypeDirectExec;

        // Notify TUI
        if (tc.on_tool_call) {
            auto args_preview = node.tool_args.size() > 60
                ? node.tool_args.substr(0, 60) + "..." : node.tool_args;
            tc.on_tool_call(node.id, node.tool, args_preview);
        }

        auto exec_result = tc.bridge.execute(node.tool, node.tool_args, tc.stream, tc.cancel);
        node.state = exec_result.isError ? BehaviorNode::Failed : BehaviorNode::Done;
        node.result_summary = exec_result.content.size() > 200
            ? exec_result.content.substr(0, 200) : exec_result.content;
        node.end_ms = steady_now_ms();

        if (tc.on_tool_result) {
            tc.on_tool_result(node.id, node.tool, exec_result.isError);
        }

        if (tc.tree) {
            tc.tree->set_state(node.id, node.state, node.end_ms);
            tc.tree->set_result(node.id, node.result_summary);
        }
        return;
    }

    // ── Decision: LLM decides execute or decompose ──
    auto decision = ask_decision(node, tc, depth, ctx, accum);

    if (decision.is_execute || depth >= MAX_DEPTH) {
        // ── Execute: LLM tool-use loop ──
        node.type = BehaviorNode::TypeExecute;
        if (tc.tree) tc.tree->set_state(node.id, BehaviorNode::Running, steady_now_ms());

        auto nr = run_execute(node, tc, ctx, accum, depth);
        node.state = nr.failed ? BehaviorNode::Failed : BehaviorNode::Done;
        node.result_summary = nr.reply.size() > 200 ? nr.reply.substr(0, 200) : nr.reply;
        node.end_ms = steady_now_ms();

        if (tc.tree) {
            tc.tree->set_state(node.id, node.state, node.end_ms);
            tc.tree->set_result(node.id, node.result_summary);
        }
    } else {
        // ── Decompose: create children, DFS traverse ──
        node.type = BehaviorNode::TypeDecompose;

        for (auto& sub : decision.subtasks) {
            BehaviorNode child;
            child.id = tc.id_alloc ? tc.id_alloc->alloc() : 0;
            child.name = sub.title;
            child.detail = sub.description;
            child.tool = sub.tool;
            child.tool_args = sub.tool_args;
            if (tc.tree) tc.tree->add_child(node.id, child);
            node.children.push_back(std::move(child));
        }

        // DFS sequential execution of children — continue on failure
        bool has_failures = false;
        for (std::size_t ci = 0; ci < node.children.size(); ++ci) {
            auto& child = node.children[ci];
            // Skip record nodes
            if (child.type == BehaviorNode::TypeToolCall ||
                child.type == BehaviorNode::TypeResponse) continue;

            // Build child context with sibling results (including failed ones)
            NodeContext child_ctx;
            child_ctx.root_name = ctx.root_name;
            child_ctx.ancestor_path = ctx.ancestor_path;
            child_ctx.ancestor_path.push_back(node.name);
            for (std::size_t si = 0; si < ci; ++si) {
                auto& sib = node.children[si];
                if (sib.is_terminal() && !sib.result_summary.empty()) {
                    std::string prefix = (sib.state == BehaviorNode::Failed) ? "[FAILED] " : "";
                    child_ctx.sibling_results.push_back({sib.name, prefix + sib.result_summary});
                }
            }

            process_node(child, tc, depth + 1, accum, child_ctx);

            if (child.state == BehaviorNode::Failed) {
                has_failures = true;
            }
        }

        if (has_failures) {
            // ── Verification: tool-less LLM call to review results ──
            BehaviorNode verify;
            verify.id = tc.id_alloc ? tc.id_alloc->alloc() : 0;
            verify.name = "verify results";
            verify.type = BehaviorNode::TypeResponse;
            verify.start_ms = steady_now_ms();

            if (tc.tree) {
                tc.tree->add_child(node.id, verify);
                tc.tree->set_state(verify.id, BehaviorNode::Running, verify.start_ms);
            }

            // Build verification prompt — no tools, just review
            std::string vprompt = tc.base_system_prompt;
            vprompt += "\n\n## Task Verification\n";
            vprompt += "User request: " + ctx.root_name + "\n\n";
            vprompt += "Subtask results:\n";
            int fail_count = 0;
            for (auto& child : node.children) {
                if (child.type == BehaviorNode::TypeToolCall ||
                    child.type == BehaviorNode::TypeResponse) continue;
                std::string status = (child.state == BehaviorNode::Done) ? "OK" : "FAILED";
                if (child.state == BehaviorNode::Failed) ++fail_count;
                vprompt += "- [" + status + "] " + child.name + ": " + child.result_summary + "\n";
            }
            vprompt += "\nBased on the results above, give a brief summary of what succeeded and what failed. "
                       "Do NOT attempt to fix anything — just summarize the outcome.";

            // Single LLM call with no tools
            llm::Conversation vconv;
            vconv.push(llm::Message::system(vprompt));
            vconv.push(llm::Message::user("Summarize the task results."));

            llm::ChatParams vparams;
            vparams.temperature = tc.cfg.temperature;
            vparams.maxTokens = tc.cfg.max_tokens;

            try {
                auto vmsgs = vconv.messages;
                auto vresp = do_llm_call(vmsgs, vparams, tc.cfg, tc.on_stream_chunk, tc.cancel);
                accum.input_tokens += vresp.usage.inputTokens;
                accum.output_tokens += vresp.usage.outputTokens;
                if (tc.on_token_update) {
                    tc.on_token_update(accum.input_tokens, accum.output_tokens);
                }
                verify.result_summary = vresp.text().size() > 200
                    ? vresp.text().substr(0, 200) : vresp.text();
            } catch (...) {
                verify.result_summary = std::to_string(fail_count) + " subtask(s) failed";
            }

            // Verify node state reflects whether failures are acceptable
            verify.state = BehaviorNode::Done;
            verify.end_ms = steady_now_ms();

            if (tc.tree) {
                tc.tree->set_state(verify.id, verify.state, verify.end_ms);
                tc.tree->set_result(verify.id, verify.result_summary);
            }
            node.children.push_back(verify);

            // Parent state: if all "real" failures exist, mark as failed
            node.state = (fail_count > 0) ? BehaviorNode::Failed : BehaviorNode::Done;
            node.result_summary = verify.result_summary;
        } else {
            node.state = BehaviorNode::Done;
            node.result_summary = synthesize_children(node);
        }

        node.end_ms = steady_now_ms();
        if (tc.tree) {
            tc.tree->set_state(node.id, node.state, node.end_ms);
            tc.tree->set_result(node.id, node.result_summary);
        }
    }
}

// ─── run_task_tree — main entry point for system-driven recursive task tree ───

export auto run_task_tree(TreeConfig& tc) -> TreeResult {
    TreeResult result;

    // Create root node
    BehaviorNode root;
    root.id = tc.id_alloc ? tc.id_alloc->alloc() : 1;
    root.type = BehaviorNode::TypeRoot;
    root.name = std::string(tc.user_input);
    root.start_ms = steady_now_ms();

    if (tc.tree) {
        tc.tree->set_root(root.id, root.name, "");
    }

    NodeContext root_ctx;
    root_ctx.root_name = root.name;

    process_node(root, tc, 0, result, root_ctx);

    result.reply = root.result_summary;

    // Append this turn to shared conversation for cross-turn context
    if (tc.conversation) {
        tc.conversation->push(llm::Message::user(tc.user_input));
        if (!result.reply.empty()) {
            tc.conversation->push(llm::Message::assistant(result.reply));
        }
    }

    return result;
}

// ─── run_one_turn — backward compatibility wrapper around run_task_tree ───

export auto run_one_turn(TurnConfig& tc) -> TurnResult {
    TreeConfig tree_cfg{
        .user_input = std::string(tc.user_input),
        .base_system_prompt = tc.system_prompt,
        .tools = tc.tools,
        .bridge = tc.bridge,
        .stream = tc.stream,
        .cfg = tc.cfg,
        .conversation = &tc.conversation,
        .policy = tc.policy,
        .confirm_cb = tc.confirm_cb,
        .cancel = tc.cancel,
        .tree = tc.behavior_tree,
        .id_alloc = tc.id_alloc,
        .tracker = tc.tracker,
        .ctx_mgr = tc.ctx_mgr,
        .lua_sandbox = tc.lua_sandbox,
        .on_stream_chunk = tc.on_stream_chunk,
        .on_tool_call = tc.on_tool_call,
        .on_tool_result = tc.on_tool_result,
        .on_token_update = tc.on_token_update,
        .on_auto_compact = tc.on_auto_compact,
    };

    auto tree_result = run_task_tree(tree_cfg);

    TurnResult tr;
    tr.reply = tree_result.reply;
    tr.input_tokens = tree_result.input_tokens;
    tr.output_tokens = tree_result.output_tokens;
    tr.cache_read_tokens = tree_result.cache_read_tokens;
    tr.cache_write_tokens = tree_result.cache_write_tokens;
    return tr;
}

// Compact conversation: keep system prompt + last N messages
export void compact_conversation(llm::Conversation& conv, int keep_recent = 6) {
    if (static_cast<int>(conv.messages.size()) <= keep_recent + 1) return;

    // Find system message if present
    std::optional<llm::Message> system_msg;
    std::size_t start_idx = 0;
    if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) {
        system_msg = conv.messages[0];
        start_idx = 1;
    }

    // Keep last keep_recent messages (excluding system)
    auto total_non_system = conv.messages.size() - start_idx;
    if (static_cast<int>(total_non_system) <= keep_recent) return;

    std::vector<llm::Message> recent(
        conv.messages.end() - keep_recent, conv.messages.end());

    conv.messages.clear();
    if (system_msg) {
        conv.messages.push_back(std::move(*system_msg));
    }
    // Insert a summary placeholder
    conv.messages.push_back(llm::Message::system(
        "[Earlier conversation context was compacted to save tokens. "
        "The most recent messages are preserved below.]"));
    conv.messages.insert(conv.messages.end(), recent.begin(), recent.end());
}

} // namespace xlings::agent
