module;

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.ui:theme;

export namespace xlings::ui::theme {

using ftxui::Color;
using ftxui::Decorator;

// --- Color Palette ---
auto cyan()         -> Color { return Color::RGB(34, 211, 238); }
auto green()        -> Color { return Color::RGB(34, 197, 94); }
auto amber()        -> Color { return Color::RGB(245, 158, 11); }
auto red()          -> Color { return Color::RGB(239, 68, 68); }
auto magenta()      -> Color { return Color::RGB(168, 85, 247); }
auto dim_color()    -> Color { return Color::RGB(148, 163, 184); }
auto text_color()   -> Color { return Color::RGB(248, 250, 252); }
auto surface()      -> Color { return Color::RGB(30, 41, 59); }
auto border_color() -> Color { return Color::RGB(51, 65, 85); }

// --- Decorator Helpers ---
auto title()     -> Decorator { return ftxui::bold | ftxui::color(cyan()); }
auto success()   -> Decorator { return ftxui::bold | ftxui::color(green()); }
auto warning()   -> Decorator { return ftxui::bold | ftxui::color(amber()); }
auto error()     -> Decorator { return ftxui::bold | ftxui::color(red()); }
auto hint()      -> Decorator { return ftxui::dim | ftxui::color(dim_color()); }
auto highlight() -> Decorator { return ftxui::bold | ftxui::color(magenta()); }
auto label()     -> Decorator { return ftxui::color(dim_color()); }
auto body()      -> Decorator { return ftxui::color(text_color()); }

// --- Unicode Icons ---
namespace icon {
    constexpr auto pending     = "\u25CC";  // ◌
    constexpr auto downloading = "\u2193";  // ↓
    constexpr auto extracting  = "\u27D0";  // ⟐
    constexpr auto installing  = "\u2699";  // ⚙
    constexpr auto configuring = "\u2699";  // ⚙
    constexpr auto done        = "\u2713";  // ✓
    constexpr auto failed      = "\u2717";  // ✗
    constexpr auto info        = "\u203A";  // ›
    constexpr auto arrow       = "\u25B8";  // ▸
    constexpr auto package     = "\u25C6";  // ◆
} // namespace icon

} // namespace xlings::ui::theme
