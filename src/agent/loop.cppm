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

// ─── decide tool definition ───

auto decide_tool_def() -> llm::ToolDef {
    return llm::ToolDef{
        .name = "decide",
        .description = "Decide how to handle the current task. "
                       "Choose 'decompose' to split into subtasks (specify tool+args for direct execution), "
                       "or 'done' to accept the current results and finish.",
        .inputSchema = R"JSON({
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "enum": ["decompose", "done"],
      "description": "decompose: split into subtasks. done: accept current results."
    },
    "summary": {
      "type": "string",
      "description": "Required when action=done. Brief summary of outcome."
    },
    "subtasks": {
      "type": "array",
      "description": "Required when action=decompose. Subtasks in execution order.",
      "items": {
        "type": "object",
        "properties": {
          "title": { "type": "string", "description": "Subtask title" },
          "description": { "type": "string", "description": "Subtask description" },
          "tool": { "type": "string", "description": "Tool name for direct execution (system calls automatically)" },
          "args": { "type": "object", "description": "Tool arguments JSON (required when tool is specified)" }
        },
        "required": ["title"]
      }
    }
  },
  "required": ["action"]
})JSON",
    };
}

// Convert ToolBridge ToolDefs to llmapi ToolDefs (real tools only)
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

// Build base system prompt
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

2. **NEVER use `run_command` or `execute_lua` for any task that a built-in tool can handle.**

3. Keep responses short and plain text — no markdown formatting.

## Lua Execution Engine

You can use the `execute_lua` tool for multi-step operations with conditional logic.

### Available APIs
```lua
pkg.search(query), pkg.install(name [,version]), pkg.remove(name)
pkg.list(), pkg.info(name), pkg.update(name)
sys.status(), sys.run(command)
ver.use(name, version)
```)";

    if (!memories.empty()) {
        prompt += "\n## Remembered Context\n\n";
        int count = 0;
        for (auto& m : memories) {
            if (count >= 20) break;
            std::string brief = m.content.size() > 100 ? m.content.substr(0, 100) + "..." : m.content;
            prompt += "- [" + m.category + "] " + brief + "\n";
            ++count;
        }
    }

    return prompt;
}

// ─── Callback types ───

export using ConfirmCallback = std::function<bool(std::string_view tool_name, std::string_view arguments)>;
export using ToolCallCallback = std::function<void(int action_id, std::string_view name, std::string_view args)>;
export using ToolResultCallback = std::function<void(int action_id, std::string_view name, bool is_error)>;
export using AutoCompactCallback = std::function<void(int evicted_turns, int freed_tokens)>;
export using TokenUpdateCallback = std::function<void(int input_tokens, int output_tokens)>;

// ─── Internal types ───

struct SubtaskDef {
    std::string title;
    std::string description;
    std::string tool;
    std::string tool_args;
};

struct Decision {
    std::string action;  // "decompose" or "done"
    std::string summary;  // for "done"
    std::vector<SubtaskDef> subtasks;  // for "decompose"
};

struct NodeContext {
    std::string root_name;
    std::vector<std::string> ancestor_path;
    std::vector<std::pair<std::string, std::string>> sibling_results;
};

// ─── TreeConfig ───

export struct TreeConfig {
    std::string user_input;
    const std::string& base_system_prompt;
    const std::vector<llm::ToolDef>& tools;  // real tools (for prompt listing)
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    llm::Conversation* conversation {nullptr};
    ApprovalPolicy* policy {nullptr};
    ConfirmCallback confirm_cb;
    CancellationToken* cancel {nullptr};
    ABehaviorTree* tree {nullptr};
    IdAllocator* id_alloc {nullptr};
    TokenTracker* tracker {nullptr};
    ContextManager* ctx_mgr {nullptr};
    LuaSandbox* lua_sandbox {nullptr};
    std::function<void(std::string_view)> on_stream_chunk;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    TokenUpdateCallback on_token_update;
    AutoCompactCallback on_auto_compact;
};

export struct TreeResult {
    std::string reply;
    int input_tokens {0};
    int output_tokens {0};
    int cache_read_tokens {0};
    int cache_write_tokens {0};
};

// ─── Keep TurnConfig for backward compat ───

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
    bool is_replan,
    const std::vector<std::pair<std::string, std::string>>& completed_results = {}
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
        prompt += "IMPORTANT: Review these results — do NOT repeat work already done.\n";
        for (auto& [name, summary] : ctx.sibling_results) {
            prompt += "- " + name + ": " + summary + "\n";
        }
    }

    // Re-plan: show completed children results
    if (is_replan && !completed_results.empty()) {
        prompt += "\n## Completed Subtasks\n";
        for (auto& [status_name, summary] : completed_results) {
            prompt += "- " + status_name + ": " + summary + "\n";
        }
        prompt += "\nSome subtasks failed. You can:\n"
                  "- \"done\": accept the current results (if failures are expected/harmless)\n"
                  "- \"decompose\": add NEW subtasks to retry or take alternative approach\n";
    }

    prompt += "\n## Current Task\n";
    prompt += node.name + "\n";
    if (!node.detail.empty()) {
        prompt += node.detail + "\n";
    }

    // Available tool names
    prompt += "\n## Available Tools\n";
    for (auto& t : tools) {
        prompt += "- " + t.name + "\n";
    }

    if (!is_replan) {
        prompt += R"(
Decide how to handle this task. Call the decide tool:

**decompose** — Split into subtasks. Two kinds of subtasks:
  1. **Atom** (with tool+args): system executes directly, zero LLM cost. Use EXACT tool name from list.
  2. **Plan** (title only, no tool+args): needs LLM reasoning. Use for steps that:
     - depend on previous results (e.g. "based on search results, install the right version")
     - involve multiple independent sub-operations (e.g. "handle d2x", "handle mdbook")
     - require conditional logic

  IMPORTANT: Group related operations under Plan subtasks for clarity.
  Example for "uninstall d2x and mdbook":
    subtasks: [
      { "title": "Handle d2x uninstall" },
      { "title": "Handle mdbook uninstall" }
    ]
  NOT a flat list of atoms — that loses the ability to reason about each group.

**done** — If the task is already complete or requires no action, provide a summary.

Call decide with your decision.
)";
    }

    return prompt;
}

// ─── Synthesize children results ───

auto synthesize_children(const BehaviorNode& node) -> std::string {
    int done_count = 0;
    int failed_count = 0;
    int total = 0;
    std::string summary;
    for (auto& child : node.children) {
        ++total;
        if (child.state == BehaviorNode::Done) ++done_count;
        if (child.state == BehaviorNode::Failed) ++failed_count;
        // Use Plan node summaries (LLM-generated), skip raw Atom output
        if (child.type == BehaviorNode::TypePlan && !child.result_summary.empty()) {
            if (!summary.empty()) summary += "; ";
            summary += child.result_summary;
        }
    }
    if (!summary.empty()) {
        return summary.size() > 200 ? summary.substr(0, 200) : summary;
    }
    if (failed_count > 0) {
        return std::to_string(done_count) + " completed, " +
               std::to_string(failed_count) + " failed out of " +
               std::to_string(total) + " tasks";
    }
    return std::to_string(done_count) + "/" + std::to_string(total) + " tasks completed";
}

// ─── Constants ───

inline constexpr int MAX_DEPTH = 5;
inline constexpr int MAX_REPLAN = 3;

// ─── ask_decision — unified for initial + re-plan ───

auto ask_decision(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    const NodeContext& ctx,
    TreeResult& accum,
    bool is_replan = false,
    const std::vector<std::pair<std::string, std::string>>& completed_results = {}
) -> Decision {

    auto prompt = build_scoped_prompt(node, ctx, tc.base_system_prompt, tc.tools,
                                       is_replan, completed_results);
    auto tools = std::vector<llm::ToolDef>{ decide_tool_def() };

    llm::ChatParams params;
    params.tools = tools;
    params.temperature = tc.cfg.temperature;
    params.maxTokens = tc.cfg.max_tokens;

    llm::Conversation conv;
    conv.push(llm::Message::system(prompt));

    // Inject conversation history at root level only
    if (depth == 0 && tc.conversation) {
        for (auto& msg : tc.conversation->messages) {
            if (msg.role == llm::Role::System) continue;
            conv.push(msg);
        }
    }

    std::string user_msg = is_replan
        ? "Review the subtask results and decide: accept (done) or add new subtasks (decompose)."
        : "Task: " + node.name + (node.detail.empty() ? "" : "\n" + node.detail);
    conv.push(llm::Message::user(user_msg));

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
        // No tool call → treat as done with the text as summary
        auto text = response.text();
        return Decision{
            .action = "done",
            .summary = text.empty() ? "completed" : (text.size() > 200 ? text.substr(0, 200) : text),
        };
    }

    auto json = nlohmann::json::parse(calls[0].arguments, nullptr, false);
    if (json.is_discarded()) {
        return Decision{.action = "done", .summary = "completed"};
    }

    auto action = json.value("action", "done");

    if (action == "done") {
        return Decision{
            .action = "done",
            .summary = json.value("summary", "completed"),
        };
    }

    if (action == "decompose" && json.contains("subtasks") && json["subtasks"].is_array()) {
        Decision d{.action = "decompose"};
        for (auto& s : json["subtasks"]) {
            SubtaskDef sub;
            sub.title = s.value("title", "untitled");
            sub.description = s.value("description", "");
            if (s.contains("tool") && s["tool"].is_string() && !s["tool"].get<std::string>().empty()) {
                auto tool_name = s["tool"].get<std::string>();
                // Validate tool name
                bool valid = (tc.bridge.tool_info(tool_name).source != "unknown");
                // Also allow execute_lua as a virtual tool
                if (!valid && tool_name == "execute_lua" && tc.lua_sandbox) valid = true;
                if (valid) {
                    sub.tool = tool_name;
                    sub.tool_args = s.contains("args") ? s["args"].dump() : "{}";
                }
            }
            // At MAX_DEPTH, drop Plan subtasks (no tool+args)
            if (depth >= MAX_DEPTH && sub.tool.empty()) continue;
            d.subtasks.push_back(std::move(sub));
        }
        if (d.subtasks.empty()) {
            return Decision{.action = "done", .summary = json.value("summary", "completed")};
        }
        return d;
    }

    return Decision{.action = "done", .summary = "completed"};
}

// ─── Helper: create children from subtask defs ───

void create_children(
    BehaviorNode& node,
    const std::vector<SubtaskDef>& subtasks,
    TreeConfig& tc
) {
    for (auto& sub : subtasks) {
        BehaviorNode child;
        child.id = tc.id_alloc ? tc.id_alloc->alloc() : 0;
        child.name = sub.title;
        child.detail = sub.description;
        child.tool = sub.tool;
        child.tool_args = sub.tool_args;
        child.type = child.is_atom() ? BehaviorNode::TypeAtom : BehaviorNode::TypePlan;
        if (tc.tree) tc.tree->add_child(node.id, child);
        node.children.push_back(std::move(child));
    }
}

// ─── Helper: execute pending children ───

void execute_pending_children(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    TreeResult& accum,
    const NodeContext& ctx
);

// Forward declare process_node
void process_node(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    TreeResult& accum,
    const NodeContext& ctx
);

void execute_pending_children(
    BehaviorNode& node,
    TreeConfig& tc,
    int depth,
    TreeResult& accum,
    const NodeContext& ctx
) {
    for (std::size_t ci = 0; ci < node.children.size(); ++ci) {
        auto& child = node.children[ci];
        if (child.state != BehaviorNode::Pending) continue;

        // Build child context with sibling results
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
    }
}

// ─── Helper: check for failed children ───

auto has_failed_children(const BehaviorNode& node) -> bool {
    for (auto& child : node.children) {
        if (child.state == BehaviorNode::Failed) return true;
    }
    return false;
}

// ─── Helper: build completed results for re-plan ───

auto build_completed_results(const BehaviorNode& node)
    -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> results;
    for (auto& child : node.children) {
        if (!child.is_terminal()) continue;
        std::string status = (child.state == BehaviorNode::Done) ? "[OK]"
            : (child.state == BehaviorNode::Failed) ? "[FAILED]" : "[SKIPPED]";
        std::string display_name = child.is_atom()
            ? child.tool + "(" + (child.tool_args.size() > 40
                ? child.tool_args.substr(0, 40) + "..." : child.tool_args) + ")"
            : child.name;
        results.push_back({status + " " + display_name, child.result_summary});
    }
    return results;
}

// ─── process_node — recursive DFS with re-plan ───

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

    // ── Atom: system direct tool call, zero LLM ──
    if (node.is_atom()) {
        node.type = BehaviorNode::TypeAtom;

        // Notify TUI
        if (tc.on_tool_call) {
            auto args_preview = node.tool_args.size() > 60
                ? node.tool_args.substr(0, 60) + "..." : node.tool_args;
            tc.on_tool_call(node.id, node.tool, args_preview);
        }

        // Approval check for destructive tools
        if (tc.policy) {
            auto info = tc.bridge.tool_info(node.tool);
            capability::CapabilitySpec spec;
            spec.name = info.name;
            spec.destructive = info.destructive;
            auto approval = tc.policy->check(spec, node.tool_args);

            if (approval == ApprovalResult::Denied) {
                node.state = BehaviorNode::Failed;
                node.result_summary = "denied by approval policy";
                node.end_ms = steady_now_ms();
                if (tc.on_tool_result) tc.on_tool_result(node.id, node.tool, true);
                if (tc.tree) {
                    tc.tree->set_state(node.id, node.state, node.end_ms);
                    tc.tree->set_result(node.id, node.result_summary);
                }
                return;
            }
            if (approval == ApprovalResult::NeedConfirm && tc.confirm_cb) {
                if (!tc.confirm_cb(node.tool, node.tool_args)) {
                    node.state = BehaviorNode::Failed;
                    node.result_summary = "denied by user";
                    node.end_ms = steady_now_ms();
                    if (tc.on_tool_result) tc.on_tool_result(node.id, node.tool, true);
                    if (tc.tree) {
                        tc.tree->set_state(node.id, node.state, node.end_ms);
                        tc.tree->set_result(node.id, node.result_summary);
                    }
                    return;
                }
            }
        }

        // Execute: handle execute_lua specially
        if (node.tool == "execute_lua" && tc.lua_sandbox) {
            auto json = nlohmann::json::parse(node.tool_args, nullptr, false);
            auto code = json.is_discarded() ? "" : json.value("code", "");
            if (code.empty()) {
                node.state = BehaviorNode::Failed;
                node.result_summary = "'code' parameter required";
            } else {
                Action lua_action;
                lua_action.layer = LayerDecision;
                lua_action.name = "execute_lua";
                lua_action.detail = code;
                auto exec_log = tc.lua_sandbox->execute(code, lua_action);
                node.state = (exec_log.status == "completed") ? BehaviorNode::Done : BehaviorNode::Failed;
                auto log_json = exec_log.to_json();
                node.result_summary = log_json.size() > 200 ? log_json.substr(0, 200) : log_json;
            }
        } else {
            // Regular tool execution
            auto exec_result = tc.bridge.execute(node.tool, node.tool_args, tc.stream, tc.cancel);
            node.state = exec_result.isError ? BehaviorNode::Failed : BehaviorNode::Done;
            node.result_summary = exec_result.content.size() > 200
                ? exec_result.content.substr(0, 200) : exec_result.content;
        }

        node.end_ms = steady_now_ms();
        if (tc.on_tool_result) {
            tc.on_tool_result(node.id, node.tool, node.state == BehaviorNode::Failed);
        }
        if (tc.tree) {
            tc.tree->set_state(node.id, node.state, node.end_ms);
            tc.tree->set_result(node.id, node.result_summary);
        }
        return;
    }

    // ── Plan: LLM decision ──
    node.type = BehaviorNode::TypePlan;

    // Update TUI status
    if (tc.on_tool_call) {
        tc.on_tool_call(node.id, "decide", node.name);
    }

    auto decision = ask_decision(node, tc, depth, ctx, accum);

    if (tc.on_tool_result) {
        tc.on_tool_result(node.id, "decide", false);
    }

    // ── action: done → immediate completion ──
    if (decision.action == "done") {
        node.state = BehaviorNode::Done;
        node.result_summary = decision.summary;
        node.end_ms = steady_now_ms();
        if (tc.tree) {
            tc.tree->set_state(node.id, node.state, node.end_ms);
            tc.tree->set_result(node.id, node.result_summary);
        }
        return;
    }

    // ── action: decompose → create children + DFS + re-plan loop ──
    create_children(node, decision.subtasks, tc);

    for (int replan = 0; replan <= MAX_REPLAN; ++replan) {
        // Execute all pending children
        execute_pending_children(node, tc, depth, accum, ctx);

        // All done?
        if (!has_failed_children(node)) {
            node.state = BehaviorNode::Done;
            node.result_summary = synthesize_children(node);
            node.end_ms = steady_now_ms();
            if (tc.tree) {
                tc.tree->set_state(node.id, node.state, node.end_ms);
                tc.tree->set_result(node.id, node.result_summary);
            }
            return;
        }

        // Max re-plan reached?
        if (replan == MAX_REPLAN) break;

        // Re-plan: ask LLM to review results
        auto completed = build_completed_results(node);

        if (tc.on_tool_call) {
            tc.on_tool_call(node.id, "re-plan", node.name);
        }

        auto replan_decision = ask_decision(node, tc, depth, ctx, accum,
                                             /*is_replan=*/true, completed);

        if (tc.on_tool_result) {
            tc.on_tool_result(node.id, "re-plan", false);
        }

        if (replan_decision.action == "done") {
            // Accept current results — add visible marker
            BehaviorNode marker;
            marker.id = tc.id_alloc ? tc.id_alloc->alloc() : 0;
            marker.type = BehaviorNode::TypePlan;
            marker.name = "re-plan: " + replan_decision.summary;
            marker.state = BehaviorNode::Done;
            marker.result_summary = replan_decision.summary;
            marker.start_ms = steady_now_ms();
            marker.end_ms = marker.start_ms;
            if (tc.tree) tc.tree->add_child(node.id, marker);
            node.children.push_back(std::move(marker));

            node.state = BehaviorNode::Done;
            node.result_summary = replan_decision.summary;
            node.end_ms = steady_now_ms();
            if (tc.tree) {
                tc.tree->set_state(node.id, node.state, node.end_ms);
                tc.tree->set_result(node.id, node.result_summary);
            }
            return;
        }

        // Wrap re-plan's new children under a visible Plan sub-node
        BehaviorNode replan_node;
        replan_node.id = tc.id_alloc ? tc.id_alloc->alloc() : 0;
        replan_node.type = BehaviorNode::TypePlan;
        replan_node.name = "re-plan #" + std::to_string(replan + 1);
        replan_node.state = BehaviorNode::Running;
        replan_node.start_ms = steady_now_ms();
        if (tc.tree) tc.tree->add_child(node.id, replan_node);
        node.children.push_back(replan_node);

        auto& rnode = node.children.back();
        create_children(rnode, replan_decision.subtasks, tc);

        // Build context for re-plan children
        NodeContext replan_ctx;
        replan_ctx.root_name = ctx.root_name;
        replan_ctx.ancestor_path = ctx.ancestor_path;
        replan_ctx.ancestor_path.push_back(node.name);
        // Pass all previous results as sibling context
        for (auto& [sn, ss] : completed) {
            replan_ctx.sibling_results.push_back({sn, ss});
        }

        execute_pending_children(rnode, tc, depth + 1, accum, replan_ctx);

        // Update re-plan node state
        rnode.state = has_failed_children(rnode) ? BehaviorNode::Failed : BehaviorNode::Done;
        rnode.result_summary = synthesize_children(rnode);
        rnode.end_ms = steady_now_ms();
        if (tc.tree) {
            tc.tree->set_state(rnode.id, rnode.state, rnode.end_ms);
            tc.tree->set_result(rnode.id, rnode.result_summary);
        }
    }

    // Max re-plan exceeded
    node.state = BehaviorNode::Failed;
    node.result_summary = synthesize_children(node);
    node.end_ms = steady_now_ms();
    if (tc.tree) {
        tc.tree->set_state(node.id, node.state, node.end_ms);
        tc.tree->set_result(node.id, node.result_summary);
    }
}

// ─── run_task_tree — main entry point ───

export auto run_task_tree(TreeConfig& tc) -> TreeResult {
    TreeResult result;

    BehaviorNode root;
    root.id = tc.id_alloc ? tc.id_alloc->alloc() : 1;
    root.type = BehaviorNode::TypePlan;
    root.name = std::string(tc.user_input);
    root.start_ms = steady_now_ms();

    if (tc.tree) {
        tc.tree->set_root(root.id, root.name, "");
    }

    NodeContext root_ctx;
    root_ctx.root_name = root.name;

    process_node(root, tc, 0, result, root_ctx);

    result.reply = root.result_summary;

    // Append to shared conversation for cross-turn context
    if (tc.conversation) {
        tc.conversation->push(llm::Message::user(tc.user_input));
        if (!result.reply.empty()) {
            tc.conversation->push(llm::Message::assistant(result.reply));
        }
    }

    return result;
}

// ─── run_one_turn — backward compat wrapper ───

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

// Compact conversation
export void compact_conversation(llm::Conversation& conv, int keep_recent = 6) {
    if (static_cast<int>(conv.messages.size()) <= keep_recent + 1) return;

    std::optional<llm::Message> system_msg;
    std::size_t start_idx = 0;
    if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) {
        system_msg = conv.messages[0];
        start_idx = 1;
    }

    auto total_non_system = conv.messages.size() - start_idx;
    if (static_cast<int>(total_non_system) <= keep_recent) return;

    std::vector<llm::Message> recent(
        conv.messages.end() - keep_recent, conv.messages.end());

    conv.messages.clear();
    if (system_msg) {
        conv.messages.push_back(std::move(*system_msg));
    }
    conv.messages.push_back(llm::Message::system(
        "[Earlier conversation context was compacted. Recent messages below.]"));
    conv.messages.insert(conv.messages.end(), recent.begin(), recent.end());
}

} // namespace xlings::agent
