export module xlings.cmdprocessor;

import std;

import xlings.log;
import xlings.json;
import xlings.config;
import xlings.subos;
import xlings.platform;
import xlings.xself;

namespace xlings::cmdprocessor {

struct CommandInfo {
    std::string name;
    std::string description;
    std::string usage;
    std::function<int(int argc, char* argv[])> func;
};

class CommandProcessor {
public:
    CommandProcessor& add(std::string name, std::string description,
                          std::function<int(int argc, char* argv[])> func,
                          std::string usage = "") {
        if (usage.empty()) usage = std::format("xlings {}", name);
        commands_.push_back({std::move(name), std::move(description),
                            std::move(usage), std::move(func)});
        return *this;
    }

    int run(int argc, char* argv[]) {
        if (argc <= 1) return print_help();

        std::string cmd = argv[1];
        if (cmd == "help" || cmd == "--help" || cmd == "-h" || cmd == "--version") {
            return print_help();
        }

        for (const auto& c : commands_) {
            if (c.name == cmd) return c.func(argc, argv);
        }

        log::error("Unknown command: {}", cmd);
        std::println("Use 'xlings help' for usage information");
        return 1;
    }

    int print_help() const {
        std::println("xlings version: {}\n", Info::VERSION);
        std::println("Usage: $ xlings [command] [target] [options]\n");
        std::println("Commands:");
        for (const auto& c : commands_) {
            std::println("\t {:12}\t{}", c.name, c.description);
        }
        std::println("\nPaths:");
        Config::print_paths();
        return 0;
    }

private:
    std::vector<CommandInfo> commands_;
};

int xim_exec(const std::string& flags, int argc, char* argv[], int startIdx = 2) {
    auto& p = Config::paths();
    std::string cmd = "xmake xim -P \"" + p.homeDir.string() + "\"";
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

int install_from_project_config() {
    namespace fs = std::filesystem;
    auto cfg = find_project_xlings_json();
    if (cfg.empty()) {
        log::error("No project .xlings.json found in current/parent directories");
        std::println("Tip: create <project>/.xlings.json with deps, or run `xlings install <package>`");
        return 1;
    }

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

    auto& p = Config::paths();
    int rc = 0;
    for (const auto& dep : json["deps"]) {
        if (!dep.is_string()) continue;
        auto target = dep.get<std::string>();
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
        .add("self", "self management (init/update/config/clean/migrate)",
            [](int argc, char* argv[]) {
                return xself::run(argc, argv);
            },
            "xlings self [init|update|config|clean [--dry-run]|migrate|help]");
}

} // namespace xlings::cmdprocessor
