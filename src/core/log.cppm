module;
#include <ctime>
#include <cstdio>

export module xlings.core.log;

import std;
import xlings.platform;

namespace xlings::log {

export enum class Level {
    Debug,
    Info,
    Warn,
    Error
};

Level gLevel_ { Level::Info };
std::string gContext_;
std::ofstream gFile_;
bool gColor_ { true };
// Terminal output is suppressed when platform::is_tui_mode() is true

export void set_level(Level level) {
    gLevel_ = level;
}

export void set_level(const std::string& level) {
    if (level == "debug") gLevel_ = Level::Debug;
    else if (level == "info") gLevel_ = Level::Info;
    else if (level == "warn") gLevel_ = Level::Warn;
    else if (level == "error") gLevel_ = Level::Error;
}

export Level get_level() {
    return gLevel_;
}

export std::string_view level_string() {
    switch (gLevel_) {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "warn";
        case Level::Error: return "error";
    }
    return "info";
}

export void set_file(const std::filesystem::path& path) {
    if (gFile_.is_open()) gFile_.close();
    if (!path.empty()) gFile_.open(path, std::ios::app);
}

export void set_context(std::string ctx) {
    gContext_ = std::move(ctx);
}

export void clear_context() {
    gContext_.clear();
}

export void enable_color(bool enabled) {
    gColor_ = enabled;
}

// ANSI color helpers (matching theme palette)
namespace ansi_ {
    constexpr auto reset   = "\033[0m";
    constexpr auto bold    = "\033[1m";
    constexpr auto dim     = "\033[2m";
    // Theme colors as RGB ANSI sequences
    constexpr auto cyan    = "\033[38;2;34;211;238m";   // #22D3EE
    constexpr auto green   = "\033[38;2;34;197;94m";    // #22C55E
    constexpr auto amber   = "\033[38;2;245;158;11m";   // #F59E0B
    constexpr auto red     = "\033[38;2;239;68;68m";    // #EF4444
    constexpr auto gray    = "\033[38;2;148;163;184m";  // #94A3B8
} // namespace ansi_

std::string colored_(const char* color, const char* text) {
    if (!gColor_) return text;
    return std::string(color) + text + ansi_::reset;
}

std::string timestamp_() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    ::localtime_s(&tm, &tt);
#else
    ::localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void write_to_file_(std::string_view prefix, std::string_view msg) {
    if (gFile_.is_open()) {
        gFile_ << timestamp_() << " " << prefix;
        if (!gContext_.empty()) {
            gFile_ << "[" << gContext_ << "] ";
        }
        gFile_ << msg << "\n";
        gFile_.flush();
    }
}

export template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Debug) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        if (!platform::is_tui_mode()) {
            std::print("{} ", colored_(ansi_::gray, "[debug]"));
            if (!gContext_.empty()) std::print("{} ", colored_(ansi_::gray, std::format("[{}]", gContext_).c_str()));
            std::println("{}", msg);
        }
        write_to_file_("[debug] ", msg);
    }
}

export template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Info) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        if (!platform::is_tui_mode()) {
            std::print("{} ", colored_(ansi_::cyan, "[xlings]"));
            if (!gContext_.empty()) std::print("{} ", colored_(ansi_::cyan, std::format("[{}]", gContext_).c_str()));
            std::println("{}", msg);
        }
        write_to_file_("[xlings] ", msg);
    }
}

export template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Warn) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        if (!platform::is_tui_mode()) {
            std::print(stderr, "{} ", colored_(ansi_::amber, "[warn]"));
            if (!gContext_.empty()) std::print(stderr, "{} ", colored_(ansi_::amber, std::format("[{}]", gContext_).c_str()));
            std::println(stderr, "{}", msg);
        }
        write_to_file_("[warn] ", msg);
    }
}

export template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    if (!platform::is_tui_mode()) {
        if (gColor_) {
            std::print(stderr, "{}{}[error]{} ", ansi_::bold, ansi_::red, ansi_::reset);
        } else {
            std::print(stderr, "[error] ");
        }
        if (!gContext_.empty()) std::print(stderr, "{} ", colored_(ansi_::red, std::format("[{}]", gContext_).c_str()));
        std::println(stderr, "{}", msg);
    }
    write_to_file_("[error] ", msg);
}

export template<typename... Args>
void println(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Info) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        if (!platform::is_tui_mode()) std::println("{}", msg);
        write_to_file_("[status] ", msg);
    }
}

export template<typename... Args>
void print(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Info) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::print("{}", msg);
    }
}

} // namespace xlings::log
