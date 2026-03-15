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

// --- Icons (cross-platform safe) ---
namespace icon {
#if defined(_WIN32)
    inline constexpr auto pending     = "o";
    inline constexpr auto downloading = "v";
    inline constexpr auto extracting  = ">";
    inline constexpr auto installing  = "*";
    inline constexpr auto configuring = "*";
    inline constexpr auto done        = "+";
    inline constexpr auto failed      = "x";
    inline constexpr auto info        = ">";
    inline constexpr auto arrow       = ">";
    inline constexpr auto package     = "#";
#else
    inline constexpr auto pending     = "\xe2\x97\x8b";   // ○
    inline constexpr auto downloading = "\xe2\x86\x93";   // ↓
    inline constexpr auto extracting  = "\xe2\x9f\x90";   // ⟐
    inline constexpr auto installing  = "\xe2\x9a\x99";   // ⚙
    inline constexpr auto configuring = "\xe2\x9a\x99";   // ⚙
    inline constexpr auto done        = "\xe2\x9c\x93";   // ✓
    inline constexpr auto failed      = "\xe2\x9c\x97";   // ✗
    inline constexpr auto info        = "\xe2\x80\xba";   // ›
    inline constexpr auto arrow       = "\xe2\x96\xb8";   // ▸
    inline constexpr auto package     = "\xe2\x97\x86";   // ◆
#endif
} // namespace icon

} // namespace xlings::ui::theme
