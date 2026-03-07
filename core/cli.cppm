export module xlings.cli;

import std;

import mcpplibs.cmdline;
import mcpplibs.capi.lua;
import mcpplibs.xpkg.executor;
import xlings.config;
import xlings.json;
import xlings.log;
import xlings.i18n;
import xlings.platform;
import xlings.subos;
import xlings.xself;
import xlings.xim.commands;
import xlings.xvm.types;
import xlings.xvm.db;
import xlings.xvm.commands;

namespace lua = mcpplibs::capi::lua;

namespace xlings::cli {

// Parse legacy config.xlings (Lua format) and extract workspace from the xim table.
// Returns empty workspace if file doesn't exist or has no xim/xlings_deps.
xvm::Workspace parse_legacy_config_(const std::filesystem::path& configFile) {
    namespace fs = std::filesystem;
    xvm::Workspace workspace;

    if (!fs::exists(configFile)) return workspace;

    auto* L = lua::L_newstate();
    if (!L) return workspace;
    lua::L_openlibs(L);

    // Provide a no-op is_host() so Lua files with conditionals don't error
    lua::L_dostring(L, "function is_host() return false end");

    if (lua::L_dofile(L, configFile.string().c_str()) != lua::OK) {
        log::warn("failed to parse legacy config: {}", lua::tostring(L, -1));
        lua::close(L);
        return workspace;
    }

    // Read xim table: { pkg_name = "version", ... }
    lua::getglobal(L, "xim");
    if (lua::type(L, -1) == lua::TTABLE) {
        lua::pushnil(L);
        while (lua::next(L, -2)) {
            if (lua::type(L, -2) == lua::TSTRING) {
                std::string key = lua::tostring(L, -2);
                // Skip non-package entries
                if (key != "xppcmds") {
                    std::string version;
                    if (lua::type(L, -1) == lua::TSTRING) {
                        version = lua::tostring(L, -1);
                    }
                    workspace[key] = version;
                }
            }
            lua::pop(L, 1); // pop value, keep key for next iteration
        }
    }
    lua::pop(L, 1); // pop xim

    // Fallback: older format uses xlings_deps = "cpp, vscode, mdbook"
    if (workspace.empty()) {
        lua::getglobal(L, "xlings_deps");
        if (lua::type(L, -1) == lua::TSTRING) {
            std::string deps = lua::tostring(L, -1);
            std::istringstream ss(deps);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    workspace[token.substr(start, end - start + 1)] = "";
                }
            }
        }
        lua::pop(L, 1);
    }

    lua::close(L);
    return workspace;
}

// Generate .xlings.json from a workspace map
void generate_xlings_json_(const std::filesystem::path& dir, const xvm::Workspace& workspace) {
    nlohmann::json ws;
    for (auto& [name, version] : workspace) {
        ws[name] = version;
    }
    nlohmann::json root;
    root["workspace"] = ws;
    auto outPath = dir / ".xlings.json";
    platform::write_string_to_file(outPath.string(), root.dump(2));
}

// Install packages from project .xlings.json workspace
int install_from_project_config_() {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (ec) return 1;
    auto homeDir = Config::paths().homeDir;

    // Walk up from cwd looking for .xlings.json with workspace
    fs::path cur = cwd;
    while (!cur.empty()) {
        auto curNorm = fs::weakly_canonical(cur, ec);
        auto homeNorm = fs::weakly_canonical(homeDir, ec);
        if (curNorm == homeNorm) {
            auto parent = cur.parent_path();
            if (parent == cur) break;
            cur = parent;
            continue;
        }

        // Try .xlings.json first
        auto cfg = cur / ".xlings.json";
        if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
            try {
                auto content = platform::read_file_to_string(cfg.string());
                auto json = nlohmann::json::parse(content, nullptr, false);
                if (!json.is_discarded() && json.contains("workspace") && json["workspace"].is_object()) {
                    auto workspace = xvm::workspace_from_json(json["workspace"]);
                    auto targets = Config::workspace_install_targets(workspace);
                    if (!targets.empty()) {
                        return xim::cmd_install(targets, true, false);
                    }
                }
            } catch (...) {
                log::error("failed to parse {}", cfg.string());
                return 1;
            }
        }

        // Fallback: try legacy config.xlings (Lua format)
        auto legacyCfg = cur / "config.xlings";
        if (fs::exists(legacyCfg, ec) && fs::is_regular_file(legacyCfg, ec)) {
            auto workspace = parse_legacy_config_(legacyCfg);
            if (!workspace.empty()) {
                std::println("detected legacy config: {}", legacyCfg.string());
                std::println("generating .xlings.json from config.xlings ...");
                generate_xlings_json_(cur, workspace);
                std::println("generated: {}", (cur / ".xlings.json").string());
                auto targets = Config::workspace_install_targets(workspace);
                return xim::cmd_install(targets, true, false);
            }
        }

        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    std::println("Tip: create <project>/.xlings.json with workspace, or run `xlings install <package>`");
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
            std::println("    {}  {}", pad("add-xpkg", 12), "Add xpkg file to package index");
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

        if (cmd == "--add-xpkg" && argc >= 3) {
            return xim::cmd_add_xpkg(std::string(argv[2]));
        }

        if (cmd == "subos") return subos::run(argc, argv);
        if (cmd == "self") return xself::run(argc, argv);
        if (cmd == "script") {
            if (argc < 3) {
                std::println("Usage: xlings script <script-file> [args...]");
                return 1;
            }
            namespace fs = std::filesystem;
            fs::path scriptFile = argv[2];
            auto execResult = mcpplibs::xpkg::create_executor(scriptFile);
            if (!execResult) {
                log::error("failed to load script: {}", execResult.error());
                return 1;
            }
            mcpplibs::xpkg::ExecutionContext ctx;
            ctx.platform = std::string(platform::OS_NAME);
            ctx.bin_dir = Config::paths().binDir;
            ctx.subos_sysrootdir = Config::paths().subosDir.string();
            ctx.run_dir = fs::current_path();
            ctx.xpkg_dir = scriptFile.parent_path();
            for (int i = 3; i < argc; ++i) {
                ctx.args.emplace_back(argv[i]);
            }
            auto result = execResult->run_script(ctx);
            if (!result.success) {
                log::error("script error: {}", result.error);
                return 1;
            }
            return 0;
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

        // add-xpkg
        .subcommand("add-xpkg")
            .description("Add xpkg file to package index")
            .arg("file").required().help("Path or URL to .lua xpkg file")
            .action([](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_add_xpkg(std::string(args.positional(0)));
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
