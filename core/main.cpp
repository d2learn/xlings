import std;

import xlings.cmdprocessor;
import xlings.config;
import xlings.platform;

int main(int argc, char* argv[]) {
    auto& p = xlings::Config::paths();
    xlings::platform::set_env_variable("XLINGS_HOME", p.homeDir.string());
    xlings::platform::set_env_variable("XLINGS_DATA", p.dataDir.string());
    xlings::platform::set_env_variable("XLINGS_SUBOS", p.subosDir.string());

    auto processor = xlings::cmdprocessor::create_processor();
    return processor.run(argc, argv);
}
