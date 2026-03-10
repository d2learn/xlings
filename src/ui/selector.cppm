module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.ui:selector;

import std;
import :theme;

export namespace xlings::ui {

// Interactive version selector -- returns chosen version or nullopt if cancelled
std::optional<std::string>
select_version(std::string_view pkgName, std::span<const std::string> versions) {
    using namespace ftxui;

    if (versions.empty()) return std::nullopt;
    if (versions.size() == 1) return std::string { versions[0] };

    int selected { 0 };
    bool confirmed { false };
    bool cancelled { false };

    std::vector<std::string> labels;
    labels.reserve(versions.size());
    for (auto& v : versions) labels.push_back(v);

    auto menu = Menu(&labels, &selected);

    auto screen = ScreenInteractive::TerminalOutput();

    auto component = CatchEvent(menu, [&](Event event) {
        if (event == Event::Return) {
            confirmed = true;
            screen.Exit();
            return true;
        }
        if (event == Event::Escape || event == Event::Character('q')) {
            cancelled = true;
            screen.Exit();
            return true;
        }
        return false;
    });

    auto renderer = Renderer(component, [&] {
        return vbox({
            text(std::format(" Select version for {}:", pkgName)) | theme::title(),
            separator() | color(theme::border_color()),
            component->Render() | vscroll_indicator | frame
                | size(HEIGHT, LESS_THAN, 15),
            separator() | color(theme::border_color()),
            text(" \u2191\u2193 navigate  Enter select  Esc cancel") | theme::hint(),
        }) | borderRounded | color(theme::border_color());
    });

    screen.Loop(renderer);

    if (confirmed && selected >= 0 && selected < (int)versions.size()) {
        return versions[selected];
    }
    return std::nullopt;
}

// Interactive package selector from name-description pairs
std::optional<std::string>
select_package(std::span<const std::pair<std::string, std::string>> items) {
    using namespace ftxui;

    if (items.empty()) return std::nullopt;
    if (items.size() == 1) return std::string { items[0].first };

    int selected { 0 };
    bool confirmed { false };

    std::vector<std::string> labels;
    labels.reserve(items.size());
    for (auto& [name, desc] : items) {
        // Avoid {:<20s} -- GCC 15 modules crash on width specifiers
        std::string padded = name;
        while (padded.size() < 20) padded += ' ';
        labels.push_back(padded + " " + desc);
    }

    auto menu = Menu(&labels, &selected);
    auto screen = ScreenInteractive::TerminalOutput();

    auto component = CatchEvent(menu, [&](Event event) {
        if (event == Event::Return) {
            confirmed = true;
            screen.Exit();
            return true;
        }
        if (event == Event::Escape || event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(Renderer(component, [&] {
        return vbox({
            text(" Select a package:") | theme::title(),
            separator() | color(theme::border_color()),
            component->Render() | vscroll_indicator | frame
                | size(HEIGHT, LESS_THAN, 20),
            separator() | color(theme::border_color()),
            text(" \u2191\u2193 navigate  Enter select  Esc cancel") | theme::hint(),
        }) | borderRounded | color(theme::border_color());
    }));

    if (confirmed && selected >= 0 && selected < (int)items.size()) {
        return std::string { items[selected].first };
    }
    return std::nullopt;
}

// Generic option selector with title + description pairs (fullscreen, centered)
// Returns selected index or nullopt if cancelled
// default_idx: pre-selected item index (-1 for first)
std::optional<int>
select_option(std::string_view title,
              std::span<const std::pair<std::string, std::string>> items,
              std::string_view done_label = "",
              int default_idx = 0) {
    using namespace ftxui;

    if (items.empty()) return std::nullopt;

    int selected = (default_idx >= 0 && default_idx < static_cast<int>(items.size()))
        ? default_idx : 0;
    bool confirmed { false };

    // Build labels: "name   description"
    std::vector<std::string> labels;
    labels.reserve(items.size() + (done_label.empty() ? 0 : 1));
    for (auto& [name, desc] : items) {
        std::string padded = name;
        while (padded.size() < 24) padded += ' ';
        labels.push_back(padded + desc);
    }
    if (!done_label.empty()) {
        labels.push_back(std::string(done_label));
    }

    MenuOption menu_opt;
    menu_opt.entries_option.transform = [](const EntryState& state) {
        auto e = text((state.focused ? "> " : "  ") + state.label);
        if (state.focused) {
            e = e | bold | inverted;
        } else {
            e = e | color(theme::text_color());
        }
        return e;
    };
    auto menu = Menu(&labels, &selected, menu_opt);
    auto screen = ScreenInteractive::Fullscreen();

    auto component = CatchEvent(menu, [&](Event event) {
        if (event == Event::Return) {
            confirmed = true;
            screen.Exit();
            return true;
        }
        if (event == Event::Escape || event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(Renderer(component, [&] {
        auto box = vbox({
            text(" " + std::string(title)) | theme::title(),
            separator() | color(theme::border_color()),
            component->Render() | vscroll_indicator | frame
                | size(HEIGHT, LESS_THAN, 15),
            separator() | color(theme::border_color()),
            text(" \u2191\u2193 navigate  Enter select  Esc cancel") | theme::hint(),
        }) | borderRounded | color(theme::border_color())
           | size(WIDTH, LESS_THAN, 72);

        return box | center;
    }));

    if (!confirmed) return std::nullopt;

    // "done" item selected
    if (!done_label.empty() && selected == static_cast<int>(items.size())) {
        return -1; // sentinel for "done"
    }

    if (selected >= 0 && selected < static_cast<int>(items.size())) {
        return selected;
    }
    return std::nullopt;
}

// Read a line from stdin with ANSI-colored prompt
std::string read_line(std::string_view prompt) {
    std::print("\033[38;2;34;211;238m  {} \033[0m", prompt);
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    while (!line.empty() && line.front() == ' ')
        line.erase(line.begin());
    return line;
}

// Simple yes/no confirmation with styled prompt
bool confirm(std::string_view message, bool defaultYes) {
    std::string prompt = defaultYes ? "[Y/n] " : "[y/N] ";
    // Use ANSI colors for inline prompt (not ftxui rendered)
    std::print("\033[38;2;34;211;238m{}\033[0m{}\033[38;2;148;163;184m{}\033[0m",
               std::string(theme::icon::arrow) + " ", message, prompt);
    std::cout.flush();

    std::string input;
    if (!std::getline(std::cin, input)) return defaultYes;
    if (input.empty()) return defaultYes;

    return input[0] == 'y' || input[0] == 'Y';
}

} // namespace xlings::ui
