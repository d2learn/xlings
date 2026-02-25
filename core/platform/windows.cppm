module;

#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#include <windows.h>
#endif

export module xlings.platform:windows;

#if defined(_WIN32)

import std;

namespace xlings {
namespace platform_impl {

    export constexpr char PATH_SEPARATOR = ';';

    export std::filesystem::path get_executable_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0) return {};
        return std::filesystem::path(buf);
    }

    export std::pair<int, std::string> run_command_capture(const std::string& cmd) {
        std::string full = cmd + " 2>&1";
        FILE* pipe = ::_popen(full.c_str(), "r");
        if (!pipe) {
            return {-1, std::string{}};
        }
        std::string output;
        std::array<char, 256> buffer{};
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        int status = ::_pclose(pipe);
        return {status, output};
    }

    export void clear_console() {
        std::system("cls");
    }

    export std::string get_home_dir() {
        if (const char* home = std::getenv("USERPROFILE")) return home;
        return ".";
    }

    export void set_env_variable(const std::string& key, const std::string& value) {
        ::_putenv_s(key.c_str(), value.c_str());
    }

    export void make_files_executable(const std::filesystem::path&) {
        // No-op: Windows does not use Unix file permissions
    }

    export bool create_directory_link(const std::filesystem::path& link,
                                      const std::filesystem::path& target) {
        std::error_code ec;
        if (std::filesystem::is_symlink(link)) {
            std::filesystem::remove(link, ec);
        } else if (std::filesystem::exists(link)) {
            std::filesystem::remove_all(link, ec);
        }
        auto canonTarget = std::filesystem::canonical(target, ec);
        if (ec) canonTarget = target;
        std::string cmd = "cmd /c mklink /J \"" + link.string() +
                          "\" \"" + canonTarget.string() + "\" >nul 2>&1";
        return std::system(cmd.c_str()) == 0;
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

#endif // defined(_WIN32)
