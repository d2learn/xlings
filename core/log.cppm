module;
#include <ctime>
#include <cstdio>

export module xlings.log;

import std;

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

export void set_level(Level level) {
    gLevel_ = level;
}

export void set_level(const std::string& level) {
    if (level == "debug") gLevel_ = Level::Debug;
    else if (level == "info") gLevel_ = Level::Info;
    else if (level == "warn") gLevel_ = Level::Warn;
    else if (level == "error") gLevel_ = Level::Error;
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
        std::print("[debug] ");
        if (!gContext_.empty()) std::print("[{}] ", gContext_);
        std::println("{}", msg);
        write_to_file_("[debug] ", msg);
    }
}

export template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Info) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::print("[xlings] ");
        if (!gContext_.empty()) std::print("[{}] ", gContext_);
        std::println("{}", msg);
        write_to_file_("[xlings] ", msg);
    }
}

export template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Warn) {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        std::print("[warn] ");
        if (!gContext_.empty()) std::print("[{}] ", gContext_);
        std::println("{}", msg);
        write_to_file_("[warn] ", msg);
    }
}

export template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    std::print("[error] ");
    if (!gContext_.empty()) std::print("[{}] ", gContext_);
    std::println("{}", msg);
    write_to_file_("[error] ", msg);
}

} // namespace xlings::log
