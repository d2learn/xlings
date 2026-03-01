module;

#include <cstdio>

export module xlings.xself;

import std;

export import :install;
export import :init;

import xlings.config;
import xlings.json;
import xlings.platform;
import xlings.profile;
import xlings.utils;

namespace xlings::xself {

namespace fs = std::filesystem;

static int cmd_init() {
    auto& p = Config::paths();
    auto dirs = {p.homeDir, p.dataDir, p.subosDir, p.binDir, p.libDir,
                 p.subosDir / "usr", p.subosDir / "generations"};
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
    auto currentLink = p.homeDir / "subos" / "current";
    if (!fs::exists(currentLink)) {
        std::error_code ec;
        fs::create_directory_symlink(p.subosDir, currentLink, ec);
        if (!ec) std::println("[xlings:self]: created subos/current -> {}", p.subosDir.filename().string());
    }

    // Create subos shims from xlings binary (single-binary multicall)
    auto xlingsBin = p.homeDir / "xlings";
    if (!fs::exists(xlingsBin)) xlingsBin = p.homeDir / "bin" / "xlings";
    if (fs::exists(xlingsBin)) {
        ensure_subos_shims(p.binDir, xlingsBin, p.homeDir);
    }

    // Create subos .xlings.json if missing
    auto subosConfig = p.subosDir / ".xlings.json";
    if (!fs::exists(subosConfig)) {
        platform::write_string_to_file(subosConfig.string(), "{\"workspace\":{}}");
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

static int cmd_clean(bool dryRun = false) {
    auto& p = Config::paths();

    auto cachedir = p.homeDir / ".xlings";
    if (fs::exists(cachedir) && fs::is_directory(cachedir)) {
        if (dryRun) {
            std::println("[xlings:self] would remove cache: {}", cachedir.string());
        } else {
            std::error_code ec;
            fs::remove_all(cachedir, ec);
            if (ec) {
                std::println(stderr, "[xlings:self] failed to remove {} - {}", cachedir.string(), ec.message());
                return 1;
            }
            std::println("[xlings:self] cleaned cache: {}", cachedir.string());
        }
    }

    profile::gc(p.homeDir, dryRun);

    if (!dryRun) std::println("[xlings:self] clean ok");
    return 0;
}

static int cmd_migrate() {
    auto& p = Config::paths();
    auto subosDir   = p.homeDir / "subos";
    auto defaultDir = subosDir / "default";

    if (fs::exists(defaultDir / "bin")) {
        std::println("[xlings:self] already migrated (subos/default/bin exists)");
        return 0;
    }

    fs::create_directories(defaultDir);

    auto oldDataDir = p.homeDir / "data";
    bool moved = false;

    auto move_if_exists = [&](const std::string& name) {
        auto src = oldDataDir / name;
        auto dst = defaultDir / name;
        if (fs::exists(src)) {
            std::error_code ec;
            fs::rename(src, dst, ec);
            if (ec) {
                fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    std::println(stderr, "[xlings:self] failed to move {}: {}", name, ec.message());
                    return;
                }
                fs::remove_all(src, ec);
            }
            std::println("[xlings:self] migrated data/{} -> subos/default/{}", name, name);
            moved = true;
        }
    };

    move_if_exists("bin");
    move_if_exists("lib");
    move_if_exists("xvm");

    fs::create_directories(defaultDir / "usr");
    fs::create_directories(defaultDir / "generations");

    auto configPath = p.homeDir / ".xlings.json";
    nlohmann::json json;
    if (fs::exists(configPath)) {
        try {
            auto content = platform::read_file_to_string(configPath.string());
            json = nlohmann::json::parse(content, nullptr, false);
            if (json.is_discarded()) json = nlohmann::json::object();
        } catch (...) { json = nlohmann::json::object(); }
    }

    json["activeSubos"] = "default";
    if (!json.contains("subos")) json["subos"] = nlohmann::json::object();
    json["subos"]["default"] = {{"dir", ""}};
    platform::write_string_to_file(configPath.string(), json.dump(2));

    auto currentLink = subosDir / "current";
    std::error_code lec;
    fs::remove(currentLink, lec);
    fs::create_directory_symlink(defaultDir, currentLink, lec);

    if (moved) {
        std::println("[xlings:self] migration complete");
    } else {
        std::println("[xlings:self] no legacy data found; initialized subos/default");
    }
    return 0;
}

static int cmd_help() {
    std::println(R"(
xlings self [action]
  install install xlings from extracted release package
  init    create home/data/subos dirs
  update  git pull in XLINGS_HOME (if a repo)
  config  print paths
  clean   remove cache + gc orphaned packages (--dry-run)
  migrate migrate old layout to subos/default
  help    this message
)");
    return 0;
}

export int run(int argc, char* argv[]) {
    std::string action = (argc >= 3) ? argv[2] : "help";
    if (action == "install") return cmd_install();
    if (action == "init") return cmd_init();
    if (action == "update") return cmd_update();
    if (action == "config") return cmd_config();
    if (action == "clean") {
        bool dryRun = argc >= 4 && std::string(argv[3]) == "--dry-run";
        return cmd_clean(dryRun);
    }
    if (action == "migrate") return cmd_migrate();
    return cmd_help();
}

} // namespace xlings::xself
