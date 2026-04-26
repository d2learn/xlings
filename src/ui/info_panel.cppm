module;

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.ui:info_panel;

import std;
import :theme;

export namespace xlings::ui {

// A key-value row for info panels
struct InfoField {
    std::string label;
    std::string value;
    bool is_highlight { false }; // Use green+bold for value
};

// Render info fields into rows with right-padded labels
void render_fields_(ftxui::Elements& rows, std::span<const InfoField> fields) {
    using namespace ftxui;
    for (auto& f : fields) {
        std::string padded = f.label;
        while (padded.size() < 14) padded += ' ';

        auto val = f.is_highlight
            ? (text(f.value) | color(theme::green()) | bold)
            : (text(f.value) | color(theme::text_color()));

        rows.push_back(hbox({
            text("  "),
            text(padded) | color(theme::dim_color()),
            text("  "),
            val,
        }));
    }
}

// Print a styled info panel with a title and key-value fields,
// plus an optional second section separated by a divider.
void print_info_panel(std::string_view title,
                      std::span<const InfoField> fields,
                      std::span<const InfoField> extra = {}) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(hbox({
        text("  " + std::string(theme::icon::package) + " ") | color(theme::magenta()),
        text(std::string(title)) | theme::highlight(),
    }));
    rows.push_back(
        text("  ────────────────────────────────────────") | color(theme::border_color())
    );

    // Calculate minimum width needed to avoid truncation
    int minWidth = 40; // separator width
    auto measure = [&](std::span<const InfoField> fs) {
        for (auto& f : fs) {
            int w = 2 + 14 + 2 + static_cast<int>(f.value.size()) + 2;
            if (w > minWidth) minWidth = w;
        }
    };
    measure(fields);
    if (!extra.empty()) measure(extra);

    render_fields_(rows, fields);

    if (!extra.empty()) {
        rows.push_back(
            text("  ────────────────────────────────────────") | color(theme::border_color())
        );
        render_fields_(rows, extra);
    }

    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto termDim = Dimension::Full();
    int width = std::max(termDim.dimx, minWidth);
    auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

// Print a styled list of items with a title
void print_styled_list(std::string_view title,
                       std::span<const std::pair<std::string, std::string>> items,
                       bool show_marker) {
    using namespace ftxui;

    Elements rows;
    if (!title.empty()) {
        rows.push_back(text("  " + std::string(title)) | bold | color(theme::text_color()));
        rows.push_back(text(""));
    }

    for (auto& [name, desc] : items) {
        auto marker = show_marker
            ? (text("  " + std::string(theme::icon::package) + " ") | color(theme::magenta()))
            : text("    ");

        auto nameEl = text(name) | bold | color(theme::magenta());

        Element row;
        if (desc.empty()) {
            row = hbox({ marker, nameEl });
        } else {
            row = hbox({
                marker,
                nameEl,
                text("  ") ,
                text(desc) | color(theme::dim_color()),
            });
        }
        rows.push_back(row);
    }
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

// Print install plan display.
// Saves cursor position before package lines so download progress can replace them.
void print_install_plan(std::span<const std::pair<std::string, std::string>> packages) {
    using namespace ftxui;

    // Print header
    Elements header;
    header.push_back(hbox({
        text("  Packages to install (") | color(theme::text_color()),
        text(std::to_string(packages.size())) | bold | color(theme::text_color()),
        text("):") | color(theme::text_color()),
    }));
    header.push_back(text(""));

    auto headerDoc = vbox(std::move(header));
    auto headerScreen = Screen::Create(Dimension::Full(), Dimension::Fit(headerDoc));
    Render(headerScreen, headerDoc);
    headerScreen.Print();
    std::print("\n");

    // Print package lines (will be replaced by download progress via cursor-up)
    Elements pkgRows;
    for (auto& [nameVer, desc] : packages) {
        pkgRows.push_back(hbox({
            text("    " + std::string(theme::icon::package) + " ") | color(theme::magenta()),
            text(nameVer) | bold | color(theme::magenta()),
            desc.empty() ? text("") : (text("  " + desc) | color(theme::dim_color())),
        }));
    }
    pkgRows.push_back(text(""));

    auto pkgDoc = vbox(std::move(pkgRows));
    auto pkgScreen = Screen::Create(Dimension::Full(), Dimension::Fit(pkgDoc));
    Render(pkgScreen, pkgDoc);
    pkgScreen.Print();
}

// Print subos list
void print_subos_list(
    std::span<const std::tuple<std::string, std::string, int, bool>> entries) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(text("  Sub-OS environments:") | bold | color(theme::text_color()));
    rows.push_back(text(""));

    for (auto& [name, dir, tools, active] : entries) {
        auto marker = active
            ? (text("  " + std::string(theme::icon::arrow) + " ") | color(theme::cyan()))
            : text("    ");
        auto nameEl = active
            ? (text(name) | bold | color(theme::cyan()))
            : (text(name) | color(theme::text_color()));

        rows.push_back(hbox({
            marker,
            nameEl,
            text("  (" + dir + "  tools: " + std::to_string(tools) + ")")
                | color(theme::dim_color()),
        }));
    }
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

// Subos status messages are single-line with a possibly-long path. ftxui
// hbox layout truncates them to the detected terminal width (80 cols when
// TERM is unset), so use plain std::print + raw ANSI here instead — the
// content has to stay readable in CI / pipes / non-TTY contexts.
namespace subos_ansi_ {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto cyan   = "\033[38;2;34;211;238m";
    constexpr auto green  = "\033[38;2;34;197;94m";
    constexpr auto gray   = "\033[38;2;148;163;184m";
}

void print_subos_created(const std::string& name, const std::string& dir) {
    using namespace subos_ansi_;
    std::println("{}  ✓ subos created: {}{}{}{}", green, bold, name, reset, reset);
    if (!dir.empty()) {
        std::println("{}    dir:{} {}", gray, reset, dir);
    }
}

void print_subos_switched(const std::string& name, const std::string& dir) {
    using namespace subos_ansi_;
    if (dir.empty()) {
        std::println("{}  ▸ switched to subos {}{}{}", cyan, bold, name, reset);
    } else {
        std::println("{}  ▸ switched to subos {}{}{}{}  ({}){}",
                     cyan, bold, name, reset, cyan, dir, reset);
    }
}

void print_subos_removed(const std::string& name) {
    using namespace subos_ansi_;
    std::println("{}  ✓ subos removed: {}{}{}", green, bold, name, reset);
}

// Print install summary with success/fail counts
void print_install_summary(int success, int failed) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(text(""));

    if (success > 0) {
        rows.push_back(hbox({
            text("  " + std::string(theme::icon::done) + " ") | color(theme::green()),
            text(std::to_string(success) + " package(s) installed") | color(theme::green()) | bold,
        }));
    }
    if (failed > 0) {
        rows.push_back(hbox({
            text("  " + std::string(theme::icon::failed) + " ") | color(theme::red()),
            text(std::to_string(failed) + " package(s) failed") | color(theme::red()) | bold,
        }));
    }
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

// Format "<name>[@<version>]" for display. Centralized so the plan and
// summary lines stay aligned when version is absent.
inline std::string remove_target_label_(const std::string& name,
                                        const std::string& version) {
    if (version.empty()) return name;
    return name + "@" + version;
}

// Print remove plan (shown before the confirmation prompt)
void print_remove_plan(const std::string& subos,
                       const std::string& name,
                       const std::string& version) {
    using namespace ftxui;

    auto label = remove_target_label_(name, version);

    Elements rows;
    rows.push_back(text(""));
    rows.push_back(hbox({
        text("  Package to remove:") | color(theme::text_color()),
    }));
    rows.push_back(text(""));
    rows.push_back(hbox({
        text("    " + std::string(theme::icon::package) + " ") | color(theme::magenta()),
        text(label) | bold | color(theme::magenta()),
        text("  (subos: " + subos + ")") | color(theme::dim_color()),
    }));
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
}

// Print uninstall summary
void print_remove_summary(const std::string& subos,
                          const std::string& name,
                          const std::string& version) {
    using namespace ftxui;

    auto label = remove_target_label_(name, version);

    Elements rows;
    rows.push_back(text(""));
    rows.push_back(hbox({
        text("  " + std::string(theme::icon::done) + " ") | color(theme::green()),
        text(label + " removed") | color(theme::green()) | bold,
        subos.empty()
            ? text("")
            : (text("  (subos: " + subos + ")") | color(theme::dim_color())),
    }));
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

} // namespace xlings::ui
