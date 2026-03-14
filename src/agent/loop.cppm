export module xlings.agent.loop;

import std;
import mcpplibs.llmapi;
import xlings.agent.tool_bridge;
import xlings.agent.llm;
import xlings.agent.approval;
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.agent.behavior_tree;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.runtime.cancellation;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// ═══════════════════════════════════════════════════════════════
//  Types
// ═══════════════════════════════════════════════════════════════

export using ConfirmCallback = std::function<bool(std::string_view tool_name, std::string_view arguments)>;
export using ToolCallCallback = std::function<void(int action_id, std::string_view name, std::string_view args)>;
export using ToolResultCallback = std::function<void(int action_id, std::string_view name, bool is_error)>;
export using TokenUpdateCallback = std::function<void(int input_tokens, int output_tokens)>;

export struct LoopResult {
    std::string reply;
};

// ═══════════════════════════════════════════════════════════════
//  Agent Loop — LLM ↔ tool execution cycle
// ═══════════════════════════════════════════════════════════════

inline constexpr int MAX_TOOL_ROUNDS = 20;

export struct AgentLoopConfig {
    std::string user_input;
    const std::string& system_prompt;
    const std::vector<llm::ToolDef>& tools;
    ToolBridge& bridge;
    EventStream& stream;
    const LlmConfig& cfg;
    llm::Conversation& conversation;
    ApprovalPolicy* policy {nullptr};
    ConfirmCallback confirm_cb;
    CancellationToken* cancel {nullptr};
    ABehaviorTree* tree {nullptr};
    IdAllocator* id_alloc {nullptr};
    TokenTracker* tracker {nullptr};
    ContextManager* ctx_mgr {nullptr};
    std::function<void(std::string_view)> on_stream_chunk;
    ToolCallCallback on_tool_call;
    ToolResultCallback on_tool_result;
    TokenUpdateCallback on_token_update;
};

export auto agent_loop(AgentLoopConfig& ac) -> LoopResult {
    LoopResult result;
    auto& conv = ac.conversation;

    if (ac.tracker) ac.tracker->begin_turn();

    // Auto-compact before this turn
    if (ac.ctx_mgr && ac.tracker) {
        ac.ctx_mgr->maybe_auto_compact(conv, *ac.tracker);
    }

    // TUI root node
    int root_id = ac.id_alloc ? ac.id_alloc->alloc() : 1;
    if (ac.tree) ac.tree->set_root(root_id, ac.user_input, "");

    // Append user message
    conv.push(llm::Message::user(ac.user_input));

    llm::ChatParams params;
    params.tools = ac.tools;
    params.temperature = ac.cfg.temperature;
    params.maxTokens = ac.cfg.max_tokens;

    // ─── LLM ↔ tool cycle ───
    for (int round = 0; round < MAX_TOOL_ROUNDS; ++round) {
        if (ac.cancel && !ac.cancel->is_active()) {
            if (ac.cancel->is_paused()) throw PausedException{};
            throw CancelledException{};
        }

        if (ac.tree) ac.tree->clear_streaming();

        // Build messages: system prompt + L2 prefix + conversation history
        std::vector<llm::Message> msgs;
        msgs.push_back(llm::Message::system(ac.system_prompt));
        if (ac.ctx_mgr) {
            auto prefix = ac.ctx_mgr->build_context_prefix();
            if (!prefix.empty()) msgs.push_back(llm::Message::system(prefix));
        }
        for (auto& msg : conv.messages) {
            if (msg.role == llm::Role::System) continue;
            msgs.push_back(msg);
        }

        auto llm_start = steady_now_ms();
        auto response = do_llm_call(msgs, params, ac.cfg, ac.on_stream_chunk, ac.cancel);
        auto llm_end = steady_now_ms();

        if (ac.tracker) {
            ac.tracker->record(response.usage.inputTokens, response.usage.outputTokens,
                               response.usage.cacheReadTokens, response.usage.cacheCreationTokens);
            if (ac.on_token_update)
                ac.on_token_update(ac.tracker->turn_input(), ac.tracker->turn_output());
        }
        if (ac.tree) ac.tree->clear_streaming();

        auto calls = response.tool_calls();

        // No tool calls → done
        if (calls.empty()) {
            result.reply = response.text();
            conv.push(llm::Message::assistant(result.reply));
            break;
        }

        // If LLM provided reasoning text alongside tool calls, add thinking node
        auto reasoning = response.text();
        if (!reasoning.empty() && ac.tree && ac.id_alloc) {
            ac.tree->add_thinking(root_id, ac.id_alloc->alloc(), reasoning, llm_start, llm_end);
        }

        // Append assistant message with tool_use content
        llm::Message asst_msg;
        asst_msg.role = llm::Role::Assistant;
        asst_msg.content = response.content;
        conv.push(asst_msg);

        // Execute tool calls
        struct ToolExec { std::string call_id, tool_name, content; bool is_error; };
        std::vector<ToolExec> tool_results;

        for (auto& call : calls) {
            int node_id = ac.id_alloc ? ac.id_alloc->alloc() : 0;
            if (ac.on_tool_call) {
                auto preview = call.arguments.size() > 60 ? call.arguments.substr(0, 60) + "..." : call.arguments;
                ac.on_tool_call(node_id, call.name, preview);
            }
            if (ac.tree) {
                BehaviorNode child;
                child.id = node_id;
                child.type = BehaviorNode::TypeAtom;
                child.name = call.name;
                child.tool = call.name;
                child.tool_args = call.arguments;
                child.start_ms = steady_now_ms();
                child.state = BehaviorNode::Running;
                ac.tree->add_child(root_id, child);
                ac.tree->set_active(node_id);
            }

            // Approval
            bool denied = false;
            if (ac.policy) {
                auto info = ac.bridge.tool_info(call.name);
                capability::CapabilitySpec spec;
                spec.name = info.name;
                spec.destructive = info.destructive;
                auto approval = ac.policy->check(spec, call.arguments);
                if (approval == ApprovalResult::Denied) {
                    denied = true;
                    tool_results.push_back({call.id, call.name, "denied by approval policy", true});
                } else if (approval == ApprovalResult::NeedConfirm && ac.confirm_cb) {
                    if (!ac.confirm_cb(call.name, call.arguments)) {
                        denied = true;
                        tool_results.push_back({call.id, call.name, "denied by user", true});
                    }
                }
            }
            if (!denied) {
                auto exec = ac.bridge.execute(call.name, call.arguments, ac.stream, ac.cancel);
                tool_results.push_back({call.id, call.name, exec.content, exec.isError});
            }

            // Update tree node
            auto& tr = tool_results.back();
            if (ac.tree) {
                ac.tree->set_state(node_id, tr.is_error ? BehaviorNode::Failed : BehaviorNode::Done, steady_now_ms());
                ac.tree->set_result(node_id, tr.content.size() > 200 ? tr.content.substr(0, 200) : tr.content);
            }
            if (ac.on_tool_result) ac.on_tool_result(node_id, call.name, tr.is_error);
        }

        // Append tool results to conversation
        for (auto& tr : tool_results) {
            llm::Message rm;
            rm.role = llm::Role::Tool;
            std::vector<llm::ContentPart> parts;
            parts.push_back(llm::ToolResultContent{
                .toolUseId = tr.call_id, .content = tr.content, .isError = tr.is_error});
            rm.content = std::move(parts);
            conv.push(std::move(rm));
        }
    }

    // Finalize TUI root
    if (ac.tree) {
        ac.tree->set_state(root_id, BehaviorNode::Done, steady_now_ms());
        ac.tree->set_result(root_id, result.reply.size() > 200 ? result.reply.substr(0, 200) : result.reply);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
//  Conversation utilities
// ═══════════════════════════════════════════════════════════════

export void compact_conversation(llm::Conversation& conv, int keep_recent = 6) {
    if (static_cast<int>(conv.messages.size()) <= keep_recent + 1) return;
    std::optional<llm::Message> sys;
    std::size_t start = 0;
    if (!conv.messages.empty() && conv.messages[0].role == llm::Role::System) { sys = conv.messages[0]; start = 1; }
    if (static_cast<int>(conv.messages.size() - start) <= keep_recent) return;
    std::vector<llm::Message> recent(conv.messages.end() - keep_recent, conv.messages.end());
    conv.messages.clear();
    if (sys) conv.messages.push_back(std::move(*sys));
    conv.messages.push_back(llm::Message::system("[Earlier conversation context was compacted. Recent messages below.]"));
    conv.messages.insert(conv.messages.end(), recent.begin(), recent.end());
}

export void compact_conversation(llm::Conversation& conv, ContextManager& ctx_mgr, int keep_recent = 3) {
    ctx_mgr.compact(conv, keep_recent);
}

} // namespace xlings::agent
