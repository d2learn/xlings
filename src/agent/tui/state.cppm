export module xlings.agent.tui.state;

import std;
import xlings.agent.behavior_tree;
import xlings.agent.token_tracker;
import xlings.libs.tinytui;

namespace xlings::agent::tui {

// Re-export from behavior_tree for backward compatibility
export using agent::steady_now_ms;
export using agent::format_duration;
export using agent::BehaviorNode;
export using agent::ABehaviorTree;
export using agent::IdAllocator;

// ─── Turn node (one user→agent round) ───

export struct TurnNode {
    std::string user_message;
    BehaviorNode root;                // behavior tree root for this turn
    std::string reply;                // final assistant reply
    std::int64_t start_ms {0};
};

// ─── ChatLine (for session resume history) ───

export struct ChatLine {
    static constexpr int UserMsg       = 0;
    static constexpr int AssistantText = 1;
    static constexpr int Hint          = 2;

    int type;
    std::string text;
};

// ─── Agent TUI shared state ───

export struct AgentTuiState {
    // Streaming state (streaming_text in ABehaviorTree)
    bool is_streaming   {false};
    bool is_thinking    {false};

    // Status bar
    std::string model_name;
    int ctx_used        {0};
    int ctx_limit       {0};
    int session_input   {0};
    int session_output  {0};
    int l2_cache_count  {0};

    // Current activity
    std::string current_action;
    std::int64_t turn_start_ms {0};

    // Conversation turns (recursive tree per turn)
    std::vector<TurnNode> turns;
    TurnNode* active_turn {nullptr};
    ABehaviorTree behavior_tree;
    IdAllocator id_alloc;

    // Slash command completion
    std::vector<std::pair<std::string, std::string>> completions;
    int completion_selected {-1};

    // Input history
    std::deque<std::string> history;
    int history_pos     {-1};
    std::string saved_input;

    // Approval prompt
    bool approval_pending {false};
    std::string approval_tool_name;
    std::string approval_args;
    int approval_node_id {0};  // tree node awaiting confirmation
};

// ─── Print ChatLine to stdout (for session resume) ───

export auto print_chat_line(const ChatLine& line) -> int {
    namespace tt = tinytui;

    switch (line.type) {
        case ChatLine::UserMsg: {
            tt::print_separator(tt::ansi::amber);
            tt::print(tt::ansi::amber, "> ");
            tt::println(tt::ansi::amber, line.text);
            return 2;
        }

        case ChatLine::AssistantText: {
            tt::print(tt::ansi::bold, "");
            tt::print(tt::ansi::cyan, "\xe2\x97\x87 ");  // ◇
            int term_w = tt::terminal_width();
            int avail = term_w - 2;
            int line_count = 0;
            std::istringstream ss(line.text);
            std::string paragraph;
            bool first = true;
            while (std::getline(ss, paragraph)) {
                if (!first) {
                    tt::print_raw("  ");
                }
                first = false;
                if (paragraph.empty()) {
                    tt::print_raw("\n");
                    ++line_count;
                    continue;
                }
                std::size_t pos = 0;
                while (pos < paragraph.size()) {
                    auto chunk = paragraph.substr(pos, avail);
                    tt::println_raw(chunk);
                    ++line_count;
                    pos += avail;
                    if (pos < paragraph.size()) {
                        tt::print_raw("  ");
                    }
                }
            }
            tt::print_raw("\n");
            return line_count + 1;
        }

        case ChatLine::Hint:
            tt::println(tt::ansi::dim, line.text);
            return 1;
    }
    return 0;
}

// ─── ThinkFilter: strips <think>...</think> from LLM streaming output ───

export struct ThinkFilter {
    bool in_think {false};
    std::string buffer;

    auto filter(std::string_view chunk) -> std::string {
        using namespace std::literals;
        std::string output;

        for (std::size_t i = 0; i < chunk.size(); ++i) {
            char c = chunk[i];

            if (!buffer.empty()) {
                buffer += c;

                if (!in_think && buffer.size() <= 7) {
                    if ("<think>"sv.starts_with(buffer)) {
                        if (buffer == "<think>") {
                            in_think = true;
                            buffer.clear();
                        }
                        continue;
                    }
                    output += buffer;
                    buffer.clear();
                    continue;
                }

                if (in_think && buffer.size() <= 8) {
                    if ("</think>"sv.starts_with(buffer)) {
                        if (buffer == "</think>") {
                            in_think = false;
                            buffer.clear();
                            while (i + 1 < chunk.size() && chunk[i + 1] == '\n') ++i;
                        }
                        continue;
                    }
                    buffer.clear();
                    continue;
                }

                if (!in_think) {
                    output += buffer;
                }
                buffer.clear();
                continue;
            }

            if (c == '<') {
                buffer += c;
                continue;
            }
            if (!in_think) {
                output += c;
            }
        }
        return output;
    }

    auto flush() -> std::string {
        std::string output;
        if (!buffer.empty() && !in_think) {
            output = buffer;
        }
        buffer.clear();
        return output;
    }
};

// ─── Simple ANSI helpers for pre-REPL messages ───

export void print_error(std::string_view msg) {
    std::print("\033[38;2;239;68;68m\xe2\x9c\x97 {}\033[0m\n", msg);
}

export void print_hint(std::string_view msg) {
    std::print("\033[38;2;148;163;184m{}\033[0m\n", msg);
}

} // namespace xlings::agent::tui
