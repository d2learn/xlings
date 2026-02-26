module;

#include <cstdio>
#include <cstdlib>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <sys/stat.h>
#endif

export module xlings.platform:macos;

#if defined(__APPLE__)

import std;

namespace xlings {
namespace platform_impl {

    export constexpr char PATH_SEPARATOR = ':';

    export std::filesystem::path get_executable_path() {
        char buf[4096];
        uint32_t size = sizeof(buf);
        if (::_NSGetExecutablePath(buf, &size) != 0) return {};
        char real[4096];
        if (::realpath(buf, real) == nullptr) return std::filesystem::path(buf);
        return std::filesystem::path(real);
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
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                ::chmod(entry.path().c_str(), 0755);
                std::string cmd = "codesign -s - -f \"" + entry.path().string() + "\" 2>/dev/null";
                std::system(cmd.c_str());
            }
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
        std::filesystem::create_directory_symlink(target, link, ec);
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

#endif // defined(__APPLE__)
