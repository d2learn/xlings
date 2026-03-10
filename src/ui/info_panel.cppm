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

// Print uninstall summary
void print_remove_summary(const std::string& name) {
    using namespace ftxui;

    Elements rows;
    rows.push_back(text(""));
    rows.push_back(hbox({
        text("  " + std::string(theme::icon::done) + " ") | color(theme::green()),
        text(name + " removed") | color(theme::green()) | bold,
    }));
    rows.push_back(text(""));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::println("");
}

} // namespace xlings::ui
