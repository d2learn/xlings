export module xlings.xself;

import std;

export import :init;
export import :install;

import xlings.config;
import xlings.json;
import xlings.log;
import xlings.platform;
import xlings.profile;
import xlings.ui;
import xlings.utils;

namespace xlings::xself {

namespace fs = std::filesystem;

static int cmd_init() {
    auto& p = Config::paths();
    if (!ensure_home_layout(p.homeDir)) return 1;
    log::info("init ok");
    return 0;
}

static int cmd_update() {
    // Use platform::exec to avoid circular module dependency
    // (xvm.commands and xim.commands both import xself)

    // Step 1: update package index
    log::info("updating package index...");
    int rc = platform::exec("xlings update");
    if (rc != 0) {
        log::error("failed to update package index");
        return rc;
    }

    // Step 2: install xlings@latest
    log::info("installing xlings@latest...");
    rc = platform::exec("xlings install xlings@latest -y");
    if (rc != 0) {
        log::warn("xlings package not available or install failed, skipping");
    } else {
        // Step 3: switch to latest xlings
        platform::exec("xlings use xlings latest");
    }

    return 0;
}

static int cmd_config() {
    auto& p = Config::paths();
    std::vector<ui::InfoField> fields;
    fields.push_back({"XLINGS_HOME", p.homeDir.string()});
    fields.push_back({"XLINGS_DATA", p.dataDir.string()});
    fields.push_back({"XLINGS_SUBOS", p.subosDir.string()});
    fields.push_back({"active subos", p.activeSubos, true});
    fields.push_back({"bin", p.binDir.string()});

    auto mirror = Config::mirror();
    if (!mirror.empty()) fields.push_back({"mirror", mirror});
    auto lang = Config::lang();
    if (!lang.empty()) fields.push_back({"lang", lang});

    // Index repos
    auto& repos = Config::global_index_repos();
    for (auto& repo : repos) {
        fields.push_back({"index-repo", repo.name + " : " + repo.url});
    }

    if (Config::has_project_config()) {
        fields.push_back({"project data", Config::project_data_dir().string()});
        auto& projectRepos = Config::project_index_repos();
        for (auto& repo : projectRepos) {
            fields.push_back({"project repo", repo.name + " : " + repo.url});
        }
    }

    ui::print_info_panel("xlings config", fields);
    return 0;
}

static int cmd_clean(bool dryRun = false) {
    auto& p = Config::paths();

    auto cachedir = p.homeDir / ".xlings";
    if (fs::exists(cachedir) && fs::is_directory(cachedir)) {
        if (dryRun) {
            log::println("  would remove cache: {}", cachedir.string());
        } else {
            std::error_code ec;
            fs::remove_all(cachedir, ec);
            if (ec) {
                log::error("failed to remove {}: {}", cachedir.string(), ec.message());
                return 1;
            }
            log::debug("cleaned cache: {}", cachedir.string());
        }
    }

    profile::gc(p.homeDir, dryRun);

    if (!dryRun) log::info("clean ok");
    return 0;
}

static int cmd_migrate() {
    auto& p = Config::paths();
    auto subosDir   = p.homeDir / "subos";
    auto defaultDir = subosDir / "default";

    if (fs::exists(defaultDir / "bin")) {
        log::info("already migrated (subos/default/bin exists)");
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
                    log::error("failed to move {}: {}", name, ec.message());
                    return;
                }
                fs::remove_all(src, ec);
            }
            log::info("migrated data/{} -> subos/default/{}", name, name);
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
        log::info("migration complete");
    } else {
        log::info("no legacy data found; initialized subos/default");
    }
    return 0;
}

static int cmd_help() {
    ui::HelpOpt opts[] = {
        {"install",  "Install xlings from release package"},
        {"init",     "Create home/data/subos dirs"},
        {"update",   "Update index + install latest xlings"},
        {"config",   "Show configuration details"},
        {"clean",    "Remove cache + gc orphaned packages (--dry-run)"},
        {"migrate",  "Migrate old layout to subos/default"},
    };
    ui::print_subcommand_help("self", "Manage xlings itself", {}, opts);
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
