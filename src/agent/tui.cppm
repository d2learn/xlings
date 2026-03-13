export module xlings.agent.tui;

import std;
import xlings.agent.token_tracker;
import xlings.core.utf8;
import xlings.libs.tinytui;

namespace xlings::agent::tui {

// ─── Time helpers ───

export auto steady_now_ms() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

export auto format_duration(std::int64_t ms) -> std::string {
    if (ms < 0) return "0ms";
    if (ms < 1000) return std::to_string(ms) + "ms";
    if (ms < 60000) return std::format("{:.1f}s", ms / 1000.0);
    return std::format("{:.0f}m{:.0f}s", ms / 60000.0, (ms % 60000) / 1000.0);
}

// ─── Behavior tree node ───
// State uses int constants (not enum class) to avoid GCC 15 module issues.

export struct TreeNode {
    // Node state
    static constexpr int Pending   = 0;
    static constexpr int Running   = 1;
    static constexpr int Done      = 2;
    static constexpr int Failed    = 3;
    static constexpr int Cancelled = 4;
    static constexpr int Paused    = 5;

    // Node kind
    static constexpr int UserTask   = 0;
    static constexpr int SubTask    = 1;
    static constexpr int Thinking   = 2;
    static constexpr int ToolCall   = 3;
    static constexpr int PlanUpdate = 4;
    static constexpr int Detail     = 5;
    static constexpr int Download   = 6;
    static constexpr int Response   = 7;
    static constexpr int Approval   = 8;

    int kind;
    int state {Pending};
    std::string title;           // display text (1 line)
    std::string details_json;    // hidden JSON details
    int node_id {-1};            // manage_tree assigned ID (SubTask/UserTask)
    int action_id {-1};          // global action counter
    std::int64_t start_ms {0};
    std::int64_t end_ms {0};
    int input_tokens {0};
    int output_tokens {0};

    // Download-specific
    float progress {0.0f};
    std::string speed;
    std::string eta;

    bool expanded {false};       // details expand control

    std::vector<TreeNode> children;

    // Count total lines for rendering (1 per node, recursive)
    auto line_count() const -> int {
        // Completed Thinking/Detail/Response nodes are auto-hidden (0 lines)
        // Response is printed separately as AssistantText in scrollback
        bool hidden = (kind == Thinking || kind == Detail || kind == Response) && state != Running;
        int n = hidden ? 0 : 1;
        for (auto& c : children) n += c.line_count();
        return n;
    }

    // Find node by node_id (recursive)
    auto find_node(int id) -> TreeNode* {
        if (node_id == id) return this;
        for (auto& c : children) {
            if (auto* found = c.find_node(id)) return found;
        }
        return nullptr;
    }

    auto find_node(int id) const -> const TreeNode* {
        if (node_id == id) return this;
        for (auto& c : children) {
            if (auto* found = c.find_node(id)) return found;
        }
        return nullptr;
    }

    // Find parent of a node by node_id (recursive)
    auto find_parent(int id) -> TreeNode* {
        for (auto& c : children) {
            if (c.node_id == id) return this;
            if (auto* found = c.find_parent(id)) return found;
        }
        return nullptr;
    }

    // Mark all Pending descendants as Done (for turn completion)
    void complete_pending() {
        for (auto& c : children) {
            if (c.state == Pending) {
                c.state = Done;
                c.end_ms = steady_now_ms();
            }
            c.complete_pending();
        }
    }

    // Mark all Running descendants as given state (for pause/cancel)
    void mark_running_as(int new_state) {
        if (state == Running) {
            state = new_state;
            end_ms = steady_now_ms();
        }
        for (auto& c : children) c.mark_running_as(new_state);
    }
};

// ─── TaskTree manager ───

export class TaskTree {
    int next_node_id_{1};
    int active_node_id_{-1};  // currently executing task node

public:
    void reset() {
        next_node_id_ = 1;
        active_node_id_ = -1;
    }

    int active_node() const { return active_node_id_; }
    void set_active(int id) { active_node_id_ = id; }

    // Get the parent node for auto-nesting tool calls/thinking/response
    auto active_parent(TreeNode& root) -> TreeNode* {
        if (active_node_id_ > 0) {
            if (auto* node = root.find_node(active_node_id_)) return node;
        }
        return &root;  // fallback: attach to root
    }

    // Create a subtask under parent_id, returns new node_id
    auto add_task(TreeNode& root, int parent_id, const std::string& title,
                  const std::string& details = "") -> int {
        int id = next_node_id_++;
        TreeNode* parent = (parent_id == 0) ? &root : root.find_node(parent_id);
        if (!parent) parent = &root;

        parent->children.push_back(TreeNode{
            .kind = TreeNode::SubTask,
            .state = TreeNode::Pending,
            .title = title,
            .details_json = details,
            .node_id = id,
        });
        return id;
    }

    // Start executing a task
    void start_task(TreeNode& root, int node_id) {
        auto* node = root.find_node(node_id);
        if (!node) return;
        node->state = TreeNode::Running;
        node->start_ms = steady_now_ms();
        active_node_id_ = node_id;
    }

    // Complete a task (done/failed/cancelled)
    void complete_task(TreeNode& root, int node_id, int state,
                       const std::string& result = "") {
        auto* node = root.find_node(node_id);
        if (!node) return;
        node->state = state;
        node->end_ms = steady_now_ms();
        if (!result.empty()) node->details_json = result;
        // Close any still-running children (Response, Thinking, ToolCall, etc.)
        node->mark_running_as(state);

        // Auto-advance: find next Pending sibling or return to parent
        auto* parent = root.find_parent(node_id);
        if (parent) {
            bool found_completed = false;
            for (auto& sibling : parent->children) {
                if (sibling.node_id == node_id) {
                    found_completed = true;
                    continue;
                }
                if (found_completed && sibling.state == TreeNode::Pending &&
                    sibling.kind == TreeNode::SubTask) {
                    // Don't auto-start, just set active for nesting
                    active_node_id_ = parent->node_id >= 0 ? parent->node_id : -1;
                    return;
                }
            }
            // No more pending siblings — return to parent
            active_node_id_ = parent->node_id >= 0 ? parent->node_id : -1;
        } else {
            active_node_id_ = -1;
        }
    }

    // Update an unstarted task's title/details
    void update_task(TreeNode& root, int node_id, const std::string& new_title,
                     const std::string& new_details = "") {
        auto* node = root.find_node(node_id);
        if (!node || node->state != TreeNode::Pending) return;
        if (!new_title.empty()) node->title = new_title;
        if (!new_details.empty()) node->details_json = new_details;
    }
};

// ─── ChatLine ───

export struct ChatLine {
    enum Type { UserMsg, AssistantText, Hint, TurnTree };

    Type type;
    std::string text;
    std::optional<TreeNode> tree;        // for TurnTree type
};

// ─── Agent TUI shared state ───

export struct AgentTuiState {
    // Output area (streaming only — no history accumulation)
    std::string streaming_text;
    bool is_streaming   {false};
    bool is_thinking    {false};

    // Flash message — temporary hint/error with auto-clear
    std::string flash_text;
    std::int64_t flash_until_ms {0};

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

    // Active behavior tree for current turn
    std::optional<TreeNode> active_turn;
    std::int64_t last_action_end_ms {0};  // for computing thinking intervals

    // Task tree manager
    TaskTree task_tree;

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
};

// ─── State icon helpers (returns icon string + ANSI color) ───

auto state_icon(const TreeNode& node) -> std::pair<std::string, const char*> {
    switch (node.state) {
        case TreeNode::Pending:
            return {"\xe2\x97\x8b ", tinytui::ansi::dim};       // ○
        case TreeNode::Running:
            switch (node.kind) {
                case TreeNode::Thinking:
                    return {"\xe2\x80\xa6 ", tinytui::ansi::amber};   // …
                case TreeNode::ToolCall:
                    return {"\xe2\x9a\xa1 ", tinytui::ansi::amber};   // ⚡
                case TreeNode::Download:
                    return {"\xe2\x96\xb8 ", tinytui::ansi::cyan};    // ▸
                case TreeNode::SubTask:
                    return {"\xe2\x80\xa6 ", tinytui::ansi::amber};   // …
                case TreeNode::Approval:
                    return {"\xe2\x9a\xa0 ", tinytui::ansi::amber};   // ⚠
                default:
                    return {"\xe2\x80\xa6 ", tinytui::ansi::amber};   // …
            }
        case TreeNode::Done:
            return {"\xe2\x9c\x93 ", tinytui::ansi::green};     // ✓
        case TreeNode::Failed:
            return {"\xe2\x9c\x97 ", tinytui::ansi::red};       // ✗
        case TreeNode::Cancelled:
            return {"\xe2\x8a\x98 ", tinytui::ansi::dim};       // ⊘
        case TreeNode::Paused:
            return {"\xe2\x8f\xb8 ", tinytui::ansi::cyan};      // ⏸
        default:
            return {"\xe2\x80\xa6 ", tinytui::ansi::dim};
    }
}

// ─── Print tree node to FrameBuffer (returns line count) ───

export auto print_tree_node(const TreeNode& node, std::int64_t now_ms,
                             int term_w, tinytui::FrameBuffer& buf,
                             std::string_view prefix = "",
                             bool is_last = true) -> int {
    namespace tt = tinytui;
    int lines = 0;

    std::string child_prefix;

    if (node.kind == TreeNode::UserTask) {
        // Root node: ⏵ title duration
        std::int64_t elapsed = (node.state == TreeNode::Done || node.state == TreeNode::Failed)
            ? (node.end_ms - node.start_ms)
            : (now_ms - node.start_ms);
        auto dur_str = format_duration(elapsed);

        buf.print(tt::ansi::bold, "");
        buf.print(tt::ansi::amber, "\xe2\x8f\xb5 ");  // ⏵
        buf.print(tt::ansi::amber, node.title);
        buf.print(tt::ansi::dim, " " + dur_str);
        buf.newline();
        ++lines;
        child_prefix = std::string(prefix);
    } else {
        // Auto-hide completed Thinking/Detail/Response nodes
        // Response is printed separately as AssistantText in scrollback
        if ((node.kind == TreeNode::Thinking || node.kind == TreeNode::Detail || node.kind == TreeNode::Response)
            && node.state != TreeNode::Running) {
            // Skip this node, but recurse into children at same depth
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                bool child_is_last = (i == node.children.size() - 1) && is_last;
                lines += print_tree_node(node.children[i], now_ms, term_w, buf, prefix, child_is_last);
            }
            return lines;
        }

        // Non-root nodes use tree connectors
        std::string connector = is_last
            ? "\xe2\x94\x94\xe2\x94\x80 "   // └─
            : "\xe2\x94\x9c\xe2\x94\x80 ";   // ├─

        auto [icon, icon_color] = state_icon(node);

        // Duration
        std::int64_t elapsed = 0;
        if (node.start_ms > 0) {
            bool finished = (node.state == TreeNode::Done || node.state == TreeNode::Failed ||
                             node.state == TreeNode::Cancelled || node.state == TreeNode::Paused);
            elapsed = finished ? (node.end_ms - node.start_ms) : (now_ms - node.start_ms);
        }
        auto dur_str = elapsed > 0 ? format_duration(elapsed) : "";

        // Truncate title if needed
        int prefix_len = static_cast<int>(std::string(prefix).size()) + 5 + 2;
        int dur_len = dur_str.empty() ? 0 : static_cast<int>(dur_str.size()) + 1;
        int avail = std::max(10, term_w - prefix_len - dur_len);
        auto display_text = node.title.size() > static_cast<std::size_t>(avail)
            ? utf8::safe_truncate(node.title, avail)
            : node.title;

        // Response node
        if (node.kind == TreeNode::Response) {
            buf.print(tt::ansi::border, std::string(prefix) + connector);
            if (node.state == TreeNode::Running) {
                buf.print(icon_color, icon);
                buf.print(tt::ansi::dim, "responding...");
                if (!dur_str.empty()) buf.print(tt::ansi::dim, " " + dur_str);
            } else {
                // Done: show first line of reply as summary
                buf.print(tt::ansi::green, "\xe2\x97\x86 ");  // ◆
                auto nl_pos = node.title.find('\n');
                std::string first_line = (nl_pos != std::string::npos)
                    ? node.title.substr(0, nl_pos) : node.title;
                auto summary = first_line.size() > static_cast<std::size_t>(avail)
                    ? utf8::safe_truncate(first_line, avail) : first_line;
                buf.print(tt::ansi::reset, summary);
                if (!dur_str.empty()) buf.print(tt::ansi::dim, " " + dur_str);
            }
            buf.newline();
            ++lines;
            child_prefix = std::string(prefix) + (is_last ? "   " : "\xe2\x94\x82  ");
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                bool child_is_last = (i == node.children.size() - 1);
                lines += print_tree_node(node.children[i], now_ms, term_w, buf, child_prefix, child_is_last);
            }
            return lines;
        }

        // PlanUpdate node
        if (node.kind == TreeNode::PlanUpdate) {
            buf.print(tt::ansi::border, std::string(prefix) + connector);
            buf.print(tt::ansi::cyan, "\xe2\x86\xbb ");   // ↻
            buf.print(tt::ansi::cyan, display_text);
            if (!dur_str.empty()) buf.print(tt::ansi::dim, " " + dur_str);
            buf.newline();
            ++lines;
            child_prefix = std::string(prefix) + (is_last ? "   " : "\xe2\x94\x82  ");
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                bool child_is_last = (i == node.children.size() - 1);
                lines += print_tree_node(node.children[i], now_ms, term_w, buf, child_prefix, child_is_last);
            }
            return lines;
        }

        // Download node with inline progress bar
        if (node.kind == TreeNode::Download && node.state == TreeNode::Running && node.progress > 0.01f) {
            buf.print(tt::ansi::border, std::string(prefix) + connector);
            buf.print(icon_color, icon);
            buf.print(tt::ansi::magenta, display_text);
            buf.print(tt::ansi::cyan, " " + tt::format_progress(node.progress, 16));
            if (!node.speed.empty()) buf.print(tt::ansi::cyan, " " + node.speed);
            if (!dur_str.empty()) buf.print(tt::ansi::dim, " " + dur_str);
            buf.newline();
            ++lines;
        } else {
            // Default rendering
            const char* text_color = tt::ansi::txt;
            if (node.kind == TreeNode::Thinking) text_color = tt::ansi::dim;
            else if (node.kind == TreeNode::Detail) text_color = tt::ansi::border;
            else if (node.kind == TreeNode::Approval) text_color = tt::ansi::amber;
            else if (node.kind == TreeNode::SubTask && node.state == TreeNode::Pending) text_color = tt::ansi::dim;
            else if (node.state == TreeNode::Cancelled) text_color = tt::ansi::dim;

            buf.print(tt::ansi::border, std::string(prefix) + connector);
            buf.print(icon_color, icon);
            buf.print(text_color, display_text);
            if (!dur_str.empty()) buf.print(tt::ansi::dim, " " + dur_str);
            buf.newline();
            ++lines;
        }

        child_prefix = std::string(prefix) + (is_last ? "   " : "\xe2\x94\x82  ");
    }

    // Render children
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        bool child_is_last = (i == node.children.size() - 1);
        lines += print_tree_node(node.children[i], now_ms, term_w, buf, child_prefix, child_is_last);
    }

    return lines;
}

// ─── Print tree node to stdout (for scrollback) ───

export auto print_tree_node_stdout(const TreeNode& node, std::int64_t now_ms,
                                    int term_w) -> int {
    tinytui::FrameBuffer buf;
    int lines = print_tree_node(node, now_ms, term_w, buf);
    // Flush buffer lines to stdout
    for (auto& line : buf.lines()) {
        tinytui::println_raw(line);
    }
    return lines;
}

// ─── Print ChatLine to stdout (returns line count) ───

export auto print_chat_line(const ChatLine& line, std::int64_t now_ms = 0) -> int {
    namespace tt = tinytui;

    switch (line.type) {
        case ChatLine::UserMsg: {
            // Used only for session resume history
            tt::print_separator(tt::ansi::amber);
            tt::print(tt::ansi::amber, "> ");
            tt::println(tt::ansi::amber, line.text);
            return 2;
        }

        case ChatLine::AssistantText: {
            tt::print(tt::ansi::bold, "");
            tt::print(tt::ansi::cyan, "\xe2\x97\x87 ");  // ◇
            // Word-wrap
            int term_w = tt::terminal_width();
            int avail = term_w - 2;
            // Split by newlines first, then wrap
            int line_count = 0;
            std::istringstream ss(line.text);
            std::string paragraph;
            bool first = true;
            while (std::getline(ss, paragraph)) {
                if (!first) {
                    tt::print_raw("  ");  // indent continuation
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

        case ChatLine::TurnTree: {
            if (!line.tree) return 0;
            int term_w = tt::terminal_width();
            // Amber separator before tree
            tt::print_separator(tt::ansi::amber, term_w);
            // Print full tree including root
            return 1 + print_tree_node_stdout(*line.tree, now_ms, term_w);
        }
    }
    return 0;
}

// ─── Print status bar (single line) ───

export auto print_status_bar(const AgentTuiState& st, std::int64_t now_ms,
                              tinytui::FrameBuffer& buf) -> int {
    namespace tt = tinytui;
    auto fmt = [](int t) { return TokenTracker::format_tokens(t); };

    buf.print(tt::ansi::cyan, " " + st.model_name);
    buf.print(tt::ansi::border, " | ");
    buf.print(tt::ansi::blue, "ctx " + fmt(st.ctx_used) + " / " + fmt(st.ctx_limit));
    buf.print(tt::ansi::border, " | ");
    buf.print(tt::ansi::green, "\xe2\x86\x91 " + fmt(st.session_input));
    buf.print(tt::ansi::border, " | ");
    buf.print(tt::ansi::magenta, "\xe2\x86\x93 " + fmt(st.session_output));
    if (st.l2_cache_count > 0) {
        buf.print(tt::ansi::border, " | ");
        buf.print(tt::ansi::dim, "cache " + std::to_string(st.l2_cache_count) + "t");
    }
    if (!st.current_action.empty()) {
        buf.print(tt::ansi::border, " | ");
        buf.print(tt::ansi::amber, st.current_action);
        if (st.turn_start_ms > 0 && now_ms > 0) {
            auto elapsed = now_ms - st.turn_start_ms;
            buf.print(tt::ansi::dim, " " + format_duration(elapsed));
        }
    }
    buf.newline();
    return 1;
}

// ─── Print completions ───

export auto print_completions(const AgentTuiState& st, tinytui::FrameBuffer& buf) -> int {
    namespace tt = tinytui;
    if (st.completions.empty()) return 0;

    int max_show = std::min(static_cast<int>(st.completions.size()), 8);
    for (int i = 0; i < max_show; ++i) {
        auto& name = st.completions[i].first;
        auto& desc = st.completions[i].second;
        if (i == st.completion_selected) {
            buf.print(tt::ansi::bold, "");
            buf.print(tt::ansi::cyan, "  \xe2\x80\xba " + name);
            buf.println(tt::ansi::dim, "  " + desc);
        } else {
            buf.print(tt::ansi::dim, "    " + name);
            buf.println(tt::ansi::border, "  " + desc);
        }
    }
    return max_show;
}

// ─── Print approval prompt ───

export auto print_approval(const AgentTuiState& st, tinytui::FrameBuffer& buf) -> int {
    namespace tt = tinytui;
    if (!st.approval_pending) return 0;

    std::string args_display = utf8::safe_truncate(st.approval_args, 50);
    buf.print(tt::ansi::amber, "  \xe2\x9a\xa0 approve ");
    buf.print(tt::ansi::bold, "");
    buf.print(tt::ansi::amber, st.approval_tool_name);
    buf.print(tt::ansi::dim, " (" + args_display + ")? ");
    buf.print(tt::ansi::bold, "");
    buf.println(tt::ansi::amber, "[Y/n] ");
    return 1;
}

// ─── Render active area to FrameBuffer ───

export void render_active_area(AgentTuiState& st, tinytui::LineEditor& editor,
                                std::int64_t now_ms, int term_w,
                                tinytui::FrameBuffer& buf) {
    namespace tt = tinytui;

    // 1. Active turn tree
    if (st.active_turn) {
        print_tree_node(*st.active_turn, now_ms, term_w, buf);
    }

    // 2. Flash message
    if (!st.flash_text.empty() && now_ms < st.flash_until_ms) {
        buf.println(tt::ansi::dim, st.flash_text);
    } else if (!st.flash_text.empty()) {
        st.flash_text.clear();
    }

    // 3. Separator
    buf.print_separator(tt::ansi::border, term_w);

    // 4. Input / Approval
    if (st.approval_pending) {
        print_approval(st, buf);
    } else {
        buf.print(tt::ansi::cyan, "> ");
        editor.render(buf, term_w, 2);
        buf.newline();
    }

    // 5. Separator
    buf.print_separator(tt::ansi::border, term_w);

    // 6. Completions
    if (!st.completions.empty()) {
        print_completions(st, buf);
    }

    // 7. Status bar
    print_status_bar(st, now_ms, buf);

    // 8. Empty line (bottom padding)
    buf.newline();
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
