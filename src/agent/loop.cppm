export module xlings.agent.loop;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.agent.llm_config;
import xlings.agent.approval;
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.agent.tui;
import xlings.libs.soul;
import xlings.runtime.event_stream;
import xlings.runtime.capability;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// Convert ToolBridge ToolDefs to llmapi ToolDefs
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

// Build system prompt
export auto build_system_prompt(const ToolBridge& bridge) -> std::string {
    std::string prompt = R"(You are xlings-agent, an AI assistant specialized in package management and environment setup.

## Available Tools

)";
    // Separate built-in tools from utility tools
    std::vector<std::string> builtin_lines;
    std::vector<std::string> utility_lines;
    for (const auto& td : bridge.tool_definitions()) {
        auto line = "- **" + td.name + "**: " + td.description;
        if (td.name == "run_command" || td.name == "view_output" ||
            td.name == "search_content" || td.name == "set_log_level") {
            utility_lines.push_back(std::move(line));
        } else {
            builtin_lines.push_back(std::move(line));
        }
    }

    prompt += "### Built-in Tools (ALWAYS prefer these)\n";
    for (auto& l : builtin_lines) prompt += l + "\n";

    prompt += R"(
### Utility Tools (use only as last resort)
)";
    for (auto& l : utility_lines) prompt += l + "\n";

    prompt += R"(
## CRITICAL Rules

1. **ALWAYS use built-in tools for package/version operations.** For example:
   - To switch Node.js version → use `use_version`, NEVER `nvm use` via run_command
   - To install a package → use `install_packages`, NEVER `apt install` via run_command
   - To search packages → use `search_packages`, NEVER shell commands
   - To list installed packages → use `list_packages`
   - To check system info → use `system_status`

2. **NEVER use `run_command` for any task that a built-in tool can handle.** The `run_command` tool is ONLY for tasks that have NO built-in equivalent (e.g., checking disk space, reading a file, running a user's custom script).

3. Before calling a tool, briefly explain what you're about to do.
4. After a tool completes, summarize the result for the user.
)";
    return prompt;
}

// Callback type: return true to approve, false to deny
export using ConfirmCallback = std::function<bool(std::string_view tool_name, std::string_view arguments)>;

// Callback for tool call/result display
export using ToolCallCallback = std::function<void(int action_id, std::string_view name, std::string_view args)>;
export using ToolResultCallback = std::function<void(int action_id, std::string_view name, bool is_error)>;
// Callback for auto-compact notification
export using AutoCompactCallback = std::function<void(int evicted_turns, int freed_tokens)>;

// Handle a single tool call with optional approval
auto handle_tool_call(
    const llm::ToolCall& call,
    ToolBridge& bridge,
    EventStream& stream,
    ApprovalPolicy* policy,
    ConfirmCallback confirm_cb
) -> llm::ToolResultContent {

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

    auto exec_result = bridge.execute(call.name, call.arguments, stream);
    return llm::ToolResultContent{
        .toolUseId = call.id,
        .content = exec_result.content,
        .isError = exec_result.isError,
    };
}

// Core loop: user input -> LLM -> Tool -> LLM -> ... -> final reply
// Returns TurnResult with reply + token usage + action nodes
export auto run_one_turn(
    llm::Conversation& conversation,
    std::string_view user_input,
    const std::string& system_prompt,
    const std::vector<llm::ToolDef>& tools,
    ToolBridge& bridge,
    EventStream& stream,
    const LlmConfig& cfg,
    std::function<void(std::string_view)> on_stream_chunk,
    ApprovalPolicy* policy = nullptr,
    ConfirmCallback confirm_cb = {},
    ToolCallCallback on_tool_call = {},
    ToolResultCallback on_tool_result = {},
    ContextManager* ctx_mgr = nullptr,
    TokenTracker* tracker = nullptr,
    AutoCompactCallback on_auto_compact = {}
) -> TurnResult {

    conversation.push(llm::Message::user(user_input));

    llm::ChatParams params;
    params.tools = tools;
    params.temperature = cfg.temperature;
    params.maxTokens = cfg.max_tokens;

    TurnResult turn_result;
    int action_counter = 0;

    constexpr int MAX_ITERATIONS = 20;
    for (int i = 0; i < MAX_ITERATIONS; ++i) {

        // Auto-compact check before each LLM call
        if (ctx_mgr && tracker) {
            int l2_before = ctx_mgr->l2_count();
            int tokens_before = ctx_mgr->total_evicted_tokens();
            if (ctx_mgr->maybe_auto_compact(conversation, *tracker)) {
                int evicted = ctx_mgr->l2_count() - l2_before;
                int freed = ctx_mgr->total_evicted_tokens() - tokens_before;
                turn_result.auto_compacted = true;
                if (on_auto_compact) on_auto_compact(evicted, freed);
            }
        }

        llm::ChatResponse response;

        if (cfg.provider == "anthropic") {
            llm::anthropic::Config acfg{
                .apiKey = cfg.api_key,
                .model = cfg.model,
            };
            if (!cfg.base_url.empty()) acfg.baseUrl = cfg.base_url;
            llm::anthropic::Anthropic provider(std::move(acfg));

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

        // Track LLM call as an action node
        ++action_counter;
        ActionNode llm_action;
        llm_action.id = action_counter;
        llm_action.type = "llm_call";
        llm_action.name = "llm";
        llm_action.input_tokens = response.usage.inputTokens;
        llm_action.output_tokens = response.usage.outputTokens;
        turn_result.actions.push_back(std::move(llm_action));
        turn_result.input_tokens += response.usage.inputTokens;
        turn_result.output_tokens += response.usage.outputTokens;

        // Add assistant response to conversation
        llm::Message assistant_msg;
        assistant_msg.role = llm::Role::Assistant;
        assistant_msg.content = response.content;
        conversation.push(std::move(assistant_msg));

        // Check if tool calls needed
        if (response.stopReason != llm::StopReason::ToolUse) {
            turn_result.reply = response.text();
            return turn_result;
        }

        auto calls = response.tool_calls();
        if (calls.empty()) {
            turn_result.reply = response.text();
            return turn_result;
        }

        // Execute each tool call
        for (const auto& call : calls) {
            ++action_counter;

            // Notify TUI of tool call
            if (on_tool_call) {
                on_tool_call(action_counter, call.name, call.arguments);
            }

            auto result = handle_tool_call(call, bridge, stream, policy, confirm_cb);

            // Track tool call as action node
            ActionNode tool_action;
            tool_action.id = action_counter;
            tool_action.type = "tool_call";
            tool_action.name = call.name;
            turn_result.actions.push_back(std::move(tool_action));

            // Notify TUI of tool result
            if (on_tool_result) {
                on_tool_result(action_counter, call.name, result.isError);
            }

            llm::Message tool_msg;
            tool_msg.role = llm::Role::Tool;
            tool_msg.content = std::vector<llm::ContentPart>{result};
            conversation.push(std::move(tool_msg));
        }

        // Continue loop to let LLM see tool results
    }

    turn_result.reply = "[agent: max iterations reached]";
    return turn_result;
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
