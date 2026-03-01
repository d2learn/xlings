export module xlings.cli;

import std;

import mcpplibs.cmdline;
import xlings.config;
import xlings.log;
import xlings.i18n;
import xlings.platform;
import xlings.subos;
import xlings.xself;
import xlings.xim.commands;
import xlings.xvm.commands;

namespace xlings::cli {

// Project config install (legacy compat)
int install_from_project_config_() {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (ec) return 1;
    auto homeDir = Config::paths().homeDir;

    fs::path cur = cwd;
    while (!cur.empty()) {
        auto cfg = cur / ".xlings.json";
        if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
            auto curNorm = fs::weakly_canonical(cur, ec);
            auto homeNorm = fs::weakly_canonical(homeDir, ec);
            if (curNorm != homeNorm) {
                std::string ximCmd = std::format(
                    "xmake xim -P \"{}\" -- --install-config-xlings \"{}\" -y",
                    homeDir.string(), cfg.string());
                return platform::exec(ximCmd);
            }
        }
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    // Check for config.xlings (legacy Lua)
    cur = cwd;
    while (!cur.empty()) {
        auto cfg = cur / "config.xlings";
        if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
            auto curNorm = fs::weakly_canonical(cur, ec);
            auto homeNorm = fs::weakly_canonical(homeDir, ec);
            if (curNorm != homeNorm) {
                std::string cmd = "xmake xim -P \"" + homeDir.string() +
                                  "\" -- --install-config-xlings \"" + cfg.string() + "\" -y";
                return platform::exec(cmd);
            }
        }
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    std::println("Tip: create <project>/.xlings.json with deps, or run `xlings install <package>`");
    return 0;
}

void apply_global_opts_(const mcpplibs::cmdline::ParsedArgs& args) {
    if (args.is_flag_set("verbose")) log::set_level(log::Level::Debug);
    if (args.is_flag_set("quiet")) log::set_level(log::Level::Error);
    if (auto lang = args.value("lang")) i18n::set_language(*lang);
}

export int run(int argc, char* argv[]) {
    using namespace mcpplibs;

    // Special: subos, self, script need raw argc/argv
    if (argc >= 2) {
        std::string cmd { argv[1] };

        // Handle -h/--help/--version before cmdline library to avoid
        // std::format width-specifier crash in GCC 15 C++23 modules.
        if (cmd == "-h" || cmd == "--help") {
            auto pad = [](std::string s, std::size_t w) {
                while (s.size() < w) s += ' ';
                return s;
            };
            std::println("xlings {}", Info::VERSION);
            std::println("\nA modern package manager and development environment tool\n");
            std::println("USAGE:");
            std::println("    xlings [OPTIONS] [SUBCOMMAND]\n");
            std::println("OPTIONS:");
            std::println("    {}  {}", pad("-y, --yes", 24), "Skip confirmation prompts");
            std::println("    {}  {}", pad("-v, --verbose", 24), "Enable verbose output");
            std::println("    {}  {}", pad("-q, --quiet", 24), "Suppress non-essential output");
            std::println("    {}  {}", pad("--lang <LANG>", 24), "Override language (en/zh)");
            std::println("    {}  {}", pad("--mirror <MIRROR>", 24), "Override mirror (GLOBAL/CN)");
            std::println("\nSUBCOMMANDS:");
            std::println("    {}  {}", pad("install", 12), "Install packages (e.g. xlings install gcc@15 node)");
            std::println("    {}  {}", pad("remove", 12), "Remove a package");
            std::println("    {}  {}", pad("update", 12), "Update package index or a specific package");
            std::println("    {}  {}", pad("search", 12), "Search for packages");
            std::println("    {}  {}", pad("list", 12), "List installed packages");
            std::println("    {}  {}", pad("info", 12), "Show package information");
            std::println("    {}  {}", pad("use", 12), "Switch tool version");
            std::println("    {}  {}", pad("config", 12), "Show xlings configuration");
            std::println("    {}  {}", pad("subos", 12), "Manage sub-OS environments");
            std::println("    {}  {}", pad("self", 12), "Manage xlings itself (install, update, clean)");
            std::println("    {}  {}", pad("script", 12), "Run xlings scripts");
            return 0;
        }
        if (cmd == "--version") {
            std::println("xlings {}", Info::VERSION);
            return 0;
        }

        if (cmd == "subos") return subos::run(argc, argv);
        if (cmd == "self") return xself::run(argc, argv);
        if (cmd == "script") {
            if (argc < 3) {
                std::println("Usage: xlings script <script-file> [args...]");
                return 1;
            }
            namespace fs = std::filesystem;
            auto homeDir = Config::paths().homeDir;
            fs::path projectDir = homeDir;
            auto exePath = platform::get_executable_path();
            if (!exePath.empty()) {
                auto candidate = exePath.parent_path().parent_path();
                if (fs::exists(candidate / "xim") && fs::exists(candidate / "xmake.lua"))
                    projectDir = candidate;
            }
            std::string scriptCmd = "xmake xscript -P \"" + projectDir.string() + "\" --";
            for (int i = 2; i < argc; ++i) {
                scriptCmd += " \"";
                scriptCmd += argv[i];
                scriptCmd += "\"";
            }
            return platform::exec(scriptCmd);
        }
    }

    auto app = cmdline::App("xlings")
        .version(std::string(Info::VERSION))
        .author("d2learn community")
        .description("A modern package manager and development environment tool")

        // Global options
        .option("yes").short_name('y').help("Skip confirmation prompts").global()
        .option("verbose").short_name('v').help("Enable verbose output").global()
        .option("quiet").short_name('q').help("Suppress non-essential output").global()
        .option("lang").takes_value().value_name("LANG").help("Override language (en/zh)").global()
        .option("mirror").takes_value().value_name("MIRROR").help("Override mirror (GLOBAL/CN)").global()

        // install: use global --use, package names are positional
        .subcommand("install")
            .description("Install packages (e.g. xlings install gcc@15 node)")
            .arg("packages").help("Package names with optional version")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::vector<std::string> targets;
                for (std::size_t i = 0; i < args.positional_count(); ++i) {
                    auto t = args.positional(i);
                    if (!t.empty()) targets.emplace_back(t);
                }
                if (targets.empty()) return install_from_project_config_();

                bool yes = args.is_flag_set("yes");
                return xim::cmd_install(targets, yes, false);
            })

        // remove
        .subcommand("remove")
            .description("Remove a package")
            .arg("package").required().help("Package to remove")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_remove(std::string(args.positional(0)));
            })

        // update
        .subcommand("update")
            .description("Update package index or a specific package")
            .arg("package").help("Package to update (omit for index only)")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::string target;
                if (args.positional_count() > 0)
                    target = std::string(args.positional(0));
                return xim::cmd_update(target);
            })

        // search
        .subcommand("search")
            .description("Search for packages")
            .arg("keyword").required().help("Search keyword")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_search(std::string(args.positional(0)));
            })

        // list
        .subcommand("list")
            .description("List installed packages")
            .arg("filter").help("Filter pattern")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::string filter;
                if (args.positional_count() > 0)
                    filter = std::string(args.positional(0));
                return xim::cmd_list(filter);
            })

        // info
        .subcommand("info")
            .description("Show package information")
            .arg("package").required().help("Package name")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_info(std::string(args.positional(0)));
            })

        // use
        .subcommand("use")
            .description("Switch tool version")
            .arg("target").required().help("Tool name")
            .arg("version").help("Version to switch to (omit to list)")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                auto target = std::string(args.positional(0));
                if (args.positional_count() >= 2) {
                    return xvm::cmd_use(target, std::string(args.positional(1)));
                }
                return xvm::cmd_list_versions(target);
            })

        // config
        .subcommand("config")
            .description("Show xlings configuration")
            .action([](const cmdline::ParsedArgs&) -> int {
                Config::print_paths();
                return 0;
            });

    return app.run(argc, argv);
}

} // namespace xlings::cli
