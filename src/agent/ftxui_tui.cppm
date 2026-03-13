module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

export module xlings.agent.ftxui_tui;

import std;
import xlings.agent.tui;
import xlings.agent.token_tracker;
import xlings.core.utf8;
import xlings.ui;
import xlings.libs.tinytui;

namespace xlings::agent {

namespace tui_icons {
    constexpr auto pending = "\xe2\x97\x8b";   // ○
    constexpr auto running = "\xe2\x9f\xb3";   // ⟳
    constexpr auto done    = "\xe2\x9c\x93";   // ✓
    constexpr auto failed  = "\xe2\x9c\x97";   // ✗
    constexpr auto turn    = "\xe2\x8f\xb5";   // ⏵
    constexpr auto reply   = "\xe2\x97\x86";   // ◆
}

// ─── Render helpers ───

using namespace ftxui;

auto node_icon(int state) -> std::string {
    switch (state) {
        case tui::TreeNode::Pending: return tui_icons::pending;
        case tui::TreeNode::Running: return tui_icons::running;
        case tui::TreeNode::Done:    return tui_icons::done;
        case tui::TreeNode::Failed:  return tui_icons::failed;
        default: return tui_icons::pending;
    }
}

auto node_color(int state) -> Color {
    switch (state) {
        case tui::TreeNode::Pending: return ui::theme::dim_color();
        case tui::TreeNode::Running: return ui::theme::amber();
        case tui::TreeNode::Done:    return ui::theme::green();
        case tui::TreeNode::Failed:  return ui::theme::red();
        default: return ui::theme::dim_color();
    }
}

auto time_text(const tui::TreeNode& node) -> std::string {
    if (node.state == tui::TreeNode::Pending) return "";
    if (node.state == tui::TreeNode::Running && node.start_ms > 0) {
        return "  " + tui::format_duration(tui::steady_now_ms() - node.start_ms);
    }
    if (node.end_ms > 0 && node.start_ms > 0) {
        return "  " + tui::format_duration(node.end_ms - node.start_ms);
    }
    return "";
}

auto render_tree_node(const tui::TreeNode& node,
                      const std::string& prefix,
                      bool is_last) -> Element {
    // Current node line
    std::string connector = is_last ? "\xe2\x94\x94\xe2\x94\x80 "   // └─
                                    : "\xe2\x94\x9c\xe2\x94\x80 ";  // ├─

    auto icon_el = text(node_icon(node.state)) | color(node_color(node.state));
    auto title_el = text(" " + node.title) | color(
        node.state == tui::TreeNode::Pending ? ui::theme::dim_color() : ui::theme::text_color());
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

auto render_turn(const tui::TurnNode& tn, const tui::AgentTuiState& state) -> Element {
    Elements rows;

    // User message header: ⏵ message
    rows.push_back(
        text(std::string(tui_icons::turn) + " " + tn.user_message)
            | bold | color(ui::theme::amber()));

    // Tree nodes (children of root)
    for (std::size_t i = 0; i < tn.root.children.size(); ++i) {
        bool is_last = (i == tn.root.children.size() - 1);
        rows.push_back(render_tree_node(tn.root.children[i], "", is_last));
    }

    // Streaming text (only for active turn)
    if (&tn == state.active_turn && !state.streaming_text.empty()) {
        rows.push_back(text(""));
        rows.push_back(
            text(std::string(tui_icons::reply) + " " + state.streaming_text)
                | color(ui::theme::cyan()));
    }

    // Final reply
    if (!tn.reply.empty() && &tn != state.active_turn) {
        rows.push_back(text(""));
        rows.push_back(hbox({
            text(std::string(tui_icons::reply) + " ") | bold | color(ui::theme::cyan()),
            text(tn.reply),
        }));
    }

    // Separator
    rows.push_back(separator() | color(ui::theme::border_color()));

    return vbox(std::move(rows));
}

auto render_all_turns(const tui::AgentTuiState& state) -> Element {
    Elements rows;
    for (auto& tn : state.turns) {
        rows.push_back(render_turn(tn, state));
    }

    // If active turn is streaming/thinking, show spinner line
    if (state.active_turn && !state.current_action.empty()) {
        std::string spinner_text = std::string(tui_icons::running) + " " + state.current_action;
        if (state.turn_start_ms > 0) {
            spinner_text += "  " + tui::format_duration(tui::steady_now_ms() - state.turn_start_ms);
        }
        rows.push_back(text(spinner_text) | color(ui::theme::amber()));
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

    return hbox(std::move(parts));
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

// ─── Input box with cursor and two separator lines ───

auto render_input_box(const tinytui::LineEditor& editor) -> Element {
    auto& content = editor.content();
    auto cursor = editor.cursor_pos();

    // Split content at cursor position
    std::string before = content.substr(0, cursor);
    std::string after = cursor < content.size() ? content.substr(cursor) : "";

    // The character under the cursor (or space if at end)
    std::string cursor_char = " ";
    if (!after.empty()) {
        // Extract first UTF-8 character
        std::size_t char_len = 1;
        auto c = static_cast<unsigned char>(after[0]);
        if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        if (char_len <= after.size()) {
            cursor_char = after.substr(0, char_len);
            after = after.substr(char_len);
        }
    }

    auto sep = separator() | color(ui::theme::border_color());

    return vbox({
        sep,
        hbox({
            text("> ") | color(ui::theme::cyan()),
            text(before),
            text(cursor_char) | inverted | color(ui::theme::cyan()),
            text(after),
        }),
        sep,
    });
}

// ─── AgentScreen ───

export class AgentScreen {
public:
    AgentScreen(tui::AgentTuiState& state,
                tinytui::LineEditor& editor,
                std::function<bool(const ftxui::Event&)> key_handler)
        : state_(state), editor_(editor), key_handler_(std::move(key_handler))
    {
        screen_.TrackMouse(true);
    }

    void loop() {
        auto main_renderer = Renderer([this] {
            // Build main layout
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
                // Input box: two separator lines + cursor
                layout.push_back(render_input_box(editor_));
            }

            // Status bar
            layout.push_back(render_status_bar(state_));

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

            // Delegate to external key handler
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
    tinytui::LineEditor& editor_;
    std::function<bool(const ftxui::Event&)> key_handler_;

    ScreenInteractive screen_ = ScreenInteractive::FullscreenPrimaryScreen();
    float scroll_y_ {1.0f};
    bool at_bottom_ {true};
};

} // namespace xlings::agent
