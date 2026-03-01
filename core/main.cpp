import std;

import xlings.cli;
import xlings.config;
import xlings.platform;
import xlings.xvm.shim;

int main(int argc, char* argv[]) {
    auto& p = xlings::Config::paths();
    xlings::platform::set_env_variable("XLINGS_HOME", p.homeDir.string());

    // Multicall: check argv[0] to determine mode
    auto program_name = xlings::xvm::extract_program_name(argv[0]);

    if (xlings::xvm::is_xlings_binary(program_name)) {
        // Normal CLI mode
        return xlings::cli::run(argc, argv);
    }

    // Shim mode: dispatch to the real program
    return xlings::xvm::shim_dispatch(program_name, argc, argv);
}
