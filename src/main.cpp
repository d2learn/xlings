import std;

import xlings.cli;
import xlings.core.config;
import xlings.platform;
import xlings.core.xvm.shim;

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDOUT_FD 1
#else
#include <unistd.h>
#define STDOUT_FD STDOUT_FILENO
#endif

#ifdef __APPLE__
#include <cstdlib>  // std::_Exit
#endif

int main(int argc, char* argv[]) {
    // Restore terminal cursor visibility on exit (safety net for TUI download progress)
    // Only emit when stdout is a TTY to avoid polluting captured output
    std::atexit([]() {
        if (isatty(STDOUT_FD)) {
            std::cout << "\033[?25h" << std::flush;
        }
    });

    xlings::platform::init_console_output();

    auto& p = xlings::Config::paths();
    xlings::platform::set_env_variable("XLINGS_HOME", p.homeDir.string());

    // Multicall: check argv[0] to determine mode.
    auto program_name = xlings::xvm::extract_program_name(argv[0]);

    // 0.4.8: short-command aliases (xim/xvm/xself/xsubos/xinstall) were
    // removed. If the user invoked one — usually via a leftover symlink
    // from an older install or a hand-typed habit — give them a clear
    // migration message instead of the cryptic "no version set for X"
    // they'd otherwise hit when shim_dispatch tries to resolve the name.
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5>
        DEPRECATED_ALIASES = {{
            {"xim",      "xlings install/remove/search/list/info ..."},
            {"xvm",      "xlings use ..."},
            {"xself",    "xlings self ..."},
            {"xsubos",   "xlings subos ..."},
            {"xinstall", "xlings install ..."},
        }};
    for (auto& [alias, suggestion] : DEPRECATED_ALIASES) {
        if (alias == program_name) {
            std::println(std::cerr,
                "[error] `{}` was removed in 0.4.8. Use `{}` instead.",
                alias, suggestion);
            std::println(std::cerr,
                "        Run `xlings self doctor --fix` to clean up "
                "leftover shortcuts.");
            return 2;
        }
    }

    int rc;
    if (xlings::xvm::is_xlings_binary(program_name)) {
        rc = xlings::cli::run(argc, argv);
    } else {
        rc = xlings::xvm::shim_dispatch(program_name, argc, argv);
    }

#ifdef __APPLE__
    // On macOS, static libc++ linked with dynamic libc++abi causes SIGABRT
    // during static destruction. Skip destructors — CLI tool needs no cleanup.
    // _Exit skips atexit handlers, so restore cursor explicitly here.
    if (isatty(STDOUT_FD)) std::cout << "\033[?25h" << std::flush;
    std::cerr.flush();
    std::_Exit(rc);
#else
    return rc;
#endif
}
