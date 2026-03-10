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

    // Multicall: check argv[0] to determine mode
    auto program_name = xlings::xvm::extract_program_name(argv[0]);

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
