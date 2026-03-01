import std;

import xlings.cli;
import xlings.config;
import xlings.platform;
import xlings.xvm.shim;

#ifdef __APPLE__
#include <cstdlib>  // std::_Exit
#endif

int main(int argc, char* argv[]) {
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
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(rc);
#else
    return rc;
#endif
}
