export module xlings.cli;

import std;

import mcpplibs.cmdline;
import mcpplibs.capi.lua;
import mcpplibs.xpkg.executor;
import xlings.core.config;
import xlings.libs.json;
import xlings.core.log;
import xlings.runtime;
import xlings.ui;
import xlings.core.i18n;
import xlings.platform;
import xlings.capabilities;
import xlings.core.subos;
import xlings.core.xself;
import xlings.core.xim.commands;
import xlings.core.xvm.types;
import xlings.core.xvm.db;
import xlings.core.xvm.commands;
import xlings.agent;
import xlings.agent.commands;
import xlings.core.utf8;
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.libs.agentfs;
import xlings.libs.soul;
import xlings.libs.journal;
import xlings.libs.tinytui;
import mcpplibs.llmapi;

namespace lua = mcpplibs::capi::lua;

namespace xlings::cli {

// ─── EventStream consumer: dispatch DataEvent to ui:: functions ───

static void dispatch_data_event(const DataEvent& e) {
    auto json = nlohmann::json::parse(e.json, nullptr, false);
    if (json.is_discarded()) {
        log::warn("invalid JSON in DataEvent kind={}", e.kind);
        return;
    }

    if (e.kind == "info_panel") {
        std::string title = json.value("title", "");
        std::vector<ui::InfoField> fields;
        if (json.contains("fields") && json["fields"].is_array()) {
            for (auto& f : json["fields"]) {
                fields.push_back({
                    f.value("label", ""),
                    f.value("value", ""),
                    f.value("highlight", false)
                });
            }
        }
        std::vector<ui::InfoField> extra;
        if (json.contains("extra_fields") && json["extra_fields"].is_array()) {
            for (auto& f : json["extra_fields"]) {
                extra.push_back({
                    f.value("label", ""),
                    f.value("value", ""),
                    f.value("highlight", false)
                });
            }
        }
        ui::print_info_panel(title, fields, extra);
    }
    else if (e.kind == "help") {
        std::string name = json.value("name", "");
        std::string desc = json.value("description", "");
        std::vector<ui::HelpArg> args;
        if (json.contains("args") && json["args"].is_array()) {
            for (auto& a : json["args"]) {
                args.push_back({
                    a.value("name", ""),
                    a.value("desc", ""),
                    a.value("required", false)
                });
            }
        }
        std::vector<ui::HelpOpt> opts;
        if (json.contains("opts") && json["opts"].is_array()) {
            for (auto& o : json["opts"]) {
                opts.push_back({
                    o.value("name", ""),
                    o.value("desc", "")
                });
            }
        }
        ui::print_subcommand_help(name, desc, args, opts);
    }
    else if (e.kind == "styled_list") {
        std::string title = json.value("title", "");
        bool numbered = json.value("numbered", false);
        std::vector<std::pair<std::string, std::string>> items;
        if (json.contains("items") && json["items"].is_array()) {
            for (auto& item : json["items"]) {
                if (item.is_array() && item.size() >= 2) {
                    items.emplace_back(item[0].get<std::string>(), item[1].get<std::string>());
                } else if (item.is_string()) {
                    items.emplace_back(item.get<std::string>(), "");
                }
            }
        }
        ui::print_styled_list(title, items, numbered);
    }
    else if (e.kind == "install_plan") {
        std::vector<std::pair<std::string, std::string>> packages;
        if (json.contains("packages") && json["packages"].is_array()) {
            for (auto& p : json["packages"]) {
                if (p.is_array() && p.size() >= 2) {
                    packages.emplace_back(p[0].get<std::string>(), p[1].get<std::string>());
                }
            }
        }
        ui::print_install_plan(packages);
    }
    else if (e.kind == "install_summary") {
        ui::print_install_summary(json.value("success", 0), json.value("failed", 0));
    }
    else if (e.kind == "remove_summary") {
        ui::print_remove_summary(json.value("target", ""));
    }
    else if (e.kind == "subos_list") {
        std::vector<std::tuple<std::string, std::string, int, bool>> entries;
        if (json.contains("entries") && json["entries"].is_array()) {
            for (auto& e : json["entries"]) {
                entries.emplace_back(
                    e.value("name", ""),
                    e.value("dir", ""),
                    e.value("pkgCount", 0),
                    e.value("active", false)
                );
            }
        }
        ui::print_subos_list(entries);
    }
    else if (e.kind == "search_results") {
        std::vector<std::pair<std::string, std::string>> results;
        if (json.contains("results") && json["results"].is_array()) {
            for (auto& r : json["results"]) {
                if (r.is_array() && r.size() >= 2) {
                    results.emplace_back(r[0].get<std::string>(), r[1].get<std::string>());
                }
            }
        }
        ui::print_search_results(results);
    }
    else if (e.kind == "table") {
        std::vector<std::string> headers;
        if (json.contains("headers") && json["headers"].is_array()) {
            for (auto& h : json["headers"]) {
                headers.push_back(h.get<std::string>());
            }
        }
        std::vector<std::vector<std::string>> rows;
        if (json.contains("rows") && json["rows"].is_array()) {
            for (auto& r : json["rows"]) {
                std::vector<std::string> row;
                if (r.is_array()) {
                    for (auto& c : r) row.push_back(c.get<std::string>());
                }
                rows.push_back(std::move(row));
            }
        }
        ui::print_table(headers, rows);
    }
    else if (e.kind == "download_progress") {
        auto nameWidth = json.value("nameWidth", std::size_t{20});
        auto elapsedSec = json.value("elapsedSec", 0.0);
        auto sizesReady = json.value("sizesReady", false);
        std::vector<ui::DownloadProgressEntry> entries;
        if (json.contains("files") && json["files"].is_array()) {
            for (auto& f : json["files"]) {
                entries.push_back({
                    f.value("name", std::string{}),
                    f.value("totalBytes", 0.0),
                    f.value("downloadedBytes", 0.0),
                    f.value("started", false),
                    f.value("finished", false),
                    f.value("success", false)
                });
            }
        }
        auto prevLines = json.value("prevLines", 0);
        ui::render_download_progress(entries, nameWidth, elapsedSec, sizesReady, prevLines);
    }
    else {
        log::debug("unhandled DataEvent kind: {}", e.kind);
    }
}

// ─── EventStream consumer: handle PromptEvent via ui:: interactive functions ───

static void handle_prompt(EventStream& stream, const PromptEvent& p) {
    // Binary yes/no → confirm dialog
    if (p.options.size() == 2 && p.options[0] == "y" && p.options[1] == "n") {
        bool defaultYes = (p.defaultValue == "y");
        bool result = ui::confirm(p.question, defaultYes);
        stream.respond(p.id, result ? "y" : "n");
        return;
    }

    // Multiple options → package/version selector
    if (!p.options.empty()) {
        std::vector<std::pair<std::string, std::string>> items;
        for (auto& opt : p.options) {
            items.emplace_back(opt, "");
        }
        auto result = ui::select_package(items);
        stream.respond(p.id, result.value_or(""));
        return;
    }

    // Free input — use default
    stream.respond(p.id, p.defaultValue);
}

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
int install_from_project_config_(EventStream& stream) {
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
                        return xim::cmd_install(targets, true, false, stream);
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
                log::println("detected legacy config: {}", legacyCfg.string());
                log::println("generating .xlings.json from config.xlings ...");
                generate_xlings_json_(cur, workspace);
                log::println("generated: {}", (cur / ".xlings.json").string());
                auto targets = Config::workspace_install_targets(workspace);
                return xim::cmd_install(targets, true, false, stream);
            }
        }

        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }

    ui::print_tip("create <project>/.xlings.json with workspace, or run `xlings install <package>`");
    return 0;
}

void apply_global_opts_(const mcpplibs::cmdline::ParsedArgs& args) {
    if (args.is_flag_set("verbose")) log::set_level(log::Level::Debug);
    if (args.is_flag_set("quiet")) log::set_level(log::Level::Error);
}

// Read-modify-write the global .xlings.json config file
nlohmann::json load_global_config_json_() {
    auto configPath = Config::paths().homeDir / ".xlings.json";
    if (std::filesystem::exists(configPath)) {
        try {
            auto content = platform::read_file_to_string(configPath.string());
            auto json = nlohmann::json::parse(content, nullptr, false);
            if (!json.is_discarded()) return json;
        } catch (...) {}
    }
    return nlohmann::json::object();
}

void save_global_config_json_(const nlohmann::json& json) {
    auto configPath = Config::paths().homeDir / ".xlings.json";
    platform::write_string_to_file(configPath.string(), json.dump(2));
}

// config subcommand handler
int cmd_config_(const mcpplibs::cmdline::ParsedArgs& args, EventStream& stream) {
    bool changed = false;
    auto json = load_global_config_json_();

    // --lang
    if (auto lang = args.value("lang")) {
        json["lang"] = std::string(*lang);
        log::println("lang = {}", *lang);
        changed = true;
    }

    // --mirror
    if (auto mirror = args.value("mirror")) {
        json["mirror"] = std::string(*mirror);
        log::println("mirror = {}", *mirror);
        changed = true;
    }

    // --add-xpkg
    if (auto xpkg = args.value("add-xpkg")) {
        if (changed) save_global_config_json_(json);
        return xim::cmd_add_xpkg(std::string(*xpkg), stream);
    }

    // --index-repo  namespace:https://....git
    if (auto repo = args.value("index-repo")) {
        std::string val(*repo);
        auto colonPos = val.find(':');
        // Find the colon that separates name from URL (skip scheme "https://")
        // Format: name:url  e.g. myrepo:https://github.com/user/repo.git
        if (colonPos == std::string::npos || colonPos == 0) {
            log::error("invalid format, expected: <namespace>:<url>");
            log::error("  e.g. myrepo:https://github.com/user/repo.git");
            return 1;
        }
        auto name = val.substr(0, colonPos);
        auto url = val.substr(colonPos + 1);
        if (url.empty()) {
            log::error("invalid format, expected: <namespace>:<url>");
            return 1;
        }

        if (!json.contains("index_repos") || !json["index_repos"].is_array()) {
            json["index_repos"] = nlohmann::json::array();
        }
        // Check for existing repo with same name and update
        bool found = false;
        for (auto& entry : json["index_repos"]) {
            if (entry.is_object() && entry.contains("name") &&
                entry["name"].get<std::string>() == name) {
                entry["url"] = url;
                found = true;
                break;
            }
        }
        if (!found) {
            nlohmann::json entry;
            entry["name"] = name;
            entry["url"] = url;
            json["index_repos"].push_back(entry);
        }
        log::println("index-repo {} = {}", name, url);
        changed = true;
    }

    if (changed) {
        save_global_config_json_(json);
        return 0;
    }

    // No options: show current config via TUI info panel
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

export int run(int argc, char* argv[]) {
    using namespace mcpplibs;

    // Create EventStream for core→UI decoupling
    EventStream stream;
    // Default TUI consumer for CLI mode (will be toggled in agent mode)
    int tui_listener = stream.on_event([&stream](const Event& e) {
        if (auto* d = std::get_if<DataEvent>(&e)) {
            dispatch_data_event(*d);
        }
        if (auto* p = std::get_if<PromptEvent>(&e)) {
            handle_prompt(stream, *p);
        }
    });

    // Build Capability Registry (for Agent/MCP use — CLI keeps direct dispatch)
    auto registry = capabilities::build_registry();

    // Scan for global flags (--verbose, -v, --quiet, -q) anywhere in argv
    // so they work regardless of position (e.g. `xlings --verbose subos new s1`)
    for (int i = 1; i < argc; ++i) {
        std::string_view a { argv[i] };
        if (a == "--verbose" || a == "-v") log::set_level(log::Level::Debug);
        else if (a == "--quiet" || a == "-q") log::set_level(log::Level::Error);
    }

    // Build filtered argv without global flags for positional-arg handlers
    // (subos, self, script parse argv[2]/argv[3] positionally)
    std::vector<char*> fargv;
    fargv.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string_view a { argv[i] };
        if (a == "--verbose" || a == "-v" || a == "--quiet" || a == "-q") continue;
        fargv.push_back(argv[i]);
    }
    int fargc = static_cast<int>(fargv.size());

    // Find the first non-flag argument as the command name
    std::string cmd;
    for (int i = 1; i < fargc; ++i) {
        std::string_view a { fargv[i] };
        if (!a.starts_with("-")) { cmd = std::string(a); break; }
    }

    // Special: subos, self, script need raw argc/argv
    if (fargc >= 2) {
        // Handle -h/--help/--version before cmdline library to avoid
        // std::format width-specifier crash in GCC 15 C++23 modules.
        if (cmd == "-h" || cmd == "--help" || cmd.empty()) {
            if (cmd.empty()) {
                // Only flags, no command — check if -h was requested
                for (int i = 1; i < fargc; ++i) {
                    std::string_view a { fargv[i] };
                    if (a == "-h" || a == "--help") { ui::print_help(Info::VERSION); return 0; }
                    if (a == "--version") { std::println("xlings {}", Info::VERSION); return 0; }
                }
            }
            ui::print_help(Info::VERSION);
            return 0;
        }
        if (cmd == "--version") {
            std::println("xlings {}", Info::VERSION);
            return 0;
        }

        // Intercept subcommand help: xlings <cmd> -h/--help
        bool wantsHelp = false;
        for (int i = 2; i < fargc; ++i) {
            std::string_view a { fargv[i] };
            if (a == "-h" || a == "--help") { wantsHelp = true; break; }
        }
        if (wantsHelp) {
            using A = ui::HelpArg;
            using O = ui::HelpOpt;

            struct SubHelp {
                std::string_view name;
                std::string_view desc;
                std::vector<A> args;
                std::vector<O> opts;
                std::vector<O> subcmds;
            };

            auto match = [&](std::string_view n) { return cmd == n; };

            std::optional<SubHelp> h;
            if (match("install")) h = SubHelp{
                "install", "Install packages (e.g. xlings install gcc@15 node)",
                { {"packages", "Package names with optional version"} },
                { {"-g, --global", "Install to global scope (not project-local subos)"} },
            };
            else if (match("remove")) h = SubHelp{
                "remove", "Remove a package",
                { {"package", "Package to remove", true} }, {},
            };
            else if (match("update")) h = SubHelp{
                "update", "Update package index or a specific package",
                { {"package", "Package to update (omit for index only)"} }, {},
            };
            else if (match("search")) h = SubHelp{
                "search", "Search for packages",
                { {"keyword", "Search keyword", true} }, {},
            };
            else if (match("list")) h = SubHelp{
                "list", "List installed packages",
                { {"filter", "Filter pattern"} }, {},
            };
            else if (match("info")) h = SubHelp{
                "info", "Show package information",
                { {"package", "Package name", true} }, {},
            };
            else if (match("use")) h = SubHelp{
                "use", "Switch tool version",
                { {"target", "Tool name", true}, {"version", "Version to switch to (omit to list)"} }, {},
            };
            else if (match("config")) h = SubHelp{
                "config", "Show or modify xlings configuration", {},
                {
                    {"--lang <LANG>",       "Set language (en/zh)"},
                    {"--mirror <MIRROR>",   "Set mirror (GLOBAL/CN)"},
                    {"--add-xpkg <FILE>",   "Add xpkg file to package index"},
                    {"--index-repo <NS:URL>", "Add/update index repo (e.g. myns:https://...git)"},
                },
            };
            else if (match("self")) h = SubHelp{
                "self", "Manage xlings itself", {},
                {
                    {"install",  "Install xlings from release package"},
                    {"init",     "Create home/data/subos dirs"},
                    {"update",   "Update index + install latest xlings"},
                    {"config",   "Show configuration details"},
                    {"clean",    "Remove cache + gc orphaned packages (--dry-run)"},
                    {"migrate",  "Migrate old layout to subos/default"},
                },
            };
            else if (match("subos")) h = SubHelp{
                "subos", "Manage sub-OS environments", {},
                {
                    {"new <name>",    "Create a new sub-OS"},
                    {"use <name>",    "Switch active sub-OS"},
                    {"list",          "List all sub-OS environments"},
                    {"remove <name>", "Remove a sub-OS"},
                    {"info [name]",   "Show sub-OS details"},
                },
            };
            else if (match("script")) h = SubHelp{
                "script", "Run xlings scripts",
                { {"script-file", "Path to script file", true}, {"args", "Script arguments"} }, {},
            };
            else if (match("agent")) h = SubHelp{
                "agent", "AI agent for interactive package management", {},
                {
                    {"--model <MODEL>",      "LLM model name (default: claude-sonnet-4-6)"},
                    {"--base-url <URL>",     "Custom API base URL"},
                    {"--profile <NAME>",     "LLM config profile from .agents/config/llm.json"},
                    {"-y, --auto-approve",   "Skip confirmation prompts for tool calls"},
                },
                {
                    {"chat",                 "Start interactive chat (default)"},
                    {"resume [ID]",          "Resume a saved session (latest if no ID)"},
                    {"sessions",             "List saved sessions"},
                    {"config [reset]",       "Show agent config, or reset to reconfigure"},
                    {"mcp",                  "List configured MCP servers"},
                },
            };

            if (h) {
                ui::print_subcommand_help(h->name, h->desc, h->args, h->opts, h->subcmds);
                return 0;
            }
        }

        // Detect unknown commands early — show TUI error + help
        {
            static constexpr std::string_view known_cmds[] = {
                "install", "remove", "update", "search", "list",
                "info", "use", "config", "subos", "self", "script", "agent",
            };
            bool known = false;
            for (auto& k : known_cmds) {
                if (cmd == k) { known = true; break; }
            }
            if (!known && !cmd.starts_with("-")) {
                log::error("unknown command: {}", cmd);
                ui::print_help(Info::VERSION);
                return 1;
            }
        }

        if (cmd == "subos") return subos::run(fargc, fargv.data(), stream);
        if (cmd == "self") return xself::run(fargc, fargv.data(), stream);
        if (cmd == "agent") {
            // --- Parse sub-subcommand and flags ---
            std::string subcmd = "chat"; // default
            std::string flag_model;
            std::string flag_base_url;
            std::string flag_profile;
            std::string subcmd_arg; // second positional (session ID for resume, "reset" for config)
            bool flag_auto_approve = false;

            // First non-flag arg after "agent" is the sub-subcommand
            bool subcmd_parsed = false;
            for (int i = 2; i < fargc; ++i) {
                std::string_view a{fargv[i]};
                if (a == "--model" && i + 1 < fargc)       { flag_model = fargv[++i]; continue; }
                if (a == "--base-url" && i + 1 < fargc)    { flag_base_url = fargv[++i]; continue; }
                if (a == "--profile" && i + 1 < fargc)     { flag_profile = fargv[++i]; continue; }
                if (a == "-y" || a == "--auto-approve")     { flag_auto_approve = true; continue; }
                if (!subcmd_parsed) {
                    subcmd = std::string(a);
                    subcmd_parsed = true;
                    continue;
                }
                // Second positional arg
                if (subcmd_arg.empty()) {
                    subcmd_arg = std::string(a);
                    continue;
                }
            }

            // Common init: .agents/ filesystem
            namespace agentfs = libs::agentfs;
            namespace soul_ns = libs::soul;
            namespace journal_ns = libs::journal;

            auto agents_root = Config::paths().homeDir / ".agents";
            agentfs::AgentFS afs(agents_root);
            afs.ensure_initialized();

            // ---- sub-subcommand: sessions ----
            if (subcmd == "sessions") {
                agent::SessionManager session_mgr(afs);
                auto sessions = session_mgr.list();
                if (sessions.empty()) {
                    agent::tui::print_hint("no saved sessions");
                } else {
                    auto pad = [](std::string s, std::size_t w) {
                        while (s.size() < w) s += ' '; return s;
                    };
                    std::println("  {} {} {} {}", pad("ID", 20), pad("MODEL", 20), pad("TITLE", 24), "TURNS");
                    std::println("  {}", std::string(75, '-'));
                    for (auto& s : sessions) {
                        std::println("  {} {} {} {}",
                            pad(s.id, 20), pad(s.model, 20), pad(s.title, 24),
                            std::to_string(s.turn_count));
                    }
                }
                return 0;
            }

            // ---- sub-subcommand: config [reset] ----
            if (subcmd == "config") {
                auto llm_path = afs.llm_config_path();

                // config reset → delete llm.json and re-enter setup wizard
                if (subcmd_arg == "reset") {
                    namespace fs = std::filesystem;
                    std::error_code ec;
                    fs::remove(llm_path, ec);
                    agent::tui::print_hint("Configuration reset. Run 'xlings agent' to reconfigure.");
                    return 0;
                }

                // Show config using TUI info panels
                auto llm_j = agentfs::AgentFS::read_json(llm_path);

                // LLM panel
                std::vector<ui::InfoField> llm_fields;
                if (llm_j.is_null()) {
                    llm_fields.push_back({"status", "not configured (run 'xlings agent')"});
                } else {
                    if (llm_j.contains("default_profile"))
                        llm_fields.push_back({"default", llm_j["default_profile"].get<std::string>(), true});
                    if (llm_j.contains("default") && llm_j["default"].contains("model"))
                        llm_fields.push_back({"model", llm_j["default"]["model"].get<std::string>()});
                    if (llm_j.contains("default") && llm_j["default"].contains("provider"))
                        llm_fields.push_back({"api format", llm_j["default"]["provider"].get<std::string>()});
                    if (llm_j.contains("default") && llm_j["default"].contains("base_url"))
                        llm_fields.push_back({"base_url", llm_j["default"]["base_url"].get<std::string>()});
                    llm_fields.push_back({"config file", llm_path.string()});
                }

                // Profiles as extra fields
                std::vector<ui::InfoField> profile_fields;
                if (!llm_j.is_null() && llm_j.contains("profiles") && llm_j["profiles"].is_object()) {
                    for (auto it = llm_j["profiles"].begin(); it != llm_j["profiles"].end(); ++it) {
                        auto m = it.value().value("model", "?");
                        auto p = it.value().value("provider", "?");
                        auto has_key = it.value().contains("api_key") && !it.value()["api_key"].get<std::string>().empty();
                        std::string val = m + " (" + p + ") key=" + (has_key ? "set" : "not set");
                        profile_fields.push_back({it.key(), val});
                    }
                }
                ui::print_info_panel("LLM Configuration", llm_fields, profile_fields);

                // Soul panel
                soul_ns::SoulManager soul_mgr(afs);
                auto soul = soul_mgr.load_or_create();
                std::vector<ui::InfoField> soul_fields;
                soul_fields.push_back({"persona", soul.persona});
                soul_fields.push_back({"trust_level", soul.trust_level, true});
                if (!soul.allowed_capabilities.empty()) {
                    std::string caps;
                    for (auto& c : soul.allowed_capabilities) { if (!caps.empty()) caps += ", "; caps += c; }
                    soul_fields.push_back({"allowed", caps});
                }
                if (!soul.denied_capabilities.empty()) {
                    std::string caps;
                    for (auto& c : soul.denied_capabilities) { if (!caps.empty()) caps += ", "; caps += c; }
                    soul_fields.push_back({"denied", caps});
                }
                ui::print_info_panel("Soul", soul_fields);

                // Environment panel
                auto ak = std::getenv("ANTHROPIC_API_KEY");
                auto ok = std::getenv("OPENAI_API_KEY");
                auto dk = std::getenv("DEEPSEEK_API_KEY");
                auto mk = std::getenv("MINIMAX_API_KEY");
                std::vector<ui::InfoField> env_fields;
                env_fields.push_back({"ANTHROPIC_API_KEY", ak ? "set" : "(not set)", ak != nullptr});
                env_fields.push_back({"OPENAI_API_KEY", ok ? "set" : "(not set)", ok != nullptr});
                env_fields.push_back({"DEEPSEEK_API_KEY", dk ? "set" : "(not set)", dk != nullptr});
                env_fields.push_back({"MINIMAX_API_KEY", mk ? "set" : "(not set)", mk != nullptr});
                ui::print_info_panel("Environment", env_fields);

                agent::tui::print_hint("Run 'xlings agent config reset' to reconfigure");
                return 0;
            }

            // ---- sub-subcommand: mcp ----
            if (subcmd == "mcp") {
                auto mcps_dir = afs.root() / "mcps";
                auto configs = agent::mcp::load_mcp_configs(mcps_dir);
                if (configs.empty()) {
                    agent::tui::print_hint("no MCP servers configured");
                    agent::tui::print_hint("add JSON configs to: " + mcps_dir.string());
                } else {
                    std::vector<ui::InfoField> mcp_fields;
                    for (auto& c : configs) {
                        std::string val = c.command;
                        for (auto& a : c.args) { val += " " + a; }
                        mcp_fields.push_back({c.name, val});
                    }
                    ui::print_info_panel("MCP Servers", mcp_fields);
                }
                return 0;
            }

            // ---- sub-subcommands that need LLM: chat / resume ----
            if (subcmd != "chat" && subcmd != "resume") {
                agent::tui::print_error("unknown agent subcommand: " + subcmd);
                agent::tui::print_hint("available: chat, resume, sessions, config, mcp");
                return 1;
            }

            // First-run setup: if no llm.json exists, launch interactive wizard
            auto llm_path = afs.llm_config_path();
            if (agent::needs_setup(llm_path)) {
                auto presets = agent::provider_presets();

                nlohmann::json profiles = nlohmann::json::object();
                std::string default_profile;

                // Loop: let user configure providers via fullscreen menus
                while (true) {
                    // Build dynamic menu: unconfigured providers + "Done"
                    std::vector<std::pair<std::string, std::string>> available;
                    std::vector<int> index_map;
                    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                        if (!profiles.contains(presets[i].id)) {
                            available.emplace_back(presets[i].name, presets[i].default_model);
                            index_map.push_back(i);
                        }
                    }

                    if (available.empty()) break;

                    std::string done_label = profiles.empty() ? "" : "Done - finish setup";
                    auto sel = ui::select_option(
                        "xlings agent - Select provider to configure", available, done_label);

                    if (!sel) break;
                    if (*sel == -1) break;

                    int preset_idx = index_map[*sel];
                    auto& preset = presets[preset_idx];

                    // API format selection (if multiple formats)
                    std::string provider_type;
                    std::string base_url;
                    if (preset.formats.size() == 1) {
                        provider_type = preset.formats[0].name;
                        base_url = preset.formats[0].base_url;
                    } else {
                        std::vector<std::pair<std::string, std::string>> fmt_items;
                        for (auto& f : preset.formats) {
                            std::string desc = f.base_url.empty() ? "(default)" : f.base_url;
                            fmt_items.emplace_back(f.name + " compatible", desc);
                        }
                        auto fmt_sel = ui::select_option(
                            preset.name + " - Select API format", fmt_items);
                        if (!fmt_sel || *fmt_sel < 0) continue;
                        provider_type = preset.formats[*fmt_sel].name;
                        base_url = preset.formats[*fmt_sel].base_url;
                    }

                    // Model selection via menu (default model as first + custom option)
                    std::string model = preset.default_model;
                    {
                        std::vector<std::pair<std::string, std::string>> model_items;
                        model_items.emplace_back(preset.default_model, "(default)");
                        model_items.emplace_back("Custom...", "enter model name manually");
                        auto m_sel = ui::select_option(
                            preset.name + " - Select model", model_items);
                        if (m_sel && *m_sel == 1) {
                            // Custom model: clear screen, prompt text input
                            platform::clear_console();
                            auto custom = ui::read_line("Enter model name for " + preset.name + ":");
                            if (!custom.empty()) model = custom;
                        }
                    }

                    // API key: clear screen for text input
                    platform::clear_console();
                    std::string api_key;
                    auto* env_key = std::getenv(preset.env_var.c_str());
                    if (env_key && std::string_view(env_key).size() > 0) {
                        std::println("\033[38;2;34;197;94m  \xe2\x9c\x93 found {} in environment\033[0m", preset.env_var);
                        api_key = env_key;
                    } else {
                        api_key = ui::read_line("Enter API key for " + preset.name + ":");
                        if (api_key.empty()) {
                            std::println("\033[38;2;245;158;11m  Skipped (no key provided)\033[0m");
                            continue;
                        }
                    }

                    // Save to profiles
                    nlohmann::json pj;
                    pj["model"] = model;
                    pj["api_key"] = api_key;
                    pj["provider"] = provider_type;
                    if (!base_url.empty()) pj["base_url"] = base_url;
                    profiles[preset.id] = pj;

                    if (default_profile.empty()) default_profile = preset.id;

                    // First-run: one provider configured is enough, enter chat
                    break;
                }

                if (profiles.empty()) {
                    agent::tui::print_hint("No providers configured. Setup cancelled.");
                    return 0;
                }

                // If multiple profiles, choose default via centered menu
                if (profiles.size() > 1) {
                    std::vector<std::pair<std::string, std::string>> def_items;
                    std::vector<std::string> pnames;
                    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                        pnames.push_back(it.key());
                        auto m = it.value().value("model", "");
                        def_items.emplace_back(it.key(), m);
                    }
                    auto def_sel = ui::select_option("Select default provider", def_items);
                    if (def_sel && *def_sel >= 0 && *def_sel < static_cast<int>(pnames.size())) {
                        default_profile = pnames[*def_sel];
                    }
                }

                agent::save_setup_config(llm_path, profiles, default_profile);
                // Clear screen before entering chat
                platform::clear_console();
                std::println("\033[38;2;34;197;94m  \xe2\x9c\x93 Configuration saved\033[0m");
                agent::tui::print_hint("Default provider: " + default_profile);
                std::println("");
            }

            // Load soul + resolve LLM config
            soul_ns::SoulManager soul_mgr(afs);
            auto soul = soul_mgr.load_or_create();
            auto cfg = agent::resolve_llm_config(
                flag_model, flag_base_url, llm_path, flag_profile
            );

            if (cfg.api_key.empty()) {
                agent::tui::print_error("API key not set. Check your config with: xlings agent config");
                return 1;
            }

            // Journal + session
            journal_ns::Journal journal(afs);
            agent::SessionManager session_mgr(afs);
            agent::SessionMeta session_meta;
            mcpplibs::llmapi::Conversation conversation;

            if (subcmd == "resume") {
                // Resume: find session by ID or use latest
                if (subcmd_arg.empty()) {
                    auto sessions = session_mgr.list();
                    if (sessions.empty()) {
                        agent::tui::print_error("no sessions to resume");
                        return 1;
                    }
                    subcmd_arg = sessions.front().id; // latest
                }
                auto meta = session_mgr.load_meta(subcmd_arg);
                if (!meta) {
                    agent::tui::print_error("session not found: " + subcmd_arg);
                    return 1;
                }
                session_meta = *meta;
                conversation = session_mgr.load_conversation(subcmd_arg);
                agent::tui::print_hint("resumed session: " + session_meta.title +
                    " (" + std::to_string(conversation.size()) + " messages)");
            } else {
                session_meta = session_mgr.create(cfg.model);
            }

            // Approval policy
            agent::ApprovalPolicy approval(soul);
            agent::ApprovalPolicy* policy_ptr = flag_auto_approve ? nullptr : &approval;

            auto bridge = agent::ToolBridge(registry);
            auto system_prompt = agent::build_system_prompt(bridge);
            auto tools = agent::to_llmapi_tools(bridge);

            // ─── Agent mode: dual consumer architecture ───
            // Consumer 1: Agent data capture (always active) — captures DataEvents for LLM
            int agent_listener = stream.on_event([&bridge](const Event& e) {
                if (auto* d = std::get_if<DataEvent>(&e)) {
                    bridge.on_data_event(*d);
                }
            });

            // Consumer 2: CLI TUI rendering — disabled in agent mode (tinytui owns terminal)
            stream.set_enabled(tui_listener, false);

            // Token tracker + context manager
            agent::TokenTracker tracker;
            agent::ContextManager ctx_mgr(cfg.model);
            auto cache_dir = afs.sessions_path() / session_meta.id / "context_cache";
            ctx_mgr.set_cache_dir(cache_dir);

            // ─── tinytui screen + line editor ───
            tinytui::Screen screen;
            tinytui::LineEditor editor;

            agent::tui::AgentTuiState tui_state;
            tui_state.model_name = cfg.model;
            tui_state.ctx_limit = agent::TokenTracker::context_limit(cfg.model);

            // ─── Consumer 3: data event listener ───
            // Handles download progress + data display in tree
            auto format_speed = [](double bps) -> std::string {
                if (bps < 1024.0)
                    return std::to_string(static_cast<int>(bps)) + " B/s";
                if (bps < 1024.0 * 1024.0) {
                    int kb = static_cast<int>(bps / 1024.0 * 10.0);
                    return std::to_string(kb / 10) + "." + std::to_string(kb % 10) + " KB/s";
                }
                int mb = static_cast<int>(bps / (1024.0 * 1024.0) * 10.0);
                return std::to_string(mb / 10) + "." + std::to_string(mb % 10) + " MB/s";
            };

            int data_listener = stream.on_event([&](const Event& e) {
                if (auto* d = std::get_if<DataEvent>(&e)) {
                    // Download progress → update download node in active tree
                    if (d->kind == "download_progress") {
                        auto j = nlohmann::json::parse(d->json, nullptr, false);
                        if (j.is_discarded()) return;

                        double total_bytes = 0, downloaded_bytes = 0;
                        std::string active_name;
                        if (j.contains("files") && j["files"].is_array()) {
                            for (auto it = j["files"].begin(); it != j["files"].end(); ++it) {
                                auto& f = *it;
                                total_bytes += f.value("totalBytes", 0.0);
                                downloaded_bytes += f.value("downloadedBytes", 0.0);
                                if (f.value("started", false) && !f.value("finished", false)) {
                                    active_name = f.value("name", "");
                                }
                            }
                        }
                        float pct = (total_bytes > 0)
                            ? static_cast<float>(downloaded_bytes / total_bytes) : 0.0f;
                        if (pct > 1.0f) pct = 1.0f;

                        double elapsed_sec = j.value("elapsedSec", 0.0);
                        std::string speed, eta_str;
                        if (elapsed_sec > 0.5 && downloaded_bytes > 0) {
                            double bps = downloaded_bytes / elapsed_sec;
                            speed = format_speed(bps);
                            if (pct > 0.01f && pct < 1.0f && bps > 0) {
                                double remain = (total_bytes - downloaded_bytes) / bps;
                                int secs = static_cast<int>(remain);
                                if (secs < 60) eta_str = "ETA " + std::to_string(secs) + "s";
                                else eta_str = "ETA " + std::to_string(secs / 60) + "m"
                                    + std::to_string(secs % 60) + "s";
                            }
                        }

                        screen.post([&, pct, name = std::move(active_name),
                                     spd = std::move(speed), et = std::move(eta_str)] {
                            // Update download node in active tree (under active parent's last tool call)
                            if (tui_state.active_turn) {
                                auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                if (!parent->children.empty()) {
                                    auto& tool_node = parent->children.back();
                                    bool found_dl = false;
                                    for (auto& child : tool_node.children) {
                                        if (child.kind == agent::tui::TreeNode::Download) {
                                            child.progress = pct;
                                            child.title = name.empty() ? "downloading..." : name;
                                            child.speed = spd;
                                            child.eta = et;
                                            found_dl = true;
                                            break;
                                        }
                                    }
                                    if (!found_dl) {
                                        tool_node.children.push_back({
                                            .kind = agent::tui::TreeNode::Download,
                                            .state = agent::tui::TreeNode::Running,
                                            .title = name.empty() ? "downloading..." : name,
                                            .start_ms = agent::tui::steady_now_ms(),
                                            .progress = pct,
                                            .speed = spd,
                                            .eta = et,
                                        });
                                    }
                                    if (pct >= 0.99f) {
                                        for (auto& child : tool_node.children) {
                                            if (child.kind == agent::tui::TreeNode::Download &&
                                                child.state == agent::tui::TreeNode::Running) {
                                                child.state = agent::tui::TreeNode::Done;
                                                child.end_ms = agent::tui::steady_now_ms();
                                                child.progress = 1.0f;
                                            }
                                        }
                                    }
                                }
                            }
                        });
                        screen.refresh();
                        return;
                    }

                    // Other data events → accumulate as detail text
                    {
                        std::string summary;
                        if (d->kind == "styled_list" || d->kind == "search_results") {
                            auto j = nlohmann::json::parse(d->json, nullptr, false);
                            if (!j.is_discarded()) {
                                summary = "[" + d->kind + "] " + j.value("title", "");
                            }
                        } else if (d->kind == "install_summary") {
                            auto j = nlohmann::json::parse(d->json, nullptr, false);
                            if (!j.is_discarded()) {
                                summary = "[install] success:" +
                                    std::to_string(j.value("success", 0)) +
                                    " failed:" + std::to_string(j.value("failed", 0));
                            }
                        } else {
                            summary = "[" + d->kind + "]";
                        }
                        if (!summary.empty()) {
                            screen.post([&, s = std::move(summary)] {
                                // Add as Detail child under last tool call in active parent
                                if (tui_state.active_turn) {
                                    auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                    if (!parent->children.empty()) {
                                        auto& tool_node = parent->children.back();
                                        tool_node.children.push_back({
                                            .kind = agent::tui::TreeNode::Detail,
                                            .state = agent::tui::TreeNode::Done,
                                            .title = s,
                                            .start_ms = agent::tui::steady_now_ms(),
                                        });
                                    }
                                }
                            });
                            screen.refresh();
                        }
                    }
                }
            });

            // Helper: flash messages (thread-safe via Post)
            auto add_hint = [&](std::string text) {
                screen.post([&, t = std::move(text)] {
                    tui_state.flash_text = std::move(t);
                    tui_state.flash_until_ms = agent::tui::steady_now_ms() + 5000;
                });
                screen.refresh();
            };
            auto add_error = [&](std::string text) {
                screen.post([&, t = std::move(text)] {
                    tui_state.flash_text = "\xe2\x9c\x97 " + std::move(t);
                    tui_state.flash_until_ms = agent::tui::steady_now_ms() + 8000;
                });
                screen.refresh();
            };

            // ─── Helper: print conversation history to scrollback ───
            auto print_conversation_history = [&]() {
                namespace llm = mcpplibs::llmapi;
                for (auto& msg : conversation.messages) {
                    if (msg.role == llm::Role::User) {
                        std::string text;
                        if (auto* s = std::get_if<std::string>(&msg.content)) {
                            text = *s;
                        } else if (auto* parts = std::get_if<std::vector<llm::ContentPart>>(&msg.content)) {
                            for (auto& part : *parts) {
                                if (auto* t = std::get_if<llm::TextContent>(&part)) {
                                    text += t->text;
                                }
                            }
                        }
                        if (!text.empty()) {
                            agent::tui::print_chat_line({
                                .type = agent::tui::ChatLine::UserMsg, .text = std::move(text)});
                        }
                    } else if (msg.role == llm::Role::Assistant) {
                        std::string text;
                        if (auto* s = std::get_if<std::string>(&msg.content)) {
                            text = *s;
                        } else if (auto* parts = std::get_if<std::vector<llm::ContentPart>>(&msg.content)) {
                            for (auto& part : *parts) {
                                if (auto* t = std::get_if<llm::TextContent>(&part)) {
                                    text += t->text;
                                }
                            }
                        }
                        if (!text.empty()) {
                            agent::tui::print_chat_line({
                                .type = agent::tui::ChatLine::AssistantText, .text = std::move(text)});
                        }
                    }
                }
                tinytui::flush();
            };

            // ─── Cancellation token (ESC to abort current turn) ───
            CancellationToken cancel_token;
            int ctrl_c_count = 0;
            std::int64_t last_ctrl_c_ms = 0;

            // ─── Message queue (main thread → agent thread) ───
            std::mutex msg_mtx;
            std::condition_variable msg_cv;
            std::deque<std::string> msg_queue;

            // ─── Approval synchronization (agent thread ↔ main thread) ───
            std::mutex approval_mtx;
            std::condition_variable approval_cv;
            std::optional<bool> approval_result;

            agent::ConfirmCallback confirm_cb;
            if (!flag_auto_approve) {
                confirm_cb = [&](std::string_view tool_name, std::string_view arguments) -> bool {
                    // 1. Reset approval_result before showing prompt
                    {
                        std::lock_guard lk(approval_mtx);
                        approval_result = std::nullopt;
                    }

                    // 2. Show approval prompt + add Approval tree node
                    screen.post([&, tn = std::string(tool_name), a = std::string(arguments)] {
                        tui_state.approval_pending = true;
                        tui_state.approval_tool_name = tn;
                        tui_state.approval_args = a;
                        // Add Approval leaf node under current ToolCall
                        if (tui_state.active_turn) {
                            auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                            if (!parent->children.empty() &&
                                parent->children.back().kind == agent::tui::TreeNode::ToolCall &&
                                parent->children.back().state == agent::tui::TreeNode::Running) {
                                parent->children.back().children.push_back({
                                    .kind = agent::tui::TreeNode::Approval,
                                    .state = agent::tui::TreeNode::Running,
                                    .title = "approve " + tn + "(" + utf8::safe_truncate(a, 40) + ")?",
                                    .start_ms = agent::tui::steady_now_ms(),
                                });
                            }
                        }
                    });
                    screen.refresh();

                    // 3. Wait with cancellation awareness (30s timeout)
                    {
                        std::unique_lock lk(approval_mtx);
                        if (!cancel_token.wait_or_cancel(lk, approval_cv,
                                [&] { return approval_result.has_value(); },
                                std::chrono::seconds{30})) {
                            // Timeout or ESC cancel → deny; update tree node
                            screen.post([&] {
                                tui_state.approval_pending = false;
                                if (tui_state.active_turn) {
                                    auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                    if (!parent->children.empty()) {
                                        for (auto& ch : parent->children.back().children) {
                                            if (ch.kind == agent::tui::TreeNode::Approval &&
                                                ch.state == agent::tui::TreeNode::Running) {
                                                ch.state = agent::tui::TreeNode::Failed;
                                                ch.end_ms = agent::tui::steady_now_ms();
                                                ch.title = "denied (timeout)";
                                                break;
                                            }
                                        }
                                    }
                                }
                            });
                            screen.refresh();
                            return false;
                        }
                    }

                    bool approved = *approval_result;
                    approval_result = std::nullopt;

                    // Clear approval prompt + update tree node
                    screen.post([&, approved] {
                        tui_state.approval_pending = false;
                        if (tui_state.active_turn) {
                            auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                            if (!parent->children.empty()) {
                                for (auto& ch : parent->children.back().children) {
                                    if (ch.kind == agent::tui::TreeNode::Approval &&
                                        ch.state == agent::tui::TreeNode::Running) {
                                        ch.state = approved ? agent::tui::TreeNode::Done
                                                           : agent::tui::TreeNode::Failed;
                                        ch.end_ms = agent::tui::steady_now_ms();
                                        ch.title = approved ? "approved" : "denied";
                                        break;
                                    }
                                }
                            }
                        }
                    });
                    screen.refresh();

                    return approved;
                };
            }

            // ─── Auto-responders for agent mode (install confirmation, package selection) ───
            stream.register_auto_responder("confirm_install", [](const PromptEvent& req) {
                return req.defaultValue.empty() ? "y" : req.defaultValue;
            });
            stream.register_auto_responder("select_package", [](const PromptEvent& req) {
                return req.options.empty() ? "" : req.options.front();
            });

            // ─── Slash command registry ───
            agent::CommandRegistry cmd_registry;

            cmd_registry.register_command({"/save", "Save current session", [&](std::string_view) {
                session_mgr.save_conversation(session_meta.id, conversation);
                session_meta.turn_count = conversation.size();
                session_mgr.update_meta(session_meta);
                add_hint("session saved: " + session_meta.id);
            }});

            cmd_registry.register_command({"/sessions", "List all saved sessions", [&](std::string_view) {
                auto sessions = session_mgr.list();
                if (sessions.empty()) {
                    add_hint("no saved sessions");
                } else {
                    for (auto& s : sessions) {
                        add_hint(s.id + " | " + s.title + " | " +
                            s.model + " | " + std::to_string(s.turn_count) + " turns");
                    }
                }
            }});

            cmd_registry.register_command({"/resume", "Resume a saved session", [&](std::string_view args) {
                if (args.empty()) {
                    add_hint("usage: /resume <session-id>");
                    return;
                }
                auto meta = session_mgr.load_meta(std::string(args));
                if (!meta) {
                    add_error("session not found: " + std::string(args));
                    return;
                }
                session_meta = *meta;
                conversation = session_mgr.load_conversation(std::string(args));

                // Print conversation history to scrollback
                screen.post([&] {
                    screen.flush_to_scrollback([&] {
                        print_conversation_history();
                    });
                });

                // Sync context manager state
                ctx_mgr.sync_from_conversation(conversation);

                // Update context cache directory for new session
                auto new_cache_dir = afs.sessions_path() / session_meta.id / "context_cache";
                ctx_mgr.set_cache_dir(new_cache_dir);

                add_hint("resumed session: " + session_meta.title +
                    " (" + std::to_string(conversation.size()) + " messages)");
            }});

            // /task-tree removed — tinytui always shows all tree nodes

            cmd_registry.register_command({"/model", "Switch or show current model", [&](std::string_view args) {
                if (args.empty()) {
                    add_hint("current model: " + cfg.model + " (" + cfg.provider + ")");
                } else {
                    cfg.model = std::string(args);
                    cfg.provider = agent::infer_provider(cfg.model);
                    tools = agent::to_llmapi_tools(bridge);
                    ctx_mgr.set_model(cfg.model);
                    screen.post([&] { tui_state.model_name = cfg.model; });
                    add_hint("switched to: " + cfg.model + " (" + cfg.provider + ")");
                }
            }});

            cmd_registry.register_command({"/tokens", "Show token usage for this session", [&](std::string_view) {
                add_hint("session tokens: \xe2\x86\x91" +
                    agent::TokenTracker::format_tokens(tracker.session_input()) + " \xe2\x86\x93" +
                    agent::TokenTracker::format_tokens(tracker.session_output()));
                add_hint("context: " +
                    agent::TokenTracker::format_tokens(tracker.context_used()) + " / " +
                    agent::TokenTracker::format_tokens(
                        agent::TokenTracker::context_limit(cfg.model)));
            }});

            cmd_registry.register_command({"/compact", "Compress context (keep last N turns, default 3)", [&](std::string_view args) {
                int keep = 3;
                if (!args.empty()) {
                    try { keep = std::stoi(std::string(args)); } catch (...) {}
                }
                auto before = conversation.size();
                ctx_mgr.compact(conversation, keep);
                auto after = conversation.size();
                add_hint("--- context compacted (" + std::to_string(before) +
                    " -> " + std::to_string(after) + " messages, L2 cache: " +
                    std::to_string(ctx_mgr.l2_count()) + " turns) ---");
            }});

            cmd_registry.register_command({"/context", "Show context cache stats", [&](std::string_view) {
                auto ctx_limit = agent::TokenTracker::context_limit(cfg.model);
                add_hint("context cache stats:");
                add_hint("  L1 (hot)  : " + std::to_string(conversation.size()) + " messages");
                add_hint("  L2 (warm) : " + std::to_string(ctx_mgr.l2_count()) + " turn summaries");
                add_hint("  L3 (cold) : " + std::to_string(ctx_mgr.l3_keyword_count()) + " keywords");
                add_hint("  evicted   : ~" + agent::TokenTracker::format_tokens(
                    ctx_mgr.total_evicted_tokens()) + " tokens");
                add_hint("  context   : " + agent::TokenTracker::format_tokens(
                    tracker.context_used()) + "/" + agent::TokenTracker::format_tokens(ctx_limit));
            }});

            cmd_registry.register_command({"/clear", "Clear display", [&](std::string_view) {
                screen.post([&] {
                    tui_state.flash_text.clear();
                });
                screen.refresh();
            }});

            cmd_registry.register_command({"/help", "Show available commands", [&](std::string_view) {
                add_hint("available commands:");
                for (auto& cmd : cmd_registry.list_all()) {
                    add_hint("  " + cmd.name + "  " + cmd.description);
                }
                add_hint("  exit/quit  Exit agent");
            }});

            // ─── tinytui: line editor callbacks ───
            editor.on_change = [&] {
                auto& input = editor.content();
                if (!input.empty() && input[0] == '/' &&
                    input.find(' ') == std::string::npos) {
                    auto matches = cmd_registry.match(input);
                    tui_state.completions = std::move(matches);
                    tui_state.completion_selected = tui_state.completions.empty() ? -1 : 0;
                } else {
                    tui_state.completions.clear();
                    tui_state.completion_selected = -1;
                }
            };

            // ─── tinytui: key handler ───
            screen.set_key_handler([&](const tinytui::KeyEvent& key) -> bool {
                // Approval mode
                if (tui_state.approval_pending) {
                    if (key.type == tinytui::KeyEvent::Char &&
                        (key.ch == "y" || key.ch == "Y")) {
                        std::lock_guard lk(approval_mtx);
                        approval_result = true;
                        approval_cv.notify_one();
                        return true;
                    }
                    if (key.type == tinytui::KeyEvent::Char &&
                        (key.ch == "n" || key.ch == "N")) {
                        std::lock_guard lk(approval_mtx);
                        approval_result = false;
                        approval_cv.notify_one();
                        return true;
                    }
                    if (key.type == tinytui::KeyEvent::Enter) {
                        std::lock_guard lk(approval_mtx);
                        approval_result = true;
                        approval_cv.notify_one();
                        return true;
                    }
                    return true;  // consume all keys in approval mode
                }

                // History: ↑
                if (key.type == tinytui::KeyEvent::Up) {
                    if (!tui_state.completions.empty()) {
                        if (tui_state.completion_selected > 0) --tui_state.completion_selected;
                    } else if (!tui_state.history.empty()) {
                        if (tui_state.history_pos < 0) {
                            tui_state.saved_input = editor.content();
                            tui_state.history_pos = static_cast<int>(tui_state.history.size()) - 1;
                        } else if (tui_state.history_pos > 0) {
                            --tui_state.history_pos;
                        }
                        editor.set_content(tui_state.history[tui_state.history_pos]);
                    }
                    return true;
                }
                if (key.type == tinytui::KeyEvent::Down) {
                    if (!tui_state.completions.empty()) {
                        if (tui_state.completion_selected < static_cast<int>(tui_state.completions.size()) - 1)
                            ++tui_state.completion_selected;
                    } else if (tui_state.history_pos >= 0) {
                        ++tui_state.history_pos;
                        if (tui_state.history_pos >= static_cast<int>(tui_state.history.size())) {
                            tui_state.history_pos = -1;
                            editor.set_content(tui_state.saved_input);
                        } else {
                            editor.set_content(tui_state.history[tui_state.history_pos]);
                        }
                    }
                    return true;
                }
                if (key.type == tinytui::KeyEvent::Tab) {
                    if (tui_state.completion_selected >= 0 &&
                        tui_state.completion_selected < static_cast<int>(tui_state.completions.size())) {
                        editor.set_content(tui_state.completions[tui_state.completion_selected].first + " ");
                        tui_state.completions.clear();
                        tui_state.completion_selected = -1;
                    }
                    return true;
                }
                if (key.type == tinytui::KeyEvent::Escape) {
                    if (!tui_state.completions.empty()) {
                        tui_state.completions.clear();
                        tui_state.completion_selected = -1;
                        return true;
                    }
                    if (tui_state.is_streaming || !tui_state.current_action.empty()) {
                        cancel_token.pause();
                        stream.cancel_all_prompts();
                        return true;
                    }
                    return true;
                }
                // Ctrl+C × 3 to exit
                if (key.type == tinytui::KeyEvent::CtrlC) {
                    auto now = agent::tui::steady_now_ms();
                    if (now - last_ctrl_c_ms > 2000) ctrl_c_count = 0;
                    ++ctrl_c_count;
                    last_ctrl_c_ms = now;
                    if (ctrl_c_count >= 3) {
                        cancel_token.cancel();
                        screen.exit();
                        return true;
                    }
                    tui_state.flash_text = "  Ctrl+C " + std::to_string(ctrl_c_count)
                        + "/3 \xe2\x80\x94 press " + std::to_string(3 - ctrl_c_count) + " more to exit";
                    tui_state.flash_until_ms = agent::tui::steady_now_ms() + 3000;
                    return true;
                }
                // Enter: submit input
                if (key.type == tinytui::KeyEvent::Enter) {
                    auto msg = editor.content();
                    if (msg.empty()) return true;
                    editor.set_content("");
                    tui_state.history_pos = -1;
                    tui_state.saved_input.clear();
                    tui_state.completions.clear();
                    tui_state.completion_selected = -1;
                    {
                        std::lock_guard lk(msg_mtx);
                        msg_queue.push_back(std::move(msg));
                    }
                    msg_cv.notify_one();
                    return true;
                }
                // All other keys → LineEditor
                editor.handle_key(key);
                return true;
            });

            // ─── tinytui: render callback (active area only) ───
            screen.set_renderer([&](tinytui::FrameBuffer& buf) {
                auto now_ms = agent::tui::steady_now_ms();
                int term_w = tinytui::terminal_width();
                agent::tui::render_active_area(tui_state, editor, now_ms, term_w, buf);
            });

            // ─── Agent worker thread ───
            std::jthread agent_thread([&](std::stop_token st) {
                while (!st.stop_requested()) {
                    std::string user_input;
                    {
                        std::unique_lock lk(msg_mtx);
                        msg_cv.wait(lk, [&] {
                            return !msg_queue.empty() || st.stop_requested();
                        });
                        if (st.stop_requested() && msg_queue.empty()) break;
                        if (msg_queue.empty()) continue;
                        user_input = std::move(msg_queue.front());
                        msg_queue.pop_front();
                    }

                    if (user_input == "exit" || user_input == "quit") {
                        screen.post([&] { screen.exit(); });
                        break;
                    }

                    // Handle slash commands
                    if (!user_input.empty() && user_input[0] == '/' && cmd_registry.execute(user_input)) {
                        continue;
                    }

                    journal.log_llm_turn("user", user_input);

                    // Initialize behavior tree BEFORE Post so run_one_turn
                    // gets a valid tree_root pointer (Post is async).
                    auto now_ms = agent::tui::steady_now_ms();
                    tui_state.active_turn = agent::tui::TreeNode{
                        .kind = agent::tui::TreeNode::UserTask,
                        .state = agent::tui::TreeNode::Running,
                        .title = utf8::safe_truncate(user_input, 80),
                        .node_id = 0,
                        .start_ms = now_ms,
                    };
                    tui_state.task_tree.reset();
                    tui_state.last_action_end_ms = now_ms;

                    // Print user message to scrollback + update state
                    screen.post([&, input = user_input, now_ms] {
                        // Clear active area first, print to scrollback, then redraw
                        screen.flush_to_scrollback([&] {
                            agent::tui::print_chat_line({
                                .type = agent::tui::ChatLine::UserMsg, .text = input});
                        });
                        tui_state.flash_text.clear();
                        tui_state.is_streaming = true;
                        tui_state.is_thinking = true;
                        tui_state.streaming_text.clear();
                        tui_state.current_action = "thinking...";
                        tui_state.turn_start_ms = now_ms;
                        if (tui_state.history.empty() || tui_state.history.back() != input) {
                            tui_state.history.push_back(input);
                        }
                    });
                    screen.refresh();

                    // Reset cancel token for new turn
                    cancel_token.reset();

                    // Run LLM turn with streaming
                    agent::tui::ThinkFilter think_filter;
                    agent::TurnResult turn_result;

                    try {
                        turn_result = agent::run_one_turn(
                            conversation, user_input, system_prompt, tools, bridge, stream, cfg,
                            // Streaming callback
                            [&](std::string_view chunk) {
                                cancel_token.throw_if_cancelled();
                                auto filtered = think_filter.filter(chunk);
                                bool thinking = think_filter.in_think;
                                screen.post([&, f = std::move(filtered), thinking] {
                                    if (thinking) {
                                        if (tui_state.streaming_text.empty()) {
                                            tui_state.is_thinking = true;
                                            tui_state.current_action = "thinking...";
                                        }
                                        // Add/update live Thinking node in tree (under active parent)
                                        if (tui_state.active_turn) {
                                            auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                            if (parent->children.empty() ||
                                                parent->children.back().state != agent::tui::TreeNode::Running ||
                                                parent->children.back().kind != agent::tui::TreeNode::Thinking) {
                                                parent->children.push_back({
                                                    .kind = agent::tui::TreeNode::Thinking,
                                                    .state = agent::tui::TreeNode::Running,
                                                    .title = "thinking",
                                                    .start_ms = tui_state.last_action_end_ms,
                                                });
                                            }
                                        }
                                    }
                                    if (!f.empty()) {
                                        tui_state.is_thinking = false;
                                        tui_state.streaming_text += f;
                                        tui_state.current_action = "responding...";
                                        // Close live Thinking node, open/update Response node
                                        if (tui_state.active_turn) {
                                            auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                            auto now = agent::tui::steady_now_ms();
                                            // Close running Thinking if present
                                            if (!parent->children.empty() &&
                                                parent->children.back().state == agent::tui::TreeNode::Running &&
                                                parent->children.back().kind == agent::tui::TreeNode::Thinking) {
                                                parent->children.back().end_ms = now;
                                                parent->children.back().state = agent::tui::TreeNode::Done;
                                            }
                                            // Extract title: first line of streaming text
                                            auto nl_pos = tui_state.streaming_text.find('\n');
                                            std::string resp_title = (nl_pos != std::string::npos)
                                                ? tui_state.streaming_text.substr(0, nl_pos)
                                                : tui_state.streaming_text;
                                            if (resp_title.size() > 80) resp_title = utf8::safe_truncate(resp_title, 80);
                                            // Add/update live Response node
                                            if (!parent->children.empty() &&
                                                parent->children.back().state == agent::tui::TreeNode::Running &&
                                                parent->children.back().kind == agent::tui::TreeNode::Response) {
                                                parent->children.back().title = tui_state.streaming_text;
                                            } else {
                                                parent->children.push_back({
                                                    .kind = agent::tui::TreeNode::Response,
                                                    .state = agent::tui::TreeNode::Running,
                                                    .title = tui_state.streaming_text,
                                                    .start_ms = now,
                                                });
                                            }
                                        }
                                    }
                                });
                                screen.refresh();
                            },
                            policy_ptr, confirm_cb,
                            // Tool call callback — flush streaming, build tree nodes
                            [&](int id, std::string_view name, std::string_view args) {
                                auto call_start = agent::tui::steady_now_ms();
                                std::string args_display = utf8::safe_truncate(args, 60);
                                screen.post([&, id, n = std::string(name), a = std::move(args_display), call_start] {
                                    // Clear streaming text — it's already in the tree as a Response node.
                                    // Don't push to lines (avoids duplicate ◆ text above tree).
                                    tui_state.streaming_text.clear();
                                    tui_state.is_streaming = false;
                                    tui_state.is_thinking = false;
                                    tui_state.current_action = "executing " + n + "...";

                                    // Close any running Thinking/Response, add ToolCall under active parent
                                    if (tui_state.active_turn) {
                                        auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                        // Close running Thinking or Response node
                                        if (!parent->children.empty() &&
                                            parent->children.back().state == agent::tui::TreeNode::Running) {
                                            auto& last = parent->children.back();
                                            if (last.kind == agent::tui::TreeNode::Thinking ||
                                                last.kind == agent::tui::TreeNode::Response) {
                                                last.end_ms = call_start;
                                                last.state = agent::tui::TreeNode::Done;
                                            }
                                        }
                                        // If no thinking node was present, add a completed one
                                        if (parent->children.empty() ||
                                            (parent->children.back().kind != agent::tui::TreeNode::Thinking &&
                                             parent->children.back().kind != agent::tui::TreeNode::Response)) {
                                            parent->children.push_back({
                                                .kind = agent::tui::TreeNode::Thinking,
                                                .state = agent::tui::TreeNode::Done,
                                                .title = "thinking",
                                                .start_ms = tui_state.last_action_end_ms,
                                                .end_ms = call_start,
                                            });
                                        }
                                        // Add tool call node (running)
                                        parent->children.push_back({
                                            .kind = agent::tui::TreeNode::ToolCall,
                                            .state = agent::tui::TreeNode::Running,
                                            .title = n + "(" + a + ")",
                                            .action_id = id,
                                            .start_ms = call_start,
                                        });
                                    }
                                });
                                screen.refresh();
                            },
                            // Tool result callback — close tree node, back to streaming
                            [&](int id, std::string_view name, bool is_error) {
                                auto end_ms = agent::tui::steady_now_ms();
                                screen.post([&, id, n = std::string(name), is_error, end_ms] {
                                    // Close last running tool call under active parent
                                    if (tui_state.active_turn) {
                                        auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                        if (!parent->children.empty() &&
                                            parent->children.back().state == agent::tui::TreeNode::Running &&
                                            parent->children.back().kind == agent::tui::TreeNode::ToolCall) {
                                            auto& last = parent->children.back();
                                            last.end_ms = end_ms;
                                            last.state = is_error ? agent::tui::TreeNode::Failed
                                                                  : agent::tui::TreeNode::Done;
                                        }
                                    }
                                    tui_state.last_action_end_ms = end_ms;

                                    // Reset to streaming/thinking state
                                    tui_state.current_action = "thinking...";
                                    tui_state.is_streaming = true;
                                    tui_state.is_thinking = true;
                                });
                                screen.refresh();
                            },
                            &ctx_mgr, &tracker,
                            // Auto-compact callback
                            [&](int evicted, int freed) {
                                add_hint("\xe2\x97\x88 auto-compact: evicted "
                                    + std::to_string(evicted) + " turns, freed ~"
                                    + agent::TokenTracker::format_tokens(freed) + " tokens");
                            },
                            &cancel_token,
                            &tui_state.task_tree,
                            tui_state.active_turn ? &*tui_state.active_turn : nullptr,
                            // Tree update callback
                            [&](const std::string& action, int node_id, const std::string& title) {
                                screen.post([&, action, node_id, title] {
                                    // Tree updates are handled directly by TaskTree in loop.cppm
                                    // Just trigger a UI refresh
                                });
                                screen.refresh();
                            },
                            // Token update callback — real-time status bar updates
                            [&](int input_tokens, int output_tokens) {
                                screen.post([&, input_tokens, output_tokens] {
                                    tui_state.session_input = tracker.session_input() + input_tokens;
                                    tui_state.session_output = tracker.session_output() + output_tokens;
                                    tui_state.ctx_used = tracker.context_used() + input_tokens;
                                });
                                screen.refresh();
                            }
                        );
                    } catch (const PausedException&) {
                        cancel_token.reset();  // Reset to Active (wait for resume)
                        screen.post([&] {
                            tui_state.is_streaming = false;
                            tui_state.is_thinking = false;
                            tui_state.streaming_text.clear();
                            tui_state.current_action = "paused";
                            // Mark running nodes as Paused (don't move to history)
                            if (tui_state.active_turn) {
                                tui_state.active_turn->mark_running_as(agent::tui::TreeNode::Paused);
                            }
                            tui_state.flash_text = "  \xe2\x8f\xb8 paused \xe2\x80\x94 send new message to continue";
                            tui_state.flash_until_ms = agent::tui::steady_now_ms() + 10000;
                        });
                        screen.refresh();
                        // Don't pop conversation, don't reset active_turn
                        continue;
                    } catch (const CancelledException&) {
                        cancel_token.reset();
                        screen.post([&] {
                            tui_state.is_streaming = false;
                            tui_state.is_thinking = false;
                            tui_state.streaming_text.clear();
                            tui_state.current_action.clear();
                            tui_state.turn_start_ms = 0;
                            // Print cancelled tree to scrollback
                            if (tui_state.active_turn) {
                                auto cancel_ms = agent::tui::steady_now_ms();
                                tui_state.active_turn->end_ms = cancel_ms;
                                tui_state.active_turn->state = agent::tui::TreeNode::Cancelled;
                                tui_state.active_turn->mark_running_as(agent::tui::TreeNode::Cancelled);
                                screen.flush_to_scrollback([&] {
                                    agent::tui::print_chat_line({
                                        .type = agent::tui::ChatLine::TurnTree,
                                        .tree = *tui_state.active_turn});
                                });
                                tui_state.active_turn.reset();
                            }
                            tui_state.flash_text = "  cancelled by user";
                            tui_state.flash_until_ms = agent::tui::steady_now_ms() + 5000;
                        });
                        // Remove the incomplete user message from conversation
                        if (!conversation.messages.empty()) {
                            conversation.messages.pop_back();
                        }
                        continue;
                    } catch (const std::exception& e) {
                        bool was_cancelled = cancel_token.is_cancelled();
                        cancel_token.reset();
                        screen.post([&, err = std::string(e.what()), was_cancelled] {
                            tui_state.is_streaming = false;
                            tui_state.is_thinking = false;
                            tui_state.streaming_text.clear();
                            tui_state.current_action.clear();
                            tui_state.turn_start_ms = 0;
                            // Print failed tree to scrollback
                            if (tui_state.active_turn) {
                                auto err_ms = agent::tui::steady_now_ms();
                                tui_state.active_turn->end_ms = err_ms;
                                tui_state.active_turn->state = agent::tui::TreeNode::Failed;
                                tui_state.active_turn->mark_running_as(agent::tui::TreeNode::Failed);
                                screen.flush_to_scrollback([&] {
                                    agent::tui::print_chat_line({
                                        .type = agent::tui::ChatLine::TurnTree,
                                        .tree = *tui_state.active_turn});
                                });
                                tui_state.active_turn.reset();
                            }
                            if (was_cancelled) {
                                tui_state.flash_text = "  cancelled by user";
                                tui_state.flash_until_ms = agent::tui::steady_now_ms() + 5000;
                            } else {
                                tui_state.flash_text = "\xe2\x9c\x97 LLM request failed: " + err;
                                tui_state.flash_until_ms = agent::tui::steady_now_ms() + 8000;
                            }
                        });
                        screen.refresh();
                        // Remove the incomplete user message from conversation
                        if (was_cancelled && !conversation.messages.empty()) {
                            conversation.messages.pop_back();
                        }
                        continue;
                    }

                    // Flush remaining think filter content
                    auto remaining = think_filter.flush();

                    // Turn complete: finalize tree, print to scrollback
                    auto turn_end_ms = agent::tui::steady_now_ms();
                    screen.post([&, rem = std::move(remaining), tr = turn_result, turn_end_ms] {
                        // Append remaining filter text
                        if (!rem.empty()) {
                            tui_state.streaming_text += rem;
                            if (tui_state.active_turn) {
                                auto* parent = tui_state.task_tree.active_parent(*tui_state.active_turn);
                                if (!parent->children.empty() &&
                                    parent->children.back().kind == agent::tui::TreeNode::Response) {
                                    parent->children.back().title = tui_state.streaming_text;
                                }
                            }
                        }
                        tui_state.is_streaming = false;
                        tui_state.is_thinking = false;
                        tui_state.streaming_text.clear();

                        // Finalize tree, print to scrollback
                        if (tui_state.active_turn) {
                            auto& turn = *tui_state.active_turn;
                            turn.mark_running_as(agent::tui::TreeNode::Done);
                            turn.complete_pending();
                            turn.end_ms = turn_end_ms;
                            turn.state = agent::tui::TreeNode::Done;
                            turn.input_tokens = tr.input_tokens;
                            turn.output_tokens = tr.output_tokens;

                            // Clear active area, print completed turn to scrollback
                            screen.flush_to_scrollback([&] {
                                agent::tui::print_chat_line({
                                    .type = agent::tui::ChatLine::TurnTree, .tree = turn});
                                if (!tr.reply.empty()) {
                                    agent::tui::print_chat_line({
                                        .type = agent::tui::ChatLine::AssistantText, .text = tr.reply});
                                }
                            });

                            tui_state.active_turn.reset();
                        }

                        // Update status bar
                        tracker.record(tr.input_tokens, tr.output_tokens);
                        ctx_mgr.record_turn();
                        tui_state.ctx_used = tracker.context_used();
                        tui_state.session_input = tracker.session_input();
                        tui_state.session_output = tracker.session_output();
                        tui_state.l2_cache_count = ctx_mgr.l2_count();
                        tui_state.current_action.clear();
                        tui_state.turn_start_ms = 0;
                    });
                    screen.refresh();

                    journal.log_llm_turn("assistant", turn_result.reply);
                }
            });

            // ─── Timer thread: 24fps re-render for smooth UI and resize handling ───
            std::jthread timer_thread([&](std::stop_token st) {
                while (!st.stop_requested()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(42));
                    if (!st.stop_requested()) {
                        screen.refresh();
                    }
                }
            });

            // ─── Print history if resuming a session ───
            if (!conversation.messages.empty()) {
                print_conversation_history();
            }

            // ─── tinytui main loop (blocks main thread) ───
            screen.loop();

            // Stop timer
            timer_thread.request_stop();

            // ─── Cleanup ───
            agent_thread.request_stop();
            msg_cv.notify_all();
            // jthread destructor will join

            // Save context cache
            ctx_mgr.save_cache();

            // Restore TUI listener and remove agent listeners
            stream.set_enabled(tui_listener, true);
            stream.remove_listener(agent_listener);
            stream.remove_listener(data_listener);
            stream.clear_auto_responders();

            // Auto-save on exit
            session_mgr.save_conversation(session_meta.id, conversation);
            session_meta.turn_count = conversation.size();
            session_mgr.update_meta(session_meta);
            return 0;
        }
        if (cmd == "script") {
            if (fargc < 3) {
                ui::print_usage("xlings script <script-file> [args...]");
                return 1;
            }
            namespace fs = std::filesystem;
            fs::path scriptFile = fargv[2];
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
            ctx.xpkg_dir = Config::paths().dataDir / "xpkgs";
            for (int i = 3; i < fargc; ++i) {
                ctx.args.emplace_back(fargv[i]);
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

        // install
        .subcommand("install")
            .description("Install packages (e.g. xlings install gcc@15 node)")
            .option(cmdline::Option("global").short_name('g').help("Install to global scope (not project-local subos)"))
            .arg("packages").help("Package names with optional version")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::vector<std::string> targets;
                for (std::size_t i = 0; i < args.positional_count(); ++i) {
                    auto t = args.positional(i);
                    if (!t.empty()) targets.emplace_back(t);
                }
                if (targets.empty()) return install_from_project_config_(stream);

                bool yes = args.is_flag_set("yes");
                bool global = args.is_flag_set("global");
                return xim::cmd_install(targets, yes, false, stream, global);
            })

        // remove
        .subcommand("remove")
            .description("Remove a package")
            .arg("package").required().help("Package to remove")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_remove(std::string(args.positional(0)), stream);
            })

        // update
        .subcommand("update")
            .description("Update package index or a specific package")
            .arg("package").help("Package to update (omit for index only)")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::string target;
                if (args.positional_count() > 0)
                    target = std::string(args.positional(0));
                return xim::cmd_update(target, stream);
            })

        // search
        .subcommand("search")
            .description("Search for packages")
            .arg("keyword").required().help("Search keyword")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_search(std::string(args.positional(0)), stream);
            })

        // list
        .subcommand("list")
            .description("List installed packages")
            .arg("filter").help("Filter pattern")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                std::string filter;
                if (args.positional_count() > 0)
                    filter = std::string(args.positional(0));
                return xim::cmd_list(filter, stream);
            })

        // info
        .subcommand("info")
            .description("Show package information")
            .arg("package").required().help("Package name")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return xim::cmd_info(std::string(args.positional(0)), stream);
            })

        // use
        .subcommand("use")
            .description("Switch tool version")
            .arg("target").required().help("Tool name")
            .arg("version").help("Version to switch to (omit to list)")
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                auto target = std::string(args.positional(0));
                if (args.positional_count() >= 2) {
                    return xvm::cmd_use(target, std::string(args.positional(1)), stream);
                }
                return xvm::cmd_list_versions(target, stream);
            })

        // config
        .subcommand("config")
            .description("Show or modify xlings configuration")
            .option(cmdline::Option("lang").takes_value().value_name("LANG").help("Set language (en/zh)"))
            .option(cmdline::Option("mirror").takes_value().value_name("MIRROR").help("Set mirror (GLOBAL/CN)"))
            .option(cmdline::Option("add-xpkg").takes_value().value_name("FILE").help("Add xpkg file to package index"))
            .option(cmdline::Option("index-repo").takes_value().value_name("NS:URL").help("Add/update index repo (e.g. myns:https://...git)"))
            .action([&stream](const cmdline::ParsedArgs& args) -> int {
                apply_global_opts_(args);
                return cmd_config_(args, stream);
            });

    return app.run(argc, argv);
}

} // namespace xlings::cli
