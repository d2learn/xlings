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
import xlings.libs.json;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.runtime.cancellation;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// manage_tree tool definition (virtual — not registered in CapabilityRegistry)
auto manage_tree_tool_def() -> llm::ToolDef {
    return llm::ToolDef{
        .name = "manage_tree",
        .description = "Manage the task tree: decompose tasks into subtasks, track progress, update plans. "
                       "Supports batch mode to execute multiple operations in one call.",
        .inputSchema = R"JSON({
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "enum": ["add_task", "start_task", "complete_task", "cancel_task", "update_task", "batch"],
      "description": "The tree operation to perform. Use 'batch' to execute multiple operations at once."
    },
    "parent_id": {
      "type": "integer",
      "description": "Parent node ID (for add_task). Use 0 for root level."
    },
    "node_id": {
      "type": "integer",
      "description": "Target node ID (for start/complete/cancel/update)."
    },
    "title": {
      "type": "string",
      "description": "Task title (for add_task/update_task)."
    },
    "details": {
      "type": "string",
      "description": "Optional JSON details or notes."
    },
    "result": {
      "type": "string",
      "description": "Completion result summary (for complete_task)."
    },
    "operations": {
      "type": "array",
      "description": "Array of operations for batch mode. Each element has the same schema as a single manage_tree call.",
      "items": {
        "type": "object",
        "properties": {
          "action": {"type": "string"},
          "parent_id": {"type": "integer"},
          "node_id": {"type": "integer"},
          "title": {"type": "string"},
          "details": {"type": "string"},
          "result": {"type": "string"}
        },
        "required": ["action"]
      }
    }
  },
  "required": ["action"]
})JSON",
    };
}

// Convert ToolBridge ToolDefs to llmapi ToolDefs, plus manage_tree
export auto to_llmapi_tools(const ToolBridge& bridge) -> std::vector<llm::ToolDef> {
    std::vector<llm::ToolDef> tools;
    for (const auto& td : bridge.tool_definitions()) {
        tools.push_back(llm::ToolDef{
            .name = td.name,
            .description = td.description,
            .inputSchema = td.inputSchema,
        });
    }
    // Add virtual manage_tree tool
    tools.push_back(manage_tree_tool_def());
    return tools;
}

// Build system prompt (tool definitions NOT listed here — they are in params.tools)
export auto build_system_prompt([[maybe_unused]] const ToolBridge& bridge) -> std::string {
    return R"(You are xlings-agent, an AI assistant specialized in package management and environment setup.

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

## Task Management

You have a `manage_tree` tool to structure your work as a task tree.

### Workflow — use batch mode to minimize tool calls:
1. When receiving a user request, decompose into subtasks using a single `manage_tree(batch)` call:
   ```json
   {"action":"batch","operations":[
     {"action":"add_task","parent_id":0,"title":"Search for packages"},
     {"action":"add_task","parent_id":0,"title":"Install version"},
     {"action":"add_task","parent_id":0,"title":"Verify installation"},
     {"action":"start_task","node_id":1}
   ]}
   ```
2. Execute the started task using the appropriate tools.
3. Complete and start the next task (can also batch):
   ```json
   {"action":"batch","operations":[
     {"action":"complete_task","node_id":1,"result":"found v0.4.40"},
     {"action":"start_task","node_id":2}
   ]}
   ```
4. If a subtask needs further decomposition, add child tasks under it.
5. Cancel unneeded tasks with `cancel_task`, modify with `update_task`.

### Rules:
- **Prefer batch mode** — combine add/start/complete operations into single calls.
- Every tool call automatically nests under the currently active task.
- A subtask can itself contain sub-subtasks (recursive decomposition).
- Completing a task automatically activates the next sibling or returns to parent.
- In batch mode, `node_id` can reference nodes created earlier in the same batch (IDs are assigned sequentially).

### Response Format:
Start every reply with a one-line title summarizing your action or decision.
Then provide details on subsequent lines.
)";
}

// Callback types
export using ConfirmCallback = std::function<bool(std::string_view tool_name, std::string_view arguments)>;
export using ToolCallCallback = std::function<void(int action_id, std::string_view name, std::string_view args)>;
export using ToolResultCallback = std::function<void(int action_id, std::string_view name, bool is_error)>;
export using AutoCompactCallback = std::function<void(int evicted_turns, int freed_tokens)>;
export using TreeUpdateCallback = std::function<void(const std::string& action, int node_id, const std::string& title)>;
export using TokenUpdateCallback = std::function<void(int input_tokens, int output_tokens)>;

// Handle manage_tree virtual tool call
auto handle_manage_tree(
    const llm::ToolCall& call,
    tui::TaskTree& task_tree,
    tui::TreeNode& root,
    TreeUpdateCallback on_tree_update
) -> llm::ToolResultContent {
    auto json = nlohmann::json::parse(call.arguments, nullptr, false);
    if (json.is_discarded()) {
        return llm::ToolResultContent{
            .toolUseId = call.id,
            .content = R"({"error":"invalid JSON arguments"})",
            .isError = true,
        };
    }

    auto action = json.value("action", "");
    nlohmann::json result_json;

    if (action == "add_task") {
        int parent_id = json.value("parent_id", 0);
        auto title = json.value("title", "");
        auto details = json.value("details", "");
        if (title.empty()) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"title is required for add_task"})",
                .isError = true,
            };
        }
        int node_id = task_tree.add_task(root, parent_id, title, details);
        result_json = {{"ok", true}, {"node_id", node_id}, {"action", "add_task"}, {"title", title}};
        if (on_tree_update) on_tree_update("add_task", node_id, title);

    } else if (action == "start_task") {
        int node_id = json.value("node_id", -1);
        if (node_id <= 0) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"node_id is required for start_task"})",
                .isError = true,
            };
        }
        task_tree.start_task(root, node_id);
        result_json = {{"ok", true}, {"action", "start_task"}, {"node_id", node_id}};
        if (on_tree_update) {
            auto* node = root.find_node(node_id);
            on_tree_update("start_task", node_id, node ? node->title : "");
        }

    } else if (action == "complete_task") {
        int node_id = json.value("node_id", -1);
        auto task_result = json.value("result", "");
        if (node_id <= 0) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"node_id is required for complete_task"})",
                .isError = true,
            };
        }
        task_tree.complete_task(root, node_id, tui::TreeNode::Done, task_result);
        result_json = {{"ok", true}, {"action", "complete_task"}, {"node_id", node_id}};
        if (on_tree_update) on_tree_update("complete_task", node_id, "");

    } else if (action == "cancel_task") {
        int node_id = json.value("node_id", -1);
        if (node_id <= 0) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"node_id is required for cancel_task"})",
                .isError = true,
            };
        }
        task_tree.complete_task(root, node_id, tui::TreeNode::Cancelled);
        result_json = {{"ok", true}, {"action", "cancel_task"}, {"node_id", node_id}};
        if (on_tree_update) on_tree_update("cancel_task", node_id, "");

    } else if (action == "update_task") {
        int node_id = json.value("node_id", -1);
        auto new_title = json.value("title", "");
        auto new_details = json.value("details", "");
        if (node_id <= 0) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"node_id is required for update_task"})",
                .isError = true,
            };
        }
        task_tree.update_task(root, node_id, new_title, new_details);
        result_json = {{"ok", true}, {"action", "update_task"}, {"node_id", node_id}};
        if (on_tree_update) on_tree_update("update_task", node_id, new_title);

    } else if (action == "batch") {
        if (!json.contains("operations") || !json["operations"].is_array()) {
            return llm::ToolResultContent{
                .toolUseId = call.id,
                .content = R"({"error":"operations must be a JSON array for batch action"})",
                .isError = true,
            };
        }
        nlohmann::json results = nlohmann::json::array();
        for (auto& op : json["operations"]) {
            auto sub_action = op.value("action", "");
            if (sub_action == "add_task") {
                int parent_id = op.value("parent_id", 0);
                auto title = op.value("title", "");
                auto details = op.value("details", "");
                if (title.empty()) {
                    results.push_back({{"ok", false}, {"action", sub_action}, {"error", "title is required for add_task"}});
                } else {
                    int node_id = task_tree.add_task(root, parent_id, title, details);
                    results.push_back({{"ok", true}, {"action", sub_action}, {"node_id", node_id}, {"title", title}});
                    if (on_tree_update) on_tree_update("add_task", node_id, title);
                }
            } else if (sub_action == "start_task") {
                int node_id = op.value("node_id", -1);
                if (node_id <= 0) {
                    results.push_back({{"ok", false}, {"action", sub_action}, {"error", "node_id is required for start_task"}});
                } else {
                    task_tree.start_task(root, node_id);
                    results.push_back({{"ok", true}, {"action", sub_action}, {"node_id", node_id}});
                    if (on_tree_update) {
                        auto* node = root.find_node(node_id);
                        on_tree_update("start_task", node_id, node ? node->title : "");
                    }
                }
            } else if (sub_action == "complete_task") {
                int node_id = op.value("node_id", -1);
                auto task_result = op.value("result", "");
                if (node_id <= 0) {
                    results.push_back({{"ok", false}, {"action", sub_action}, {"error", "node_id is required for complete_task"}});
                } else {
                    task_tree.complete_task(root, node_id, tui::TreeNode::Done, task_result);
                    results.push_back({{"ok", true}, {"action", sub_action}, {"node_id", node_id}});
                    if (on_tree_update) on_tree_update("complete_task", node_id, "");
                }
            } else if (sub_action == "cancel_task") {
                int node_id = op.value("node_id", -1);
                if (node_id <= 0) {
                    results.push_back({{"ok", false}, {"action", sub_action}, {"error", "node_id is required for cancel_task"}});
                } else {
                    task_tree.complete_task(root, node_id, tui::TreeNode::Cancelled);
                    results.push_back({{"ok", true}, {"action", sub_action}, {"node_id", node_id}});
                    if (on_tree_update) on_tree_update("cancel_task", node_id, "");
                }
            } else if (sub_action == "update_task") {
                int node_id = op.value("node_id", -1);
                auto new_title = op.value("title", "");
                auto new_details = op.value("details", "");
                if (node_id <= 0) {
                    results.push_back({{"ok", false}, {"action", sub_action}, {"error", "node_id is required for update_task"}});
                } else {
                    task_tree.update_task(root, node_id, new_title, new_details);
                    results.push_back({{"ok", true}, {"action", sub_action}, {"node_id", node_id}});
                    if (on_tree_update) on_tree_update("update_task", node_id, new_title);
                }
            } else {
                results.push_back({{"ok", false}, {"action", sub_action}, {"error", "unknown sub-action: " + sub_action}});
            }
        }
        result_json = {{"ok", true}, {"action", "batch"}, {"results", results}};

    } else {
        return llm::ToolResultContent{
            .toolUseId = call.id,
            .content = R"({"error":"unknown action: )" + action + R"("})",
            .isError = true,
        };
    }

    return llm::ToolResultContent{
        .toolUseId = call.id,
        .content = result_json.dump(),
        .isError = false,
    };
}

// Handle a single tool call with optional approval
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
    AutoCompactCallback on_auto_compact = {},
    CancellationToken* cancel = nullptr,
    tui::TaskTree* task_tree = nullptr,
    tui::TreeNode* tree_root = nullptr,
    TreeUpdateCallback on_tree_update = {},
    TokenUpdateCallback on_token_update = {}
) -> TurnResult {

    conversation.push(llm::Message::user(user_input));

    llm::ChatParams params;
    params.tools = tools;
    params.temperature = cfg.temperature;
    params.maxTokens = cfg.max_tokens;

    TurnResult turn_result;
    int action_counter = 0;

    constexpr int MAX_ITERATIONS = 40;
    for (int i = 0; i < MAX_ITERATIONS; ++i) {

        // Check cancellation/pause before each LLM call
        if (cancel && !cancel->is_active()) {
            if (cancel->is_paused()) throw PausedException{};
            throw CancelledException{};
        }

        // Auto-compact check before each LLM call
        if (ctx_mgr && tracker) {
            int l2_before = ctx_mgr->l2_count();
            int tokens_before = ctx_mgr->total_evicted_tokens();
            if (ctx_mgr->maybe_auto_compact(conversation, *tracker)) {
                int evicted = ctx_mgr->l2_count() - l2_before;
                int freed = ctx_mgr->total_evicted_tokens() - tokens_before;
                turn_result.auto_compacted = true;
                if (on_auto_compact && (evicted > 0 || freed > 0)) on_auto_compact(evicted, freed);
            }
        }

        llm::ChatResponse response;

        // ── Cancellable LLM call via worker thread ──
        auto abandoned = std::make_shared<std::atomic<bool>>(false);
        auto done_flag = std::make_shared<std::atomic<bool>>(false);
        auto resp_ptr = std::make_shared<llm::ChatResponse>();
        auto err_ptr = std::make_shared<std::exception_ptr>();
        auto cv_mtx = std::make_shared<std::mutex>();
        auto cv_done = std::make_shared<std::condition_variable>();

        bool has_stream_cb = static_cast<bool>(on_stream_chunk);
        auto safe_chunk = [abandoned, &on_stream_chunk](std::string_view chunk) {
            if (abandoned->load(std::memory_order_acquire)) throw CancelledException{};
            if (on_stream_chunk) on_stream_chunk(chunk);
        };

        // Build messages snapshot
        auto msgs = conversation.messages;
        if (msgs.empty() || msgs[0].role != llm::Role::System) {
            msgs.insert(msgs.begin(), llm::Message::system(system_prompt));
        }

        if (cfg.provider == "anthropic") {
            llm::anthropic::Config acfg{
                .apiKey = cfg.api_key,
                .model = cfg.model,
            };
            if (!cfg.base_url.empty()) acfg.baseUrl = cfg.base_url;

            std::thread worker([done_flag, resp_ptr, err_ptr, cv_mtx, cv_done,
                                provider = llm::anthropic::Anthropic(std::move(acfg)),
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
        } else {
            llm::openai::Config ocfg{
                .apiKey = cfg.api_key,
                .model = cfg.model,
            };
            if (!cfg.base_url.empty()) ocfg.baseUrl = cfg.base_url;

            std::thread worker([done_flag, resp_ptr, err_ptr, cv_mtx, cv_done,
                                provider = llm::openai::OpenAI(std::move(ocfg)),
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
        }

        if (*err_ptr) std::rethrow_exception(*err_ptr);
        response = std::move(*resp_ptr);

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

        // Real-time token update callback
        if (on_token_update) {
            on_token_update(turn_result.input_tokens, turn_result.output_tokens);
        }

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

            // Check cancellation/pause before each tool call
            if (cancel && !cancel->is_active()) {
                if (cancel->is_paused()) throw PausedException{};
                throw CancelledException{};
            }

            // Intercept manage_tree virtual tool
            if (call.name == "manage_tree" && task_tree && tree_root) {
                auto result = handle_manage_tree(call, *task_tree, *tree_root, on_tree_update);

                ActionNode tool_action;
                tool_action.id = action_counter;
                tool_action.type = "tool_call";
                tool_action.name = "manage_tree";
                turn_result.actions.push_back(std::move(tool_action));

                llm::Message tool_msg;
                tool_msg.role = llm::Role::Tool;
                tool_msg.content = std::vector<llm::ContentPart>{result};
                conversation.push(std::move(tool_msg));
                continue;
            }

            // Notify TUI of tool call
            if (on_tool_call) {
                on_tool_call(action_counter, call.name, call.arguments);
            }

            auto result = handle_tool_call(call, bridge, stream, policy, confirm_cb, cancel);

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
