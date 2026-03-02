module;

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"

export module xlings.ui:progress;

import std;

export namespace xlings::ui {

enum class Phase { Pending, Downloading, Extracting, Installing, Configuring, Done, Failed };

struct StatusEntry {
    std::string name;
    Phase phase { Phase::Pending };
    float progress { 0.0f };
    std::string message;
};

std::string phase_icon(Phase p) {
    switch (p) {
        case Phase::Pending:      return "  ";
        case Phase::Downloading:  return "..";
        case Phase::Extracting:   return ">>";
        case Phase::Installing:   return ">>";
        case Phase::Configuring:  return "~~";
        case Phase::Done:         return "ok";
        case Phase::Failed:       return "!!";
    }
    return "??";
}

std::string phase_label(Phase p) {
    switch (p) {
        case Phase::Pending:      return "pending";
        case Phase::Downloading:  return "downloading";
        case Phase::Extracting:   return "extracting";
        case Phase::Installing:   return "installing";
        case Phase::Configuring:  return "configuring";
        case Phase::Done:         return "done";
        case Phase::Failed:       return "failed";
    }
    return "unknown";
}

// Render a static snapshot of install progress to stdout
void print_progress(std::span<const StatusEntry> entries) {
    using namespace ftxui;
    Elements rows;
    for (auto& e : entries) {
        auto icon = text(phase_icon(e.phase));
        auto name = text(e.name) | size(WIDTH, EQUAL, 24);
        Element status;
        if (e.phase == Phase::Downloading && e.progress > 0.0f) {
            status = hbox({
                text(phase_label(e.phase) + " "),
                gauge(e.progress) | size(WIDTH, EQUAL, 20),
                text(std::format(" {}%", static_cast<int>(e.progress * 100.0f))),
            });
        } else {
            status = text(phase_label(e.phase));
            if (!e.message.empty()) {
                status = hbox({ status, text(": " + e.message) });
            }
        }
        rows.push_back(hbox({ text("["), icon, text("] "), name, text(" "), status }));
    }
    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

} // namespace xlings::ui
