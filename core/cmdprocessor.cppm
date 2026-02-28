export module xlings.cmdprocessor;

import std;
import mcpplibs.cmdline;

import xlings.log;
import xlings.json;
import xlings.config;
import xlings.subos;
import xlings.platform;
import xlings.xself;

namespace xlings::cmdprocessor {

using namespace mcpplibs;

// ---------------------------------------------------------------------------
// Helper: resolve the xmake project directory that contains xim task.
// ---------------------------------------------------------------------------
std::filesystem::path find_xim_project_dir() {
    namespace fs = std::filesystem;

    auto is_xim_project = [](const fs::path& dir) {
        return fs::exists(dir / "xim") && fs::exists(dir / "xmake.lua");
    };

    // 1. Prefer layout next to the running binary (release package)
    auto exePath = platform::get_executable_path();
    if (!exePath.empty()) {
        auto candidate = exePath.parent_path().parent_path();
        if (is_xim_project(candidate)) {
            return candidate;
        }
    }

    // 2. $XLINGS_HOME (installed)
    auto homeDir = Config::paths().homeDir;
    if (is_xim_project(homeDir)) {
        return homeDir;
    }

    // 3. Default fallback (~/.xlings) when homeDir is the source tree
    auto defaultHome = std::filesystem::path(platform::get_home_dir()) / ".xlings";
    if (is_xim_project(defaultHome)) {
        return defaultHome;
    }

    return homeDir;
}

int xim_exec(const std::string& flags, int argc, char* argv[], int startIdx = 2) {
    auto projectDir = find_xim_project_dir();
    std::string cmd = "xmake xim -P \"" + projectDir.string() + "\"";
    if (!flags.empty()) {
        cmd += " " + flags;
    }
    cmd += " --";
    for (int i = startIdx; i < argc; ++i) {
        cmd += " ";
        cmd += argv[i];
    }
    return platform::exec(cmd);
}

int xvm_exec(const std::string& subcommand, int argc, char* argv[], int startIdx = 2) {
    std::string cmd = "xvm " + subcommand;
    for (int i = startIdx; i < argc; ++i) {
        cmd += " ";
        cmd += argv[i];
    }
    return platform::exec(cmd);
}

std::filesystem::path find_project_xlings_json() {
    namespace fs = std::filesystem;
    std::error_code ec;

    auto cwd = fs::current_path(ec);
    if (ec) return {};
    auto homeDir = Config::paths().homeDir;

    fs::path cur = cwd;
    while (!cur.empty()) {
        auto cfg = cur / ".xlings.json";
        if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
            auto curNorm = fs::weakly_canonical(cur, ec);
            if (!ec) {
                auto homeNorm = fs::weakly_canonical(homeDir, ec);
                if (!ec && curNorm == homeNorm) {
                    // Skip global config at $XLINGS_HOME/.xlings.json
                } else {
                    return cfg;
                }
            } else {
                return cfg;
            }
        }

        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

std::filesystem::path find_project_legacy_xlings_lua() {
    namespace fs = std::filesystem;
    std::error_code ec;

    auto cwd = fs::current_path(ec);
    if (ec) return {};
    auto homeDir = Config::paths().homeDir;

    fs::path cur = cwd;
    while (!cur.empty()) {
        auto cfg = cur / "config.xlings";
        if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
            auto curNorm = fs::weakly_canonical(cur, ec);
            if (!ec) {
                auto homeNorm = fs::weakly_canonical(homeDir, ec);
                if (!ec && curNorm == homeNorm) {
                    // Skip global config under $XLINGS_HOME
                } else {
                    return cfg;
                }
            } else {
                return cfg;
            }
        }

        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

static int install_targets_from_list(const std::vector<std::string>& targets) {
    auto& p = Config::paths();
    int rc = 0;
    for (const auto& target : targets) {
        if (target.empty()) continue;
        std::string cmd = std::format(
            "xmake xim -P \"{}\" -- {} -y",
            p.homeDir.string(),
            target
        );
        auto dep_rc = platform::exec(cmd);
        if (dep_rc != 0) {
            rc = dep_rc;
            break;
        }
    }
    return rc;
}

static int install_from_legacy_config_xlings_via_xim(const std::filesystem::path& cfg) {
    auto projectDir = find_xim_project_dir();
    std::string cmd = "xmake xim -P \"" + projectDir.string() + "\" -- --install-config-xlings \"" + cfg.string() + "\" -y";
    return platform::exec(cmd);
}

int install_from_project_config() {
    auto cfg = find_project_xlings_json();
    if (!cfg.empty()) {
        nlohmann::json json;
        try {
            auto content = platform::read_file_to_string(cfg.string());
            json = nlohmann::json::parse(content, nullptr, false);
        } catch (...) {
            json = nlohmann::json::object();
        }
        if (json.is_discarded() || !json.is_object()) {
            log::error("Invalid JSON in {}", cfg.string());
            return 1;
        }
        if (!json.contains("deps") || !json["deps"].is_array()) {
            log::error("Missing or invalid `deps` array in {}", cfg.string());
            return 1;
        }

        std::vector<std::string> targets;
        for (const auto& dep : json["deps"]) {
            if (!dep.is_string()) continue;
            auto target = dep.get<std::string>();
            if (!target.empty()) targets.push_back(std::move(target));
        }
        return install_targets_from_list(targets);
    }

    // Forward-compat for legacy project config.xlings (Lua):
    auto legacyCfg = find_project_legacy_xlings_lua();
    if (!legacyCfg.empty()) {
        return install_from_legacy_config_xlings_via_xim(legacyCfg);
    }

    std::println("Tip: create <project>/.xlings.json with deps (preferred), or use config.xlings xim table, or run `xlings install <package>`");
    return 0;
}

// ---------------------------------------------------------------------------
// CommandProcessor: backed by mcpplibs::cmdline::App for subcommand
// registration and dispatch. Help/version are handled with the original
// format to stay compatible with existing tests and users.
// ---------------------------------------------------------------------------

struct CmdEntry {
    std::string name;
    std::string description;
    std::string usage;
    std::function<int(int, char**)> func;
};

export class CommandProcessor {
public:
    CommandProcessor() : app_("xlings") {}

    CommandProcessor& add(std::string name, std::string description,
                          std::function<int(int argc, char* argv[])> func,
                          std::string usage = "") {
        if (usage.empty()) usage = std::format("xlings {}", name);
        // Register in cmdline App for structured subcommand dispatch
        app_.subcommand(name).description(description).end();
        cmds_.push_back({std::move(name), std::move(description),
                        std::move(usage), std::move(func)});
        return *this;
    }

    int run(int argc, char* argv[]) {
        if (argc <= 1) return print_help();

        std::string_view first = argv[1];

        // Intercept help/version before delegating to cmdline so we control
        // the output format (E2E tests expect "Commands:", not "SUBCOMMANDS:").
        if (first == "-h" || first == "--help" || first == "help" || first == "--version") {
            if (first == "--version") {
                std::println("{}", Info::VERSION);
                return 0;
            }
            return print_help();
        }

        // Build a synthetic argv for cmdline::App::parse() that inserts "--"
        // after argv[1] (the subcommand token). This stops cmdline from
        // interpreting downstream flags (-y, --global, --dry-run, …) as its
        // own unknown options, while still letting it identify the subcommand.
        std::string sep = "--";
        std::vector<char*> synth;
        synth.reserve(static_cast<std::size_t>(argc) + 1);
        synth.push_back(argv[0]);
        synth.push_back(argv[1]);
        synth.push_back(sep.data());
        for (int i = 2; i < argc; ++i) synth.push_back(argv[i]);
        int synth_argc = static_cast<int>(synth.size());

        auto result = app_.parse(synth_argc, synth.data());
        if (!result) {
            if (result.error().is_error()) {
                log::error("Unknown command: {}", first);
                std::println("Use 'xlings help' for usage information");
            }
            return result.error().is_error() ? 1 : 0;
        }

        if (result->has_subcommand()) {
            auto sub_name = std::string(result->subcommand_name());
            for (const auto& cmd : cmds_) {
                if (cmd.name == sub_name) {
                    return cmd.func(argc, argv);  // pass original argv
                }
            }
        }

        log::error("Unknown command: {}", first);
        std::println("Use 'xlings help' for usage information");
        return 1;
    }

    int print_help() const {
        std::println("xlings version: {}\n", Info::VERSION);
        std::println("Usage: $ xlings [command] [target] [options]\n");
        std::println("Commands:");
        for (const auto& cmd : cmds_) {
            std::println("\t {:12}\t{}", cmd.name, cmd.description);
        }
        return 0;
    }

private:
    cmdline::App app_;
    std::vector<CmdEntry> cmds_;
};

export CommandProcessor create_processor() {
    return CommandProcessor{}
        .add("install", "install package/tool",
            [](int argc, char* argv[]) {
                if (argc <= 2) {
                    return install_from_project_config();
                }
                return xim_exec("", argc, argv);
            },
            "xlings install [package[@version]]")
        .add("remove", "remove package/tool",
            [](int argc, char* argv[]) {
                return xim_exec("", argc, argv, 1);
            },
            "xlings remove <package>")
        .add("update", "update package/tool",
            [](int argc, char* argv[]) {
                return xim_exec("", argc, argv, 1);
            },
            "xlings update <package>")
        .add("search", "search for packages",
            [](int argc, char* argv[]) {
                return xim_exec("", argc, argv, 1);
            },
            "xlings search <keyword>")
        .add("use", "switch version of a tool",
            [](int argc, char* argv[]) {
                if (argc < 3) {
                    std::println("Usage: xlings use <target> [version]");
                    return 1;
                }
                if (argc >= 4) {
                    return xvm_exec("use", argc, argv);
                }
                return xvm_exec("list", argc, argv);
            },
            "xlings use <target> [version]")
        .add("info", "show tool info",
            [](int argc, char* argv[]) {
                if (argc < 3) {
                    std::println("Usage: xlings info <target>");
                    return 1;
                }
                return xvm_exec("info", argc, argv);
            },
            "xlings info <target>")
        .add("config", "show xlings configuration",
            [](int, char**) {
                Config::print_paths();
                return 0;
            })
        .add("subos", "manage sub-os environments",
            [](int argc, char* argv[]) {
                return subos::run(argc, argv);
            },
            "xlings subos <new|use|list|ls|remove|rm|info|i> [name]")
        .add("script", "run xpkg script",
            [](int argc, char* argv[]) {
                if (argc < 3) {
                    std::println("Usage: xlings script <script-file> [args...]");
                    return 1;
                }
                auto projectDir = find_xim_project_dir();
                std::string cmd = "xmake xscript -P \"" + projectDir.string() + "\" --";
                for (int i = 2; i < argc; ++i) {
                    cmd += " \"";
                    cmd += argv[i];
                    cmd += "\"";
                }
                return platform::exec(cmd);
            },
            "xlings script <script-file> [args...]")
        .add("self", "self management (init/update/config/clean/migrate)",
            [](int argc, char* argv[]) {
                return xself::run(argc, argv);
            },
            "xlings self [init|update|config|clean [--dry-run]|migrate|help]");
}

} // namespace xlings::cmdprocessor
