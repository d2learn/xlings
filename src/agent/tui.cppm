module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.agent.tui;

import std;
import xlings.agent.token_tracker;

namespace xlings::agent::tui {

// ─── Theme colors ───

export namespace colors {
    auto cyan()    -> ftxui::Color { return ftxui::Color::RGB(34, 211, 238); }
    auto green()   -> ftxui::Color { return ftxui::Color::RGB(34, 197, 94); }
    auto amber()   -> ftxui::Color { return ftxui::Color::RGB(245, 158, 11); }
    auto red()     -> ftxui::Color { return ftxui::Color::RGB(239, 68, 68); }
    auto magenta() -> ftxui::Color { return ftxui::Color::RGB(168, 85, 247); }
    auto dim()     -> ftxui::Color { return ftxui::Color::RGB(148, 163, 184); }
    auto txt()     -> ftxui::Color { return ftxui::Color::RGB(248, 250, 252); }
    auto border()  -> ftxui::Color { return ftxui::Color::RGB(51, 65, 85); }
    auto blue()    -> ftxui::Color { return ftxui::Color::RGB(96, 165, 250); }
} // namespace colors

// ─── Time helpers ───

export auto steady_now_ms() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

export auto format_duration(int64_t ms) -> std::string {
    if (ms < 0) return "0ms";
    if (ms < 1000) return std::to_string(ms) + "ms";
    if (ms < 60000) return std::format("{:.1f}s", ms / 1000.0);
    return std::format("{:.0f}m{:.0f}s", ms / 60000.0, (ms % 60000) / 1000.0);
}

// ─── Sub-step: request / decision / execution detail ───

export struct SubStep {
    std::string label;      // "request", "decision", "execution"
    int64_t start_ms {0};   // steady_clock ms
    int64_t end_ms   {0};   // 0 = still running
    bool done        {false};
    bool failed      {false};
};

// ─── Chat line ───

export struct ChatLine {
    enum Type { UserMsg, AssistantText, ToolAction, Progress, Separator, Hint, Error };
    enum Status { Running, Done, Failed };

    Type type;
    std::string text;
    int action_id      {-1};
    Status status      {Running};
    std::vector<std::string> details;    // data event details
    std::vector<SubStep> sub_steps;      // request/decision/execution
    int64_t start_ms   {0};              // for real-time elapsed
    int input_tokens   {0};
    int output_tokens  {0};
    int elapsed_ms     {0};              // final elapsed (when done)
    float progress     {0.0f};
    std::string speed;
    std::string eta;
};

// ─── Agent TUI shared state ───

export struct AgentTuiState {
    // Output area
    std::vector<ChatLine> lines;
    std::string streaming_text;
    bool is_streaming   {false};
    bool is_thinking    {false};
    bool auto_scroll    {true};

    // Status bar
    std::string model_name;
    int ctx_used        {0};
    int ctx_limit       {0};
    int session_input   {0};
    int session_output  {0};
    int l2_cache_count  {0};

    // Current activity
    std::string current_action;
    int64_t turn_start_ms {0};

    // Active progress (download etc.)
    float active_progress {0.0f};
    std::string active_progress_name;
    std::string active_progress_speed;
    std::string active_progress_eta;

    // Active details (data events during tool execution)
    std::vector<std::string> active_details;

    // Active tool action tracking
    std::string active_action_text;
    int64_t active_action_start {0};
    std::vector<SubStep> active_sub_steps;  // sub-steps being built for current action

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

// ─── Render sub-step ───

export auto render_sub_step(const SubStep& ss, int64_t now_ms) -> ftxui::Element {
    using namespace ftxui;

    std::string icon;
    ftxui::Color icon_color = colors::dim();
    int64_t elapsed = 0;

    if (ss.done) {
        icon = ss.failed ? "\xe2\x9c\x97" : "\xe2\x9c\x93";  // ✗ or ✓
        icon_color = ss.failed ? colors::red() : colors::green();
        elapsed = ss.end_ms - ss.start_ms;
    } else {
        icon = "\xe2\x80\xa6";  // …
        icon_color = colors::amber();
        elapsed = now_ms - ss.start_ms;
    }

    return hbox({
        text("      " + icon + " ") | color(icon_color),
        text(ss.label) | color(colors::dim()),
        text(" " + format_duration(elapsed)) | color(colors::border()),
    });
}

// ─── Render a single ChatLine ───

export auto render_chat_line(const ChatLine& line, int64_t now_ms = 0) -> ftxui::Element {
    using namespace ftxui;

    switch (line.type) {
        case ChatLine::UserMsg:
            return vbox({
                separator() | color(colors::amber()),
                hbox({
                    text("> ") | bold | color(colors::amber()),
                    paragraph(line.text) | color(colors::amber()),
                }),
                separator() | color(colors::amber()),
            });

        case ChatLine::AssistantText:
            return hbox({
                text("\xe2\x97\x86 ") | color(colors::magenta()),  // ◆
                paragraph(line.text),
            });

        case ChatLine::ToolAction: {
            Elements elems;
            std::string icon;
            ftxui::Color status_color = colors::amber();
            switch (line.status) {
                case ChatLine::Running:
                    icon = "\xe2\x9a\xa1 ";  // ⚡
                    status_color = colors::amber();
                    break;
                case ChatLine::Done:
                    icon = "\xe2\x9c\x93 ";  // ✓
                    status_color = colors::green();
                    break;
                case ChatLine::Failed:
                    icon = "\xe2\x9c\x97 ";  // ✗
                    status_color = colors::red();
                    break;
            }
            std::string prefix = "  " + icon;
            if (line.action_id >= 0) {
                prefix += "[" + std::to_string(line.action_id) + "] ";
            }
            elems.push_back(text(prefix + line.text) | color(status_color));

            // Real-time or final timing
            int64_t elapsed = line.elapsed_ms;
            if (line.status == ChatLine::Running && line.start_ms > 0 && now_ms > 0) {
                elapsed = now_ms - line.start_ms;
            }
            if (elapsed > 0) {
                elems.push_back(text(" " + format_duration(elapsed))
                    | color(colors::dim()));
            }
            if (line.input_tokens > 0 || line.output_tokens > 0) {
                elems.push_back(
                    text("  \xe2\x86\x91" + TokenTracker::format_tokens(line.input_tokens) +
                         " \xe2\x86\x93" + TokenTracker::format_tokens(line.output_tokens))
                    | color(colors::dim()));
            }

            // Build vbox: action line + sub-steps + detail lines
            Elements rows;
            rows.push_back(hbox(std::move(elems)));
            for (auto& ss : line.sub_steps) {
                rows.push_back(render_sub_step(ss, now_ms));
            }
            for (auto& detail : line.details) {
                rows.push_back(text("      " + detail) | color(colors::border()));
            }
            return vbox(std::move(rows));
        }

        case ChatLine::Progress: {
            int pct = static_cast<int>(line.progress * 100.0f);
            Elements elems;
            elems.push_back(text("  \xe2\x96\xb8 ") | color(colors::cyan()));  // ▸
            elems.push_back(gauge(line.progress)
                | size(WIDTH, EQUAL, 24) | color(colors::cyan()));
            elems.push_back(text(" " + std::to_string(pct) + "%") | bold);
            if (!line.text.empty()) {
                elems.push_back(text("  " + line.text) | color(colors::magenta()));
            }
            if (!line.speed.empty()) {
                elems.push_back(text("  " + line.speed) | color(colors::cyan()));
            }
            if (!line.eta.empty()) {
                elems.push_back(text("  " + line.eta) | color(colors::dim()));
            }
            return hbox(std::move(elems));
        }

        case ChatLine::Separator:
            return separator() | color(colors::border());

        case ChatLine::Hint:
            return text(line.text) | color(colors::dim());

        case ChatLine::Error:
            return text("\xe2\x9c\x97 " + line.text) | color(colors::red());  // ✗
    }
    return text("");
}

// ─── Render status bar (each item a different color, spaces between) ───

export auto render_status_bar(const AgentTuiState& st, int64_t now_ms = 0) -> ftxui::Element {
    using namespace ftxui;
    auto fmt = [](int t) { return TokenTracker::format_tokens(t); };
    auto bar = [](){ return text(" | ") | color(colors::border()); };

    Elements elems;
    elems.push_back(text(" " + st.model_name) | color(colors::cyan()));
    elems.push_back(bar());
    elems.push_back(text("ctx " + fmt(st.ctx_used) + " / " + fmt(st.ctx_limit))
        | color(colors::blue()));
    elems.push_back(bar());
    elems.push_back(text("\xe2\x86\x91 " + fmt(st.session_input)) | color(colors::green()));
    elems.push_back(bar());
    elems.push_back(text("\xe2\x86\x93 " + fmt(st.session_output)) | color(colors::magenta()));
    if (st.l2_cache_count > 0) {
        elems.push_back(bar());
        elems.push_back(text("cache " + std::to_string(st.l2_cache_count) + "t")
            | color(colors::dim()));
    }
    if (!st.current_action.empty()) {
        elems.push_back(bar());
        elems.push_back(text(st.current_action) | color(colors::amber()));
        if (st.turn_start_ms > 0 && now_ms > 0) {
            auto elapsed = now_ms - st.turn_start_ms;
            elems.push_back(text(" " + format_duration(elapsed)) | color(colors::dim()));
        }
    }
    return hbox(std::move(elems));
}

// ─── Render completion popup ───

export auto render_completions(const AgentTuiState& st) -> ftxui::Element {
    using namespace ftxui;
    if (st.completions.empty()) return text("");

    Elements elems;
    int max_show = std::min(static_cast<int>(st.completions.size()), 8);
    for (int i = 0; i < max_show; ++i) {
        auto& name = st.completions[i].first;
        auto& desc = st.completions[i].second;
        if (i == st.completion_selected) {
            elems.push_back(hbox({
                text("  \xe2\x80\xba " + name) | bold | color(colors::cyan()),
                text("  " + desc) | color(colors::dim()),
            }));
        } else {
            elems.push_back(hbox({
                text("    " + name) | color(colors::dim()),
                text("  " + desc) | color(colors::border()),
            }));
        }
    }
    return vbox(std::move(elems));
}

// ─── Render approval prompt ───

export auto render_approval(const AgentTuiState& st) -> ftxui::Element {
    using namespace ftxui;
    if (!st.approval_pending) return text("");

    std::string args_display = st.approval_args;
    if (args_display.size() > 50) {
        args_display = args_display.substr(0, 47) + "...";
    }
    return hbox({
        text("  \xe2\x9a\xa0 approve ") | color(colors::amber()),
        text(st.approval_tool_name) | bold | color(colors::amber()),
        text(" (" + args_display + ")? ") | color(colors::dim()),
        text("[Y/n] ") | bold | color(colors::amber()),
    });
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

// ─── ANSI format helpers (for screen.Print() output) ───

export auto ansi_separator() -> std::string {
    return "\033[38;2;51;65;85m" + std::string(72, '-') + "\033[0m\n";
}

export auto ansi_user_msg(std::string_view text) -> std::string {
    return "\033[1;38;2;34;211;238m> \033[0;38;2;248;250;252m" + std::string(text) + "\033[0m\n";
}

export auto ansi_assistant_text(std::string_view text) -> std::string {
    return "\033[38;2;168;85;247m\xe2\x97\x86 \033[0m" + std::string(text) + "\n";
}

export auto ansi_tool_action(const ChatLine& line) -> std::string {
    std::string icon, color_code;
    switch (line.status) {
        case ChatLine::Running:
            icon = "\xe2\x9a\xa1";  // ⚡
            color_code = "38;2;245;158;11";
            break;
        case ChatLine::Done:
            icon = "\xe2\x9c\x93";  // ✓
            color_code = "38;2;34;197;94";
            break;
        case ChatLine::Failed:
            icon = "\xe2\x9c\x97";  // ✗
            color_code = "38;2;239;68;68";
            break;
    }
    std::string result = "  \033[" + color_code + "m" + icon + " ";
    if (line.action_id >= 0) {
        result += "[" + std::to_string(line.action_id) + "] ";
    }
    result += line.text;
    if (line.elapsed_ms > 0) {
        result += " \033[38;2;148;163;184m" + format_duration(line.elapsed_ms);
    }
    result += "\033[0m\n";
    // Sub-steps
    for (auto& ss : line.sub_steps) {
        std::string ss_icon = ss.done ? (ss.failed ? "\xe2\x9c\x97" : "\xe2\x9c\x93") : "\xe2\x80\xa6";
        std::string ss_color = ss.done ? (ss.failed ? "38;2;239;68;68" : "38;2;34;197;94") : "38;2;245;158;11";
        int64_t ss_elapsed = ss.done ? (ss.end_ms - ss.start_ms) : 0;
        result += "      \033[" + ss_color + "m" + ss_icon + "\033[38;2;148;163;184m " +
            ss.label + " " + format_duration(ss_elapsed) + "\033[0m\n";
    }
    // Details
    for (auto& d : line.details) {
        result += "      \033[38;2;51;65;85m" + d + "\033[0m\n";
    }
    return result;
}

export auto ansi_hint(std::string_view text) -> std::string {
    return "\033[38;2;148;163;184m" + std::string(text) + "\033[0m\n";
}

export auto ansi_error(std::string_view text) -> std::string {
    return "\033[38;2;239;68;68m\xe2\x9c\x97 " + std::string(text) + "\033[0m\n";
}

// ─── Simple ANSI helpers for pre-REPL messages ───

export void print_error(std::string_view msg) {
    std::print("\033[38;2;239;68;68m\xe2\x9c\x97 {}\033[0m\n", msg);
}

export void print_hint(std::string_view msg) {
    std::print("\033[38;2;148;163;184m{}\033[0m\n", msg);
}

} // namespace xlings::agent::tui
