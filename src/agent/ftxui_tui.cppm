module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

export module xlings.agent.ftxui_tui;

import std;
import xlings.agent.behavior_tree;
import xlings.agent.tui;
import xlings.agent.token_tracker;
import xlings.core.utf8;
import xlings.ui;
import xlings.libs.json;
namespace xlings::agent {

namespace tui_icons {
    constexpr auto pending    = "\xe2\x97\x8b";   // ○
    constexpr auto running    = "\xe2\x9f\xb3";   // ⟳
    constexpr auto done       = "\xe2\x9c\x93";   // ✓
    constexpr auto failed     = "\xe2\x9c\x97";   // ✗
    constexpr auto skipped    = "\xe2\x96\xb7";   // ▷
    constexpr auto turn       = "\xe2\x8f\xb5";   // ⏵
    constexpr auto reply      = "\xe2\x97\x86";   // ◆
    constexpr auto direct_exec = "\xe2\x9a\x99";  // ⚙
}

// ─── Render helpers ───

using namespace ftxui;

auto node_icon(const BehaviorNode& node) -> std::string {
    // Response nodes use done icon
    if (node.type == BehaviorNode::TypeResponse && node.state == BehaviorNode::Done) {
        return tui_icons::done;
    }
    // DirectExec nodes use gear icon
    if (node.type == BehaviorNode::TypeDirectExec) {
        switch (node.state) {
            case BehaviorNode::Running: return tui_icons::direct_exec;
            case BehaviorNode::Done:    return tui_icons::done;
            case BehaviorNode::Failed:  return tui_icons::failed;
            default: return tui_icons::direct_exec;
        }
    }
    switch (node.state) {
        case BehaviorNode::Pending: return tui_icons::pending;
        case BehaviorNode::Running: return tui_icons::running;
        case BehaviorNode::Done:    return tui_icons::done;
        case BehaviorNode::Failed:  return tui_icons::failed;
        case BehaviorNode::Skipped: return tui_icons::skipped;
        default: return tui_icons::pending;
    }
}

auto node_color(const BehaviorNode& node) -> Color {
    // Response nodes are always cyan
    if (node.type == BehaviorNode::TypeResponse) {
        return ui::theme::cyan();
    }
    // DirectExec uses amber/green/red
    if (node.type == BehaviorNode::TypeDirectExec) {
        switch (node.state) {
            case BehaviorNode::Running: return ui::theme::amber();
            case BehaviorNode::Done:    return ui::theme::green();
            case BehaviorNode::Failed:  return ui::theme::red();
            default: return ui::theme::dim_color();
        }
    }
    switch (node.state) {
        case BehaviorNode::Pending: return ui::theme::dim_color();
        case BehaviorNode::Running: return ui::theme::amber();
        case BehaviorNode::Done:    return ui::theme::green();
        case BehaviorNode::Failed:  return ui::theme::red();
        case BehaviorNode::Skipped: return ui::theme::dim_color();
        default: return ui::theme::dim_color();
    }
}

auto title_color_for(const BehaviorNode& node) -> Color {
    if (node.type == BehaviorNode::TypeResponse) return ui::theme::cyan();
    if (node.state == BehaviorNode::Pending) return ui::theme::dim_color();
    return ui::theme::text_color();
}

auto time_text(const BehaviorNode& node) -> std::string {
    if (node.state == BehaviorNode::Pending) return "";
    if (node.state == BehaviorNode::Running && node.start_ms > 0) {
        return "  " + format_duration(steady_now_ms() - node.start_ms);
    }
    if (node.end_ms > 0 && node.start_ms > 0) {
        return "  " + format_duration(node.end_ms - node.start_ms);
    }
    return "";
}

// Format tool args compactly: {"query":"nodejs"} → "nodejs"
auto compact_tool_args(const std::string& args_json) -> std::string {
    auto json = nlohmann::json::parse(args_json, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return args_json;
    std::string result;
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!result.empty()) result += ", ";
        auto& v = it.value();
        if (v.is_string()) {
            auto s = v.get<std::string>();
            if (s.size() > 30) s = s.substr(0, 30) + "...";
            result += "\"" + s + "\"";
        } else {
            auto s = v.dump();
            if (s.size() > 30) s = s.substr(0, 30) + "...";
            result += s;
        }
    }
    return result;
}

auto render_tree_node(const BehaviorNode& node,
                      const std::string& prefix,
                      bool is_last) -> Element {
    // Current node line
    std::string connector = is_last ? "\xe2\x94\x94\xe2\x94\x80 "   // └─
                                    : "\xe2\x94\x9c\xe2\x94\x80 ";  // ├─

    auto icon_el = text(node_icon(node)) | color(node_color(node));

    // DirectExec nodes show tool(args) instead of task title
    std::string title_str;
    if (node.type == BehaviorNode::TypeDirectExec && !node.tool.empty()) {
        title_str = " " + node.tool + "(" + compact_tool_args(node.tool_args) + ")";
    } else {
        title_str = " " + node.name;
    }
    auto title_el = text(title_str) | color(title_color_for(node));
    auto time_el = text(time_text(node)) | color(ui::theme::dim_color());

    auto line = hbox({
        text(prefix + connector),
        icon_el,
        title_el,
        time_el,
    });

    // Collect children
    Elements rows;
    rows.push_back(line);

    std::string child_prefix = prefix + (is_last
        ? "   "                                                      // 3 spaces
        : "\xe2\x94\x82  ");                                        // │  (│ + 2 spaces)

    for (std::size_t i = 0; i < node.children.size(); ++i) {
        bool child_is_last = (i == node.children.size() - 1);
        rows.push_back(render_tree_node(node.children[i], child_prefix, child_is_last));
    }

    return vbox(std::move(rows));
}

auto render_turn(const tui::TurnNode& tn, const tui::AgentTuiState& state,
                 const BehaviorNode* tree_snap, const std::string& streaming_snap) -> Element {
    Elements rows;
    // Use snapshot for active turn, tn.root for completed turns
    auto& tree_root = tree_snap ? *tree_snap : tn.root;

    // User message header: ⏵ message  total_time
    {
        auto header = text(std::string(tui_icons::turn) + " " + tn.user_message)
            | bold | color(ui::theme::amber());

        // Total elapsed time for this turn
        std::string total_time;
        if (&tn == state.active_turn && tn.start_ms > 0) {
            // Active turn: real-time elapsed
            total_time = "  " + format_duration(steady_now_ms() - tn.start_ms);
        } else if (tn.start_ms > 0 && !tree_root.children.empty()) {
            // Completed turn: find max end_ms among all descendants
            std::int64_t max_end = 0;
            std::function<void(const BehaviorNode&)> find_max = [&](const BehaviorNode& n) {
                if (n.end_ms > max_end) max_end = n.end_ms;
                for (auto& c : n.children) find_max(c);
            };
            find_max(tree_root);
            if (max_end > tn.start_ms) {
                total_time = "  " + format_duration(max_end - tn.start_ms);
            }
        }

        if (total_time.empty()) {
            rows.push_back(header);
        } else {
            rows.push_back(hbox({
                header,
                text(total_time) | color(ui::theme::dim_color()),
            }));
        }
    }

    // Tree nodes (children of root)
    for (std::size_t i = 0; i < tree_root.children.size(); ++i) {
        bool is_last = (i == tree_root.children.size() - 1);
        rows.push_back(render_tree_node(tree_root.children[i], "", is_last));
    }

    // Separator between tree and reply (amber)
    bool has_reply = (&tn == state.active_turn && !streaming_snap.empty())
                  || (!tn.reply.empty() && &tn != state.active_turn);
    if (has_reply) {
        rows.push_back(separator() | color(ui::theme::amber()));
    }

    // Streaming text (only for active turn)
    if (&tn == state.active_turn && !streaming_snap.empty()) {
        rows.push_back(
            text(std::string(tui_icons::reply) + " " + streaming_snap)
                | color(ui::theme::cyan()));
    }

    // Final reply (multiline support)
    if (!tn.reply.empty() && &tn != state.active_turn) {
        std::istringstream ss(tn.reply);
        std::string line;
        bool first = true;
        while (std::getline(ss, line)) {
            if (first) {
                rows.push_back(hbox({
                    text(std::string(tui_icons::reply) + " ") | bold | color(ui::theme::cyan()),
                    text(line),
                }));
                first = false;
            } else {
                rows.push_back(text("  " + line));
            }
        }
    }

    // Trailing empty line between turns
    rows.push_back(text(""));

    return vbox(std::move(rows));
}

auto render_all_turns(const tui::AgentTuiState& state) -> Element {
    // Take snapshot of active turn's tree and streaming text (thread-safe)
    auto tree_snap = state.behavior_tree.snapshot();
    auto streaming_snap = state.behavior_tree.streaming_text();

    Elements rows;
    for (auto& tn : state.turns) {
        bool is_active = (&tn == state.active_turn);
        rows.push_back(render_turn(tn, state,
            is_active ? &tree_snap : nullptr,
            is_active ? streaming_snap : std::string{}));
    }

    if (rows.empty()) {
        rows.push_back(text(""));
    }
    return vbox(std::move(rows));
}

auto render_status_bar(const tui::AgentTuiState& st) -> Element {
    auto fmt = [](int t) { return TokenTracker::format_tokens(t); };

    Elements parts;
    parts.push_back(text(" " + st.model_name) | color(ui::theme::cyan()));
    parts.push_back(text(" | ") | color(ui::theme::border_color()));
    parts.push_back(text("ctx " + fmt(st.ctx_used) + " / " + fmt(st.ctx_limit))
        | color(Color::RGB(96, 165, 250)));  // blue
    parts.push_back(text(" | ") | color(ui::theme::border_color()));
    parts.push_back(text("\xe2\x86\x91 " + fmt(st.session_input)) | color(ui::theme::green()));
    parts.push_back(text(" | ") | color(ui::theme::border_color()));
    parts.push_back(text("\xe2\x86\x93 " + fmt(st.session_output)) | color(ui::theme::magenta()));
    if (st.l2_cache_count > 0) {
        parts.push_back(text(" | ") | color(ui::theme::border_color()));
        parts.push_back(text("cache " + std::to_string(st.l2_cache_count) + "t")
            | color(ui::theme::dim_color()));
    }

    auto left = hbox(std::move(parts));

    // Right side: activity indicator (thinking/responding + timer)
    if (!st.current_action.empty()) {
        std::string activity = std::string(tui_icons::running) + " " + st.current_action;
        if (st.turn_start_ms > 0) {
            activity += " " + format_duration(steady_now_ms() - st.turn_start_ms);
        }
        return hbox({left, filler(), text(activity + " ") | color(ui::theme::amber())});
    }

    return left;
}

auto render_completion_menu(const tui::AgentTuiState& st) -> Element {
    if (st.completions.empty()) return text("");

    Elements items;
    for (int i = 0; i < static_cast<int>(st.completions.size()); ++i) {
        auto& [name, desc] = st.completions[i];
        auto line = text("  " + name + "  " + desc);
        if (i == st.completion_selected) {
            line = line | inverted;
        } else {
            line = line | color(ui::theme::dim_color());
        }
        items.push_back(line);
    }
    return vbox(std::move(items));
}

auto render_approval(const tui::AgentTuiState& st) -> Element {
    if (!st.approval_pending) return text("");
    return hbox({
        text("\xe2\x9a\xa0 approve ") | color(ui::theme::amber()),
        text(st.approval_tool_name) | bold | color(ui::theme::amber()),
        text(" (" + utf8::safe_truncate(st.approval_args, 50) + ")? ") | color(ui::theme::dim_color()),
        text("[Y/n]") | bold | color(ui::theme::amber()),
    });
}

// ─── AgentScreen (uses ftxui::Input for real system cursor + IME support) ───

export class AgentScreen {
public:
    AgentScreen(tui::AgentTuiState& state,
                std::string& input_content,
                std::function<void()> on_input_change,
                std::function<bool(const ftxui::Event&)> key_handler)
        : state_(state), input_content_(input_content),
          on_input_change_(std::move(on_input_change)),
          key_handler_(std::move(key_handler))
    {
        screen_.TrackMouse(true);

        // Create ftxui Input component — provides real terminal cursor for IME
        InputOption opt;
        opt.multiline = false;
        opt.on_change = [this] { if (on_input_change_) on_input_change_(); };
        // Strip default background decoration — just return the raw element
        opt.transform = [](InputState state) { return state.element; };
        input_comp_ = Input(&input_content_, opt);
    }

    void loop() {
        // Renderer with input_comp_ as child — Input gets focus and drives cursor
        auto main_renderer = Renderer(input_comp_, [this] {
            Elements layout;

            // History area (flex, scrollable)
            auto history = render_all_turns(state_);
            if (at_bottom_) scroll_y_ = 1.0f;
            layout.push_back(
                history | focusPositionRelative(0, scroll_y_) | yframe | flex);

            // Completion menu (above input)
            if (!state_.completions.empty()) {
                layout.push_back(render_completion_menu(state_));
            }

            // Approval prompt (replaces input when pending)
            if (state_.approval_pending) {
                layout.push_back(render_approval(state_));
            } else {
                // Input box: top separator + "> " prefix + ftxui Input (real cursor) + bottom separator
                layout.push_back(separator() | color(ui::theme::border_color()));
                layout.push_back(hbox({
                    text("> ") | color(ui::theme::cyan()),
                    input_comp_->Render() | flex,
                }));
                layout.push_back(separator() | color(ui::theme::border_color()));
            }

            // Status bar + trailing empty line
            layout.push_back(render_status_bar(state_));
            layout.push_back(text(""));

            return vbox(std::move(layout));
        });

        auto main_component = CatchEvent(main_renderer, [this](Event event) {
            // Mouse wheel scrolling
            if (event.is_mouse()) {
                auto& m = event.mouse();
                if (m.button == Mouse::WheelUp) {
                    scroll_y_ = std::max(0.0f, scroll_y_ - 0.05f);
                    at_bottom_ = false;
                    return true;
                }
                if (m.button == Mouse::WheelDown) {
                    scroll_y_ = std::min(1.0f, scroll_y_ + 0.05f);
                    if (scroll_y_ >= 0.99f) at_bottom_ = true;
                    return true;
                }
            }

            // Delegate to external key handler (Enter, Up/Down, Tab, Esc, Ctrl+C)
            // Unhandled events fall through to input_comp_ (chars, backspace, etc.)
            if (key_handler_ && key_handler_(event)) {
                return true;
            }

            return false;
        });

        // 200ms timer for time display refresh
        std::jthread timer([this](std::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (!st.stop_requested()) {
                    screen_.PostEvent(Event::Custom);
                }
            }
        });

        // Focus input component so cursor appears at "> " prompt
        screen_.Post([this] { input_comp_->TakeFocus(); });

        screen_.Loop(main_component);
        timer.request_stop();
    }

    void exit() {
        screen_.Exit();
    }

    void post(std::function<void()> fn) {
        screen_.Post(std::move(fn));
    }

    void refresh() {
        screen_.PostEvent(Event::Custom);
    }

    // Scroll to bottom (call when new content arrives)
    void scroll_to_bottom() {
        at_bottom_ = true;
        scroll_y_ = 1.0f;
    }

private:
    tui::AgentTuiState& state_;
    std::string& input_content_;
    std::function<void()> on_input_change_;
    std::function<bool(const ftxui::Event&)> key_handler_;

    Component input_comp_;
    ScreenInteractive screen_ = ScreenInteractive::FullscreenPrimaryScreen();
    float scroll_y_ {1.0f};
    bool at_bottom_ {true};
};

} // namespace xlings::agent
