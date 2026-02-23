export module xlings.platform;

import std;

export import :linux;
export import :macos;
export import :windows;

namespace xlings {
namespace platform {

    static std::string gRundir = std::filesystem::current_path().string();

    export using platform_impl::PATH_SEPARATOR;
    export using platform_impl::clear_console;
    export using platform_impl::get_home_dir;
    export using platform_impl::get_executable_path;
    export using platform_impl::set_env_variable;
    export using platform_impl::println;

    export [[nodiscard]] std::string get_rundir() {
        return gRundir;
    }

    export void set_rundir(const std::string& dir) {
        gRundir = dir;
    }

    export [[nodiscard]] std::string get_system_language() {
        try {
            auto loc = std::locale("");
            auto name = loc.name();

            if (name.empty() || name == "C" || name == "POSIX") {
                return "en";
            }

            if (auto pos = name.find_first_of("_-.@"); pos != std::string::npos) {
                return name.substr(0, pos);
            }

            return name;
        } catch (const std::runtime_error&) {
            return "en";
        }
    }

    export std::pair<int, std::string> run_command_capture(const std::string& cmd) {
        return platform_impl::run_command_capture(cmd);
    }

    export int exec(const std::string& cmd) {
        return std::system(cmd.c_str());
    }

} // namespace platform
} // namespace xlings
