export module xlings.agent.runtime;

import std;
import mcpplibs.llmapi;
import xlings.agent.prompt;
import xlings.agent.loop;
import xlings.agent.llm;
import xlings.agent.tool_bridge;
import xlings.agent.approval;
import xlings.agent.session;
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.agent.behavior_tree;
import xlings.agent.tui.state;
import xlings.agent.tui.screen;
import xlings.agent.commands;
import xlings.libs.soul;
import xlings.runtime.event_stream;
import xlings.runtime.capability;
import xlings.runtime.cancellation;
import xlings.libs.semantic_memory;
import xlings.libs.agentfs;
import xlings.capabilities;

namespace xlings::agent {

namespace llm = mcpplibs::llmapi;

// ═══════════════════════════════════════════════════════════════
//  AgentRuntime — owns subsystems + interactive session loop
// ═══════════════════════════════════════════════════════════════

export class AgentRuntime {
    std::string system_prompt_;
    std::vector<llm::ToolDef> tools_;

    // Message queue (UI thread → worker thread)
    std::mutex msg_mtx_;
    std::condition_variable msg_cv_;
    std::deque<std::string> msg_queue_;

public:
    // ── Owned subsystems ──
    TokenTracker tracker;
    ContextManager ctx_mgr;
    libs::semantic_memory::MemoryStore memory_store;
    capability::Registry registry;
    ToolBridge bridge;

    // ── External references (set via init) ──
    LlmConfig cfg;
    EventStream* stream {nullptr};
    llm::Conversation* conversation {nullptr};
    ApprovalPolicy* policy {nullptr};
    CancellationToken* cancel {nullptr};

    // ── TUI integration ──
    AgentScreen* screen {nullptr};
    tui::AgentTuiState* tui_state {nullptr};
    CommandRegistry* commands {nullptr};
    ConfirmCallback confirm_cb;

    // ── Optional log callback ──
    std::function<void(std::string_view role, std::string_view content)> on_log;

    // ── Constructor: create subsystems ──
    AgentRuntime(libs::agentfs::AgentFS& afs, const LlmConfig& config, const SessionMeta& session)
        : ctx_mgr(config.model)
        , memory_store(afs)
        , registry(capabilities::build_registry(&memory_store, &ctx_mgr))
        , bridge(registry)
        , cfg(config)
    {
        memory_store.load();
        auto cache_dir = afs.sessions_path() / session.id / "context_cache";
        ctx_mgr.set_cache_dir(cache_dir);
    }

    AgentRuntime(const AgentRuntime&) = delete;
    AgentRuntime& operator=(const AgentRuntime&) = delete;

    // ── Init: wire external references + build prompt ──
    void init(const libs::soul::Soul& soul,
              llm::Conversation& conv, EventStream& ev_stream) {
        stream = &ev_stream;
        conversation = &conv;
        system_prompt_ = build_system_prompt(bridge, soul);
        tools_ = to_llmapi_tools(bridge);
    }

    void set_prompt(std::string prompt) { system_prompt_ = std::move(prompt); }
    void set_tools(std::vector<llm::ToolDef> tools) { tools_ = std::move(tools); }

    // ── Thread communication ──
    void send_input(std::string input) {
        { std::lock_guard lk(msg_mtx_); msg_queue_.push_back(std::move(input)); }
        msg_cv_.notify_one();
    }
    void notify() { msg_cv_.notify_all(); }

    // ── Session loop: wait for input → dispatch → repeat ──
    void run_session(std::stop_token st) {
        while (!st.stop_requested()) {
            std::string user_input;
            {
                std::unique_lock lk(msg_mtx_);
                msg_cv_.wait(lk, [&] { return !msg_queue_.empty() || st.stop_requested(); });
                if (st.stop_requested() && msg_queue_.empty()) break;
                if (msg_queue_.empty()) continue;
                user_input = std::move(msg_queue_.front());
                msg_queue_.pop_front();
            }

            if (user_input == "exit" || user_input == "quit") {
                screen->post([this] { screen->exit(); });
                break;
            }

            if (!user_input.empty() && user_input[0] == '/' && commands && commands->execute(user_input))
                continue;

            if (on_log) on_log("user", user_input);

            // Prepare TUI
            auto now_ms = steady_now_ms();
            tui_state->behavior_tree.reset();
            screen->post([this, input = user_input, now_ms] {
                tui::TurnNode tn;
                tn.user_message = input;
                tn.start_ms = now_ms;
                tui_state->turns.push_back(std::move(tn));
                tui_state->active_turn = &tui_state->turns.back();
                tui_state->is_streaming = true;
                tui_state->is_thinking = true;
                tui_state->current_action = "thinking...";
                tui_state->turn_start_ms = now_ms;
                if (tui_state->history.empty() || tui_state->history.back() != input)
                    tui_state->history.push_back(input);
                screen->scroll_to_bottom();
            });
            screen->refresh();

            tui_state->id_alloc.reset();
            cancel->reset();

            LoopResult turn_result;
            try {
                turn_result = process_turn_(user_input);
            } catch (const PausedException&) {
                cancel->reset();
                tui_state->behavior_tree.clear_streaming();
                screen->post([this] {
                    tui_state->is_streaming = false; tui_state->is_thinking = false;
                    tui_state->current_action = "paused"; tui_state->active_turn = nullptr;
                });
                screen->refresh();
                continue;
            } catch (const CancelledException&) {
                cancel->reset();
                tui_state->behavior_tree.clear_streaming();
                screen->post([this] {
                    tui_state->is_streaming = false; tui_state->is_thinking = false;
                    tui_state->current_action.clear(); tui_state->turn_start_ms = 0;
                    if (tui_state->active_turn) tui_state->active_turn->reply = "[cancelled]";
                    tui_state->active_turn = nullptr;
                });
                screen->refresh();
                if (conversation && !conversation->messages.empty()) conversation->messages.pop_back();
                continue;
            } catch (const std::exception& e) {
                bool was_cancelled = cancel->is_cancelled();
                cancel->reset();
                tui_state->behavior_tree.clear_streaming();
                screen->post([this, err = std::string(e.what()), was_cancelled] {
                    tui_state->is_streaming = false; tui_state->is_thinking = false;
                    tui_state->current_action.clear(); tui_state->turn_start_ms = 0;
                    if (tui_state->active_turn)
                        tui_state->active_turn->reply = was_cancelled ? "[cancelled]" : "[error: " + err + "]";
                    tui_state->active_turn = nullptr;
                });
                screen->refresh();
                if (was_cancelled && conversation && !conversation->messages.empty())
                    conversation->messages.pop_back();
                continue;
            }

            tui_state->behavior_tree.finalize(steady_now_ms());
            screen->post([this, reply = turn_result.reply] {
                tui_state->is_streaming = false; tui_state->is_thinking = false;
                if (tui_state->active_turn) {
                    if (!reply.empty()) tui_state->active_turn->reply = reply;
                    else {
                        auto s = tui_state->behavior_tree.get_streaming_as_reply();
                        if (!s.empty()) tui_state->active_turn->reply = s;
                    }
                    tui_state->active_turn->root = tui_state->behavior_tree.snapshot();
                }
                tui_state->behavior_tree.clear_streaming();
                ctx_mgr.record_turn();
                tui_state->ctx_used = tracker.context_used();
                tui_state->session_input = tracker.session_input();
                tui_state->session_output = tracker.session_output();
                tui_state->l2_cache_count = ctx_mgr.l2_count();
                tui_state->current_action.clear();
                tui_state->turn_start_ms = 0;
                tui_state->active_turn = nullptr;
                screen->scroll_to_bottom();
            });
            screen->refresh();

            if (on_log) on_log("assistant", turn_result.reply);
        }
    }

private:
    auto process_turn_(std::string_view user_input) -> LoopResult {
        AgentLoopConfig ac{
            .user_input = std::string(user_input),
            .system_prompt = system_prompt_,
            .tools = tools_,
            .bridge = bridge,
            .stream = *stream,
            .cfg = cfg,
            .conversation = *conversation,
            .policy = policy,
            .confirm_cb = confirm_cb,
            .cancel = cancel,
            .tree = &tui_state->behavior_tree,
            .id_alloc = &tui_state->id_alloc,
            .tracker = &tracker,
            .ctx_mgr = &ctx_mgr,
            .on_stream_chunk = [this](std::string_view chunk) {
                cancel->throw_if_cancelled();
                if (!chunk.empty()) tui_state->behavior_tree.append_streaming(std::string(chunk));
                screen->post([this] {
                    tui_state->is_streaming = true; tui_state->is_thinking = false;
                    tui_state->current_action = "thinking...";
                });
                screen->refresh();
            },
            .on_tool_call = [this](int id, std::string_view name, std::string_view args) {
                (void)args;
                auto n = std::string(name);
                screen->post([this, n, id] {
                    tui_state->is_streaming = false; tui_state->is_thinking = false;
                    tui_state->current_action = "executing " + n + "...";
                    tui_state->approval_node_id = id;
                    screen->scroll_to_bottom();
                });
                screen->refresh();
            },
            .on_tool_result = [this]([[maybe_unused]] int id, [[maybe_unused]] std::string_view name, [[maybe_unused]] bool is_error) {
                screen->post([this] {
                    tui_state->download_progress.clear();
                    tui_state->current_action = "thinking...";
                    tui_state->is_streaming = true; tui_state->is_thinking = true;
                });
                screen->refresh();
            },
            .on_token_update = [this]([[maybe_unused]] int turn_in, [[maybe_unused]] int turn_out) {
                screen->post([this] {
                    tui_state->session_input = tracker.session_input();
                    tui_state->session_output = tracker.session_output();
                    tui_state->ctx_used = tracker.context_used();
                });
                screen->refresh();
            },
        };
        return agent_loop(ac);
    }
};

} // namespace xlings::agent
