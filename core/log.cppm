export module xlings.log;

import std;

namespace xlings::log {

export enum class Level {
    Debug,
    Info,
    Warn,
    Error
};

Level gLevel_ = Level::Info;

export void set_level(Level level) {
    gLevel_ = level;
}

export void set_level(const std::string& level) {
    if (level == "debug") gLevel_ = Level::Debug;
    else if (level == "info") gLevel_ = Level::Info;
    else if (level == "warn") gLevel_ = Level::Warn;
    else if (level == "error") gLevel_ = Level::Error;
}

export template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Debug) {
        std::print("[debug] ");
        std::println(fmt, std::forward<Args>(args)...);
    }
}

export template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Info) {
        std::print("[xlings] ");
        std::println(fmt, std::forward<Args>(args)...);
    }
}

export template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (gLevel_ <= Level::Warn) {
        std::print("[warn] ");
        std::println(fmt, std::forward<Args>(args)...);
    }
}

export template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    std::print("[error] ");
    std::println(fmt, std::forward<Args>(args)...);
}

} // namespace xlings::log
