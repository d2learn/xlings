module;

#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/stat.h>
#endif

export module xlings.platform:linux;

#if defined(__linux__)

import std;

namespace xlings {
namespace platform_impl {

    export constexpr char PATH_SEPARATOR = ':';

    export std::filesystem::path get_executable_path() {
        char buf[4096];
        ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n == -1) return {};
        buf[n] = '\0';
        return std::filesystem::path(buf);
    }

    export std::pair<int, std::string> run_command_capture(const std::string& cmd) {
        std::string full = cmd + " 2>&1";
        FILE* pipe = ::popen(full.c_str(), "r");
        if (!pipe) {
            return {-1, std::string{}};
        }
        std::string output;
        std::array<char, 256> buffer{};
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        int status = ::pclose(pipe);
        return {status, output};
    }

    export void clear_console() {
        std::system("clear");
    }

    export std::string get_home_dir() {
        if (const char* home = std::getenv("HOME")) return home;
        return ".";
    }

    export void set_env_variable(const std::string& key, const std::string& value) {
        ::setenv(key.c_str(), value.c_str(), 1);
    }

    export void make_files_executable(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) return;
        for (auto it = std::filesystem::directory_iterator(dir); it != std::default_sentinel; ++it) {
            if (it->is_regular_file())
                ::chmod(it->path().c_str(), 0755);
        }
    }

    export bool create_directory_link(const std::filesystem::path& link,
                                      const std::filesystem::path& target) {
        std::error_code ec;
        if (std::filesystem::is_symlink(link)) {
            std::filesystem::remove(link, ec);
        } else if (std::filesystem::exists(link)) {
            std::filesystem::remove_all(link, ec);
        }
        auto linkTarget = target;
        auto rel = std::filesystem::relative(target, link.parent_path(), ec);
        if (!ec && !rel.empty()) linkTarget = rel;
        ec.clear();
        std::filesystem::create_directory_symlink(linkTarget, link, ec);
        return !static_cast<bool>(ec);
    }

    export template<typename... Args>
    void println(std::format_string<Args...> fmt, Args&&... args) {
        std::println(fmt, std::forward<Args>(args)...);
    }

    export inline void println(const std::string& msg) {
        std::println("{}", msg);
    }

} // namespace platform_impl
}

#endif // defined(__linux__)
