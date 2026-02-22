module;

#include <cstdio>

export module xlings.xself;

import std;

import xlings.config;
import xlings.platform;

namespace xlings::xself {

namespace fs = std::filesystem;

static int cmd_init() {
    auto& p = Config::paths();
    auto dirs = {p.homeDir, p.dataDir, p.binDir, p.libDir};
    for (const auto& d : dirs) {
        if (d.empty()) continue;
        if (!fs::exists(d)) {
            std::error_code ec;
            fs::create_directories(d, ec);
            if (ec) {
                std::println(stderr, "[xlings:self]: failed to create {} - {}", d.string(), ec.message());
                return 1;
            }
            std::println("[xlings:self]: created {}", d.string());
        }
    }
    std::println("[xlings:self]: init ok");
    return 0;
}

static int cmd_update() {
    auto& p = Config::paths();
    auto git_dir = p.homeDir / ".git";
    if (!fs::exists(git_dir) || !fs::is_directory(git_dir)) {
        std::println("[xlings:self]: {} is not a git repo, skip update", p.homeDir.string());
        return 0;
    }
    std::println("[xlings:self]: update from git ...");
    std::string cmd = "git -C \"" + p.homeDir.string() + "\" pull";
    int ret = platform::exec(cmd);
    return ret != 0 ? 1 : 0;
}

static int cmd_config() {
    Config::print_paths();
    return 0;
}

static int cmd_clean() {
    auto& p = Config::paths();
    auto cachedir = p.homeDir / ".xlings";
    if (fs::exists(cachedir) && fs::is_directory(cachedir)) {
        std::error_code ec;
        fs::remove_all(cachedir, ec);
        if (ec) {
            std::println(stderr, "[xlings:self]: failed to remove {} - {}", cachedir.string(), ec.message());
            return 1;
        }
        std::println("[xlings:self]: cleaned {}", cachedir.string());
    }
    std::println("[xlings:self]: clean ok");
    return 0;
}

static int cmd_help() {
    std::println(R"(
xlings self [action]
  init    create home/data/bin/lib dirs
  update  git pull in XLINGS_HOME (if a repo)
  config  print paths
  clean   remove runtime cache
  help    this message
)");
    return 0;
}

export int run(int argc, char* argv[]) {
    std::string action = (argc >= 3) ? argv[2] : "help";
    if (action == "init") return cmd_init();
    if (action == "update") return cmd_update();
    if (action == "config") return cmd_config();
    if (action == "clean") return cmd_clean();
    return cmd_help();
}

} // namespace xlings::xself
