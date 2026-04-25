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

// --- Icons ---
//
// All glyphs are BMP-region characters that ship in the default monospace
// fonts of every mainstream terminal we care about: Cascadia Code/Mono
// (Windows Terminal default), Consolas (conhost), Lucida Console (legacy
// conhost), DejaVu Sans Mono / Liberation Mono / Noto Mono (Linux), and
// SF Mono / Menlo (macOS). No platform-conditional ASCII fallback —
// keeping a single glyph set is what makes Linux / macOS / Windows look
// the same, which is the whole point.
//
// Avoid: U+27D0 ⟐, U+2699 ⚙, U+27F0 ⟰ — these are spotty in older
// console fonts and were the main offenders behind Windows mojibake.
namespace icon {
    inline constexpr auto pending     = "\xe2\x97\x8b";   // ○ U+25CB
    inline constexpr auto downloading = "\xe2\x86\x93";   // ↓ U+2193
    inline constexpr auto extracting  = "\xe2\x96\xbe";   // ▾ U+25BE
    inline constexpr auto installing  = "\xe2\x8a\x95";   // ⊕ U+2295
    inline constexpr auto configuring = "\xe2\x8a\x95";   // ⊕ U+2295
    inline constexpr auto done        = "\xe2\x9c\x93";   // ✓ U+2713
    inline constexpr auto failed      = "\xe2\x9c\x97";   // ✗ U+2717
    inline constexpr auto info        = "\xe2\x80\xba";   // › U+203A
    inline constexpr auto arrow       = "\xe2\x96\xb8";   // ▸ U+25B8
    inline constexpr auto package     = "\xe2\x97\x86";   // ◆ U+25C6
} // namespace icon

} // namespace xlings::ui::theme
