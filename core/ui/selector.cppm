module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

export module xlings.ui:selector;

import std;

export namespace xlings::ui {

// Interactive version selector — returns chosen version or nullopt if cancelled
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
            text(std::format("Select version for {}:", pkgName)) | bold,
            separator(),
            component->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 15),
            separator(),
            text("Enter=select  q/Esc=cancel") | dim,
        }) | border;
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
        // Avoid {:<20s} — GCC 15 modules crash on width specifiers
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
            text("Select a package:") | bold,
            separator(),
            component->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 20),
        }) | border;
    }));

    if (confirmed && selected >= 0 && selected < (int)items.size()) {
        return std::string { items[selected].first };
    }
    return std::nullopt;
}

// Simple yes/no confirmation using ftxui
bool confirm(std::string_view message, bool defaultYes) {
    // For non-interactive or simple cases, use stdin
    std::string prompt = defaultYes ? "[Y/n] " : "[y/N] ";
    std::print("{}{}", message, prompt);
    std::cout.flush();

    std::string input;
    if (!std::getline(std::cin, input)) return defaultYes;
    if (input.empty()) return defaultYes;

    return input[0] == 'y' || input[0] == 'Y';
}

} // namespace xlings::ui
