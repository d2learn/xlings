module;

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

export module xlings.libs.tinytui;
import std;

export namespace xlings::tinytui {

// ─── ANSI color constants (matching theme) ───

namespace ansi {
    constexpr auto reset   = "\033[0m";
    constexpr auto bold    = "\033[1m";
    constexpr auto cyan    = "\033[38;2;34;211;238m";
    constexpr auto green   = "\033[38;2;34;197;94m";
    constexpr auto amber   = "\033[38;2;245;158;11m";
    constexpr auto red     = "\033[38;2;239;68;68m";
    constexpr auto magenta = "\033[38;2;168;85;247m";
    constexpr auto dim     = "\033[38;2;148;163;184m";
    constexpr auto txt     = "\033[38;2;248;250;252m";
    constexpr auto border  = "\033[38;2;51;65;85m";
    constexpr auto blue    = "\033[38;2;96;165;250m";
} // namespace ansi

// ─── Terminal query ───

inline int terminal_width() {
#if defined(__linux__) || defined(__APPLE__)
    struct winsize w{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return static_cast<int>(w.ws_col);
#elif defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#endif
    return 80;
}

// ─── Output primitives ───

inline void print(const char* color, std::string_view text) {
    std::fwrite(color, 1, std::strlen(color), stdout);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fwrite(ansi::reset, 1, std::strlen(ansi::reset), stdout);
}

inline void println(const char* color, std::string_view text) {
    print(color, text);
    std::fwrite("\n", 1, 1, stdout);
}

inline void println_bold(const char* color, std::string_view text) {
    std::fwrite(ansi::bold, 1, std::strlen(ansi::bold), stdout);
    print(color, text);
    std::fwrite("\n", 1, 1, stdout);
}

inline void print_raw(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
}

inline void println_raw(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fwrite("\n", 1, 1, stdout);
}

inline void print_separator(const char* color, int width = 0) {
    if (width <= 0) width = terminal_width();
    std::string line(width, '\xe2' == '\xe2' ? '-' : '-');
    // Use Unicode thin line ─
    line.clear();
    for (int i = 0; i < width; ++i) line += "\xe2\x94\x80";
    tinytui::println(color, line);
}

inline std::string format_progress(float frac, int width = 16) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int filled = static_cast<int>(frac * width);
    std::string bar;
    for (int i = 0; i < filled; ++i) bar += "\xe2\x96\x88";      // █
    for (int i = filled; i < width; ++i) bar += "\xe2\x96\x91";   // ░
    int pct = static_cast<int>(frac * 100.0f);
    bar += " " + std::to_string(pct) + "%";
    return bar;
}

inline void flush() {
    std::fflush(stdout);
}

// ─── Cursor control ───

inline void cursor_up(int n) {
    if (n > 0) {
        char buf[32];
        auto len = std::snprintf(buf, sizeof(buf), "\033[%dA", n);
        std::fwrite(buf, 1, len, stdout);
    }
}

inline void cursor_down(int n) {
    if (n > 0) {
        char buf[32];
        auto len = std::snprintf(buf, sizeof(buf), "\033[%dB", n);
        std::fwrite(buf, 1, len, stdout);
    }
}

inline void cursor_column(int col) {
    char buf[32];
    auto len = std::snprintf(buf, sizeof(buf), "\033[%dG", col);
    std::fwrite(buf, 1, len, stdout);
}

inline void clear_line() {
    std::fwrite("\033[2K\r", 1, 5, stdout);
}

inline void clear_lines_up(int n) {
    for (int i = 0; i < n; ++i) {
        cursor_up(1);
        clear_line();
    }
}

inline void hide_cursor() {
    std::fwrite("\033[?25l", 1, 6, stdout);
}

inline void show_cursor() {
    std::fwrite("\033[?25h", 1, 6, stdout);
}

// ─── Clear to end of screen ───
inline void clear_to_end() {
    std::fwrite("\033[J", 1, 3, stdout);
}

// ─── KeyEvent ───

struct KeyEvent {
    static constexpr int Char      = 0;
    static constexpr int Up        = 1;
    static constexpr int Down      = 2;
    static constexpr int Left      = 3;
    static constexpr int Right     = 4;
    static constexpr int Tab       = 5;
    static constexpr int Enter     = 6;
    static constexpr int Escape    = 7;
    static constexpr int Backspace = 8;
    static constexpr int Delete    = 9;
    static constexpr int CtrlC     = 10;
    static constexpr int Home      = 11;
    static constexpr int End       = 12;
    static constexpr int Unknown   = 99;

    int type {Unknown};
    std::string ch;    // Char type: UTF-8 character
};

// ─── Read a key from stdin (raw mode) ───

inline std::optional<KeyEvent> read_key(int timeout_ms = -1) {
#if defined(__linux__) || defined(__APPLE__)
    struct pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return std::nullopt;

    unsigned char buf[8];
    auto n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return std::nullopt;

    // Single byte
    if (n == 1) {
        unsigned char c = buf[0];
        if (c == 3) return KeyEvent{KeyEvent::CtrlC};
        if (c == 9) return KeyEvent{KeyEvent::Tab};
        if (c == 10 || c == 13) return KeyEvent{KeyEvent::Enter};
        if (c == 27) {
            // Check for escape sequence
            struct pollfd pfd2{};
            pfd2.fd = STDIN_FILENO;
            pfd2.events = POLLIN;
            if (::poll(&pfd2, 1, 20) > 0) {
                unsigned char seq[8];
                auto sn = ::read(STDIN_FILENO, seq, sizeof(seq));
                if (sn >= 2 && seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A': return KeyEvent{KeyEvent::Up};
                        case 'B': return KeyEvent{KeyEvent::Down};
                        case 'C': return KeyEvent{KeyEvent::Right};
                        case 'D': return KeyEvent{KeyEvent::Left};
                        case 'H': return KeyEvent{KeyEvent::Home};
                        case 'F': return KeyEvent{KeyEvent::End};
                        case '3':
                            if (sn >= 3 && seq[2] == '~') return KeyEvent{KeyEvent::Delete};
                            break;
                        case '1':
                            if (sn >= 3 && seq[2] == '~') return KeyEvent{KeyEvent::Home};
                            break;
                        case '4':
                            if (sn >= 3 && seq[2] == '~') return KeyEvent{KeyEvent::End};
                            break;
                    }
                }
                return KeyEvent{KeyEvent::Unknown};
            }
            return KeyEvent{KeyEvent::Escape};
        }
        if (c == 127 || c == 8) return KeyEvent{KeyEvent::Backspace};
        if (c >= 32 && c < 127) {
            return KeyEvent{KeyEvent::Char, std::string(1, static_cast<char>(c))};
        }
        // Start of UTF-8 multi-byte
        if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 — but we only got 1 byte, read more
            unsigned char rest[3];
            auto rn = ::read(STDIN_FILENO, rest, 1);
            if (rn == 1) {
                std::string s;
                s += static_cast<char>(c);
                s += static_cast<char>(rest[0]);
                return KeyEvent{KeyEvent::Char, s};
            }
        }
        if ((c & 0xF0) == 0xE0) {
            unsigned char rest[3];
            auto rn = ::read(STDIN_FILENO, rest, 2);
            if (rn == 2) {
                std::string s;
                s += static_cast<char>(c);
                s += static_cast<char>(rest[0]);
                s += static_cast<char>(rest[1]);
                return KeyEvent{KeyEvent::Char, s};
            }
        }
        if ((c & 0xF8) == 0xF0) {
            unsigned char rest[3];
            auto rn = ::read(STDIN_FILENO, rest, 3);
            if (rn == 3) {
                std::string s;
                s += static_cast<char>(c);
                s += static_cast<char>(rest[0]);
                s += static_cast<char>(rest[1]);
                s += static_cast<char>(rest[2]);
                return KeyEvent{KeyEvent::Char, s};
            }
        }
        return KeyEvent{KeyEvent::Unknown};
    }

    // Multi-byte read (escape sequence already captured)
    if (n >= 3 && buf[0] == 27 && buf[1] == '[') {
        switch (buf[2]) {
            case 'A': return KeyEvent{KeyEvent::Up};
            case 'B': return KeyEvent{KeyEvent::Down};
            case 'C': return KeyEvent{KeyEvent::Right};
            case 'D': return KeyEvent{KeyEvent::Left};
            case 'H': return KeyEvent{KeyEvent::Home};
            case 'F': return KeyEvent{KeyEvent::End};
            case '3':
                if (n >= 4 && buf[3] == '~') return KeyEvent{KeyEvent::Delete};
                break;
        }
        return KeyEvent{KeyEvent::Unknown};
    }

    // UTF-8 multi-byte from initial read
    {
        std::string s(reinterpret_cast<char*>(buf), n);
        return KeyEvent{KeyEvent::Char, s};
    }
#elif defined(_WIN32)
    // Windows: use ReadConsoleInput
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (timeout_ms >= 0) {
        DWORD result = WaitForSingleObject(hIn, timeout_ms);
        if (result != WAIT_OBJECT_0) return std::nullopt;
    }
    INPUT_RECORD rec;
    DWORD count;
    if (!ReadConsoleInputW(hIn, &rec, 1, &count) || count == 0) return std::nullopt;
    if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) return std::nullopt;

    auto vk = rec.Event.KeyEvent.wVirtualKeyCode;
    auto ch = rec.Event.KeyEvent.uChar.UnicodeChar;

    if (vk == VK_UP) return KeyEvent{KeyEvent::Up};
    if (vk == VK_DOWN) return KeyEvent{KeyEvent::Down};
    if (vk == VK_LEFT) return KeyEvent{KeyEvent::Left};
    if (vk == VK_RIGHT) return KeyEvent{KeyEvent::Right};
    if (vk == VK_HOME) return KeyEvent{KeyEvent::Home};
    if (vk == VK_END) return KeyEvent{KeyEvent::End};
    if (vk == VK_DELETE) return KeyEvent{KeyEvent::Delete};
    if (vk == VK_TAB) return KeyEvent{KeyEvent::Tab};
    if (vk == VK_RETURN) return KeyEvent{KeyEvent::Enter};
    if (vk == VK_ESCAPE) return KeyEvent{KeyEvent::Escape};
    if (vk == VK_BACK) return KeyEvent{KeyEvent::Backspace};
    if (ch == 3) return KeyEvent{KeyEvent::CtrlC};
    if (ch >= 32) {
        // Convert UTF-16 to UTF-8
        wchar_t wc = static_cast<wchar_t>(ch);
        char mb[8];
        int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, mb, sizeof(mb), nullptr, nullptr);
        if (len > 0) return KeyEvent{KeyEvent::Char, std::string(mb, len)};
    }
    return KeyEvent{KeyEvent::Unknown};
#else
    return std::nullopt;
#endif
}

// ─── FrameBuffer (off-screen line buffer for diff rendering) ───

class FrameBuffer {
public:
    void print(const char* color, std::string_view text) {
        current_line_ += color;
        current_line_ += text;
        current_line_ += ansi::reset;
    }

    void println(const char* color, std::string_view text) {
        print(color, text);
        newline();
    }

    void println_bold(const char* color, std::string_view text) {
        current_line_ += ansi::bold;
        print(color, text);
        newline();
    }

    void print_raw(std::string_view text) {
        current_line_ += text;
    }

    void println_raw(std::string_view text) {
        current_line_ += text;
        newline();
    }

    void print_separator(const char* color, int width = 0) {
        if (width <= 0) width = terminal_width();
        std::string line;
        for (int i = 0; i < width; ++i) line += "\xe2\x94\x80";
        println(color, line);
    }

    void newline() {
        lines_.push_back(std::move(current_line_));
        current_line_.clear();
    }

    void set_cursor(int line, int col) {
        cursor_line_ = line;
        cursor_col_ = col;
    }

    auto lines() const -> const std::vector<std::string>& { return lines_; }
    auto line_count() const -> int { return static_cast<int>(lines_.size()); }
    auto cursor_line() const -> int { return cursor_line_; }
    auto cursor_col() const -> int { return cursor_col_; }

    void clear() {
        lines_.clear();
        current_line_.clear();
        cursor_line_ = -1;
        cursor_col_ = 1;
    }

private:
    std::vector<std::string> lines_;
    std::string current_line_;
    int cursor_line_{-1};
    int cursor_col_{1};
};

// ─── LineEditor (input line editor) ───

class LineEditor {
public:
    std::function<void()> on_change;
    std::function<void(std::string)> on_enter;

    void handle_key(const KeyEvent& key) {
        switch (key.type) {
            case KeyEvent::Char:
                content_.insert(cursor_, key.ch);
                cursor_ += key.ch.size();
                if (on_change) on_change();
                break;
            case KeyEvent::Backspace:
                if (cursor_ > 0) {
                    // Back up past UTF-8 continuation bytes
                    auto prev = cursor_;
                    do { --prev; } while (prev > 0 &&
                        (static_cast<unsigned char>(content_[prev]) & 0xC0) == 0x80);
                    content_.erase(prev, cursor_ - prev);
                    cursor_ = prev;
                    if (on_change) on_change();
                }
                break;
            case KeyEvent::Delete:
                if (cursor_ < content_.size()) {
                    auto next = cursor_ + 1;
                    while (next < content_.size() &&
                           (static_cast<unsigned char>(content_[next]) & 0xC0) == 0x80)
                        ++next;
                    content_.erase(cursor_, next - cursor_);
                    if (on_change) on_change();
                }
                break;
            case KeyEvent::Left:
                if (cursor_ > 0) {
                    do { --cursor_; } while (cursor_ > 0 &&
                        (static_cast<unsigned char>(content_[cursor_]) & 0xC0) == 0x80);
                }
                break;
            case KeyEvent::Right:
                if (cursor_ < content_.size()) {
                    ++cursor_;
                    while (cursor_ < content_.size() &&
                           (static_cast<unsigned char>(content_[cursor_]) & 0xC0) == 0x80)
                        ++cursor_;
                }
                break;
            case KeyEvent::Home:
                cursor_ = 0;
                break;
            case KeyEvent::End:
                cursor_ = content_.size();
                break;
            case KeyEvent::Enter:
                if (on_enter) on_enter(content_);
                break;
            default:
                break;
        }
    }

    void set_content(const std::string& s) {
        content_ = s;
        cursor_ = s.size();
    }

    auto content() const -> const std::string& { return content_; }
    auto cursor_pos() const -> std::size_t { return cursor_; }

    // Render input line to stdout (inline: no newline, positions cursor)
    // prefix_cols = number of display columns already printed on this line (e.g. 2 for "> ")
    void render(int width, int prefix_cols = 0) {
        // Print content
        std::fwrite(content_.data(), 1, content_.size(), stdout);
        // Pad to clear old text
        int content_display = display_width(content_);
        int avail = width - prefix_cols;
        if (content_display < avail) {
            std::string pad(avail - content_display, ' ');
            std::fwrite(pad.data(), 1, pad.size(), stdout);
        }
        // Use absolute cursor column positioning
        // Column = prefix_cols + display_width(content[0..cursor_]) + 1 (1-based)
        int cursor_display = display_width(std::string_view(content_.data(), cursor_));
        cursor_column(prefix_cols + cursor_display + 1);
    }

    // Render input line to a FrameBuffer (for diff rendering)
    void render(FrameBuffer& buf, int width, int prefix_cols = 0) {
        buf.print_raw(content_);
        int content_display = display_width(content_);
        int avail = width - prefix_cols;
        if (content_display < avail) {
            std::string pad(avail - content_display, ' ');
            buf.print_raw(pad);
        }
        int cursor_display = display_width(std::string_view(content_.data(), cursor_));
        buf.set_cursor(buf.line_count(), prefix_cols + cursor_display + 1);
    }

    // Calculate display width of a UTF-8 string (ASCII=1, CJK/emoji=2, other=1)
    static int display_width(std::string_view s) {
        int w = 0;
        std::size_t i = 0;
        while (i < s.size()) {
            auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                // ASCII
                w += 1;
                i += 1;
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte UTF-8
                w += 1;
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte UTF-8: CJK ranges are typically wide
                if (i + 2 < s.size()) {
                    std::uint32_t cp = ((c & 0x0F) << 12)
                                | ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 6)
                                | (static_cast<unsigned char>(s[i+2]) & 0x3F);
                    w += is_wide_codepoint(cp) ? 2 : 1;
                } else {
                    w += 1;
                }
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte UTF-8: emoji etc, typically wide
                w += 2;
                i += 4;
            } else {
                w += 1;
                i += 1;
            }
        }
        return w;
    }

private:
    static bool is_wide_codepoint(std::uint32_t cp) {
        // CJK Unified Ideographs, Katakana, Hiragana, Hangul, fullwidth forms, etc.
        return (cp >= 0x1100 && cp <= 0x115F)   // Hangul Jamo
            || (cp >= 0x2E80 && cp <= 0x303E)   // CJK Radicals, Kangxi, CJK Symbols
            || (cp >= 0x3040 && cp <= 0x33BF)   // Hiragana, Katakana, CJK Compat
            || (cp >= 0x3400 && cp <= 0x4DBF)   // CJK Ext A
            || (cp >= 0x4E00 && cp <= 0x9FFF)   // CJK Unified Ideographs
            || (cp >= 0xA000 && cp <= 0xA4CF)   // Yi
            || (cp >= 0xAC00 && cp <= 0xD7AF)   // Hangul Syllables
            || (cp >= 0xF900 && cp <= 0xFAFF)   // CJK Compat Ideographs
            || (cp >= 0xFE30 && cp <= 0xFE6F)   // CJK Compat Forms
            || (cp >= 0xFF01 && cp <= 0xFF60)   // Fullwidth Forms
            || (cp >= 0xFFE0 && cp <= 0xFFE6)   // Fullwidth Signs
            || (cp >= 0x20000 && cp <= 0x2FA1F); // CJK Ext B-F, Compat Supplement
    }

private:
    std::string content_;
    std::size_t cursor_{0};
};

// ─── Screen (event loop + active area management) ───

class Screen {
public:
    // Set render callback: writes to FrameBuffer instead of stdout
    void set_renderer(std::function<void(FrameBuffer&)> renderer) {
        renderer_ = std::move(renderer);
    }

    // Set key handler: returns true if consumed
    void set_key_handler(std::function<bool(const KeyEvent&)> handler) {
        key_handler_ = std::move(handler);
    }

    // Thread-safe: post lambda to main loop
    void post(std::function<void()> fn) {
        std::lock_guard lk(mtx_);
        queue_.push_back(std::move(fn));
        dirty_ = true;
    }

    // Trigger next frame redraw
    void refresh() {
        dirty_ = true;
    }

    // Get last active line count (for external clear)
    int last_active_lines() const { return last_active_lines_; }

    // Flush active area to scrollback: clear active area, run print_fn (stdout),
    // then reset frame state so next render starts fresh.
    // Must be called from main thread (inside post() lambda).
    void flush_to_scrollback(std::function<void()> print_fn) {
        hide_cursor();
        if (last_active_lines_ > 0) {
            int cursor_at = (prev_cursor_line_ >= 0)
                ? prev_cursor_line_ : (last_active_lines_ - 1);
            std::fwrite("\r", 1, 1, stdout);
            if (cursor_at > 0) cursor_up(cursor_at);
            clear_to_end();
        }
        show_cursor();
        if (print_fn) print_fn();
        tinytui::flush();
        prev_frame_.clear();
        last_active_lines_ = 0;
        prev_cursor_line_ = -1;
        dirty_ = true;
    }

    // Blocking event loop with diff rendering
    void loop() {
        enable_raw_mode();

        while (!quit_) {
            // 1. Drain queue
            {
                std::deque<std::function<void()>> batch;
                {
                    std::lock_guard lk(mtx_);
                    batch.swap(queue_);
                }
                for (auto& fn : batch) fn();
            }

            // 2. Redraw if dirty (diff rendering)
            if (dirty_.exchange(false)) {
                hide_cursor();

                // Render to off-screen buffer
                frame_buf_.clear();
                if (renderer_) {
                    renderer_(frame_buf_);
                }
                auto& new_lines = frame_buf_.lines();
                int new_count = static_cast<int>(new_lines.size());
                int old_count = static_cast<int>(prev_frame_.size());
                int new_width = terminal_width();

                // Move cursor to top of active area
                // (cursor was left at prev_cursor_line_ from last frame)
                if (old_count > 0) {
                    int cursor_at = (prev_cursor_line_ >= 0)
                        ? prev_cursor_line_ : (old_count - 1);
                    std::fwrite("\r", 1, 1, stdout);
                    if (cursor_at > 0) cursor_up(cursor_at);
                }

                bool full_repaint = (new_width != prev_width_) || (new_count != old_count);

                if (full_repaint) {
                    // Full repaint: write all new lines, clear leftover old lines
                    int max_lines = std::max(old_count, new_count);
                    for (int i = 0; i < max_lines; ++i) {
                        clear_line();
                        if (i < new_count) {
                            std::fwrite(new_lines[i].data(), 1, new_lines[i].size(), stdout);
                        }
                        if (i < max_lines - 1) {
                            std::fwrite("\n", 1, 1, stdout);
                        }
                    }
                    // If old was taller, cursor is past new_count; move back
                    if (old_count > new_count && old_count > 0) {
                        // We're at line (max_lines-1), need to be at (new_count-1)
                        int excess = old_count - new_count;
                        cursor_up(excess);
                    }
                } else {
                    // Diff: only update changed lines
                    for (int i = 0; i < new_count; ++i) {
                        if (i < old_count && new_lines[i] == prev_frame_[i]) {
                            // Line unchanged, just move down
                            if (i < new_count - 1) {
                                std::fwrite("\n", 1, 1, stdout);
                            }
                        } else {
                            clear_line();
                            std::fwrite(new_lines[i].data(), 1, new_lines[i].size(), stdout);
                            if (i < new_count - 1) {
                                std::fwrite("\n", 1, 1, stdout);
                            }
                        }
                    }
                }

                // Save frame state
                prev_frame_.assign(new_lines.begin(), new_lines.end());
                prev_width_ = new_width;
                last_active_lines_ = new_count;

                // Position cursor at input line
                int cl = frame_buf_.cursor_line();
                int cc = frame_buf_.cursor_col();
                if (cl >= 0 && new_count > 0) {
                    int lines_up = (new_count - 1) - cl;
                    if (lines_up > 0) cursor_up(lines_up);
                    cursor_column(cc);
                    prev_cursor_line_ = cl;
                    show_cursor();
                } else {
                    prev_cursor_line_ = new_count > 0 ? new_count - 1 : 0;
                }
                tinytui::flush();
            }

            // 3. Poll stdin for key
            if (auto key = read_key(42)) {
                if (key_handler_) {
                    key_handler_(*key);
                }
            }
        }

        // Cleanup: clear active area
        if (last_active_lines_ > 0) {
            hide_cursor();
            int cursor_at = (prev_cursor_line_ >= 0)
                ? prev_cursor_line_ : (last_active_lines_ - 1);
            std::fwrite("\r", 1, 1, stdout);
            if (cursor_at > 0) cursor_up(cursor_at);
            clear_to_end();
            last_active_lines_ = 0;
        }
        show_cursor();
        tinytui::flush();
        restore_mode();
    }

    // Exit event loop (can be called from any thread)
    void exit() {
        quit_ = true;
    }

private:
    std::mutex mtx_;
    std::deque<std::function<void()>> queue_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> dirty_{true};
    int last_active_lines_{0};

    // Diff rendering state
    std::vector<std::string> prev_frame_;
    int prev_width_{0};
    int prev_cursor_line_{-1};
    FrameBuffer frame_buf_;

    std::function<void(FrameBuffer&)> renderer_;
    std::function<bool(const KeyEvent&)> key_handler_;

#if defined(__linux__) || defined(__APPLE__)
    struct termios orig_termios_{};
    bool raw_mode_{false};

    void enable_raw_mode() {
        if (::tcgetattr(STDIN_FILENO, &orig_termios_) == -1) return;
        raw_mode_ = true;
        struct termios raw = orig_termios_;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    void restore_mode() {
        if (raw_mode_) {
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios_);
            raw_mode_ = false;
        }
    }
#elif defined(_WIN32)
    DWORD orig_mode_{0};
    bool raw_mode_{false};

    void enable_raw_mode() {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hIn, &orig_mode_);
        raw_mode_ = true;
        DWORD mode = orig_mode_;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hIn, mode);
        // Enable VT processing on stdout
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode;
        GetConsoleMode(hOut, &outMode);
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, outMode);
    }

    void restore_mode() {
        if (raw_mode_) {
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_mode_);
            raw_mode_ = false;
        }
    }
#else
    void enable_raw_mode() {}
    void restore_mode() {}
#endif
};

} // namespace xlings::tinytui
