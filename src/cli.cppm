module;

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/terminal.hpp"

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
import xlings.agent.token_tracker;
import xlings.agent.context_manager;
import xlings.libs.agentfs;
import xlings.libs.soul;
import xlings.libs.journal;
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

            // Consumer 2: CLI TUI rendering — disabled in agent mode (ftxui owns terminal)
            stream.set_enabled(tui_listener, false);

            // Token tracker + context manager
            agent::TokenTracker tracker;
            agent::ContextManager ctx_mgr(cfg.model);
            auto cache_dir = afs.sessions_path() / session_meta.id / "context_cache";
            ctx_mgr.set_cache_dir(cache_dir);

            // ─── ftxui screen (FitComponent = natural terminal scrolling) ───
            auto screen = ftxui::ScreenInteractive::FitComponent();
            screen.TrackMouse(false);  // allow terminal scroll wheel

            agent::tui::AgentTuiState tui_state;
            tui_state.model_name = cfg.model;
            tui_state.ctx_limit = agent::TokenTracker::context_limit(cfg.model);

            // ─── Consumer 3: ftxui-compatible data event listener ───
            // Handles download progress (always) + verbose data display (toggled)
            bool verbose_data = false;

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

            int ftxui_data_listener = stream.on_event([&](const Event& e) {
                if (auto* d = std::get_if<DataEvent>(&e)) {
                    // Download progress → update active_progress in tui_state
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

                        screen.Post([&, pct, name = std::move(active_name),
                                     spd = std::move(speed), et = std::move(eta_str)] {
                            tui_state.active_progress = pct;
                            tui_state.active_progress_name = name;
                            tui_state.active_progress_speed = spd;
                            tui_state.active_progress_eta = et;
                        });
                        screen.PostEvent(ftxui::Event::Custom);
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
                            screen.Post([&, s = std::move(summary)] {
                                tui_state.active_details.push_back(s);
                            });
                            screen.PostEvent(ftxui::Event::Custom);
                        }
                    }
                }
            });

            // Helper: add output lines (thread-safe via Post)
            auto add_hint = [&](std::string text) {
                screen.Post([&, t = std::move(text)] {
                    tui_state.lines.push_back({
                        .type = agent::tui::ChatLine::Hint,
                        .text = std::move(t),
                    });
                });
                screen.PostEvent(ftxui::Event::Custom);
            };
            auto add_error = [&](std::string text) {
                screen.Post([&, t = std::move(text)] {
                    tui_state.lines.push_back({
                        .type = agent::tui::ChatLine::Error,
                        .text = std::move(t),
                    });
                });
                screen.PostEvent(ftxui::Event::Custom);
            };

            // ─── Cancellation flag (ESC to abort current turn) ───
            std::atomic<bool> cancel_requested{false};

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
                    // Show approval prompt in TUI
                    screen.Post([&, tn = std::string(tool_name), a = std::string(arguments)] {
                        tui_state.approval_pending = true;
                        tui_state.approval_tool_name = tn;
                        tui_state.approval_args = a;
                    });
                    screen.PostEvent(ftxui::Event::Custom);

                    // Block agent thread until user responds
                    {
                        std::lock_guard lk(approval_mtx);
                        approval_result = std::nullopt;
                    }
                    std::unique_lock lk(approval_mtx);
                    approval_cv.wait(lk, [&] { return approval_result.has_value(); });
                    bool approved = *approval_result;

                    // Clear approval prompt
                    screen.Post([&] { tui_state.approval_pending = false; });
                    screen.PostEvent(ftxui::Event::Custom);

                    return approved;
                };
            }

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
                add_hint("resumed session: " + session_meta.title +
                    " (" + std::to_string(conversation.size()) + " messages)");
            }});

            cmd_registry.register_command({"/verbose", "Toggle activity details display", [&](std::string_view) {
                verbose_data = !verbose_data;
                add_hint(verbose_data ? "activity details: ON" : "activity details: OFF");
            }});

            cmd_registry.register_command({"/model", "Switch or show current model", [&](std::string_view args) {
                if (args.empty()) {
                    add_hint("current model: " + cfg.model + " (" + cfg.provider + ")");
                } else {
                    cfg.model = std::string(args);
                    cfg.provider = agent::infer_provider(cfg.model);
                    tools = agent::to_llmapi_tools(bridge);
                    ctx_mgr.set_model(cfg.model);
                    screen.Post([&] { tui_state.model_name = cfg.model; });
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
                add_hint("compacted: " + std::to_string(before) +
                    " -> " + std::to_string(after) + " messages" +
                    " (L2 cache: " + std::to_string(ctx_mgr.l2_count()) + " turns)");
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

            cmd_registry.register_command({"/clear", "Clear output", [&](std::string_view) {
                screen.Post([&] { tui_state.lines.clear(); });
                screen.PostEvent(ftxui::Event::Custom);
            }});

            cmd_registry.register_command({"/help", "Show available commands", [&](std::string_view) {
                add_hint("available commands:");
                for (auto& cmd : cmd_registry.list_all()) {
                    add_hint("  " + cmd.name + "  " + cmd.description);
                }
                add_hint("  exit/quit  Exit agent");
            }});

            // ─── Build ftxui component (minimal: active content + input + status) ───
            // Start with a space so ftxui places a blinking cursor (workaround for
            // Chinese IME positioning — empty input has no cursor anchor).
            std::string input_content = " ";

            auto input_opt = ftxui::InputOption();
            input_opt.content = &input_content;
            input_opt.multiline = false;
            input_opt.placeholder = "";
            // Strip default decoration (no white background)
            input_opt.transform = [](ftxui::InputState state) {
                return state.element;
            };
            input_opt.on_change = [&] {
                // Ensure leading space cursor anchor is preserved
                if (input_content.empty() || input_content[0] != ' ') {
                    input_content = " " + input_content;
                }
                // Check for slash commands (after the leading space)
                auto trimmed = input_content.substr(1);
                if (!trimmed.empty() && trimmed[0] == '/' &&
                    trimmed.find(' ') == std::string::npos) {
                    auto matches = cmd_registry.match(trimmed);
                    tui_state.completions = std::move(matches);
                    tui_state.completion_selected = tui_state.completions.empty() ? -1 : 0;
                } else {
                    tui_state.completions.clear();
                    tui_state.completion_selected = -1;
                }
            };
            input_opt.on_enter = [&] {
                if (tui_state.approval_pending) {
                    std::lock_guard lk(approval_mtx);
                    approval_result = true;
                    approval_cv.notify_one();
                    return;
                }
                // Trim leading space (cursor workaround)
                auto msg = input_content;
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                if (msg.empty()) return;
                input_content = " ";  // reset with cursor anchor space
                tui_state.history_pos = -1;
                tui_state.saved_input.clear();
                tui_state.completions.clear();
                tui_state.completion_selected = -1;
                {
                    std::lock_guard lk(msg_mtx);
                    msg_queue.push_back(std::move(msg));
                }
                msg_cv.notify_one();
            };

            auto input_component = ftxui::Input(input_opt);

            auto component = ftxui::CatchEvent(input_component, [&](ftxui::Event event) {
                if (tui_state.approval_pending) {
                    if (event == ftxui::Event::Character('y') || event == ftxui::Event::Character('Y')) {
                        std::lock_guard lk(approval_mtx);
                        approval_result = true;
                        approval_cv.notify_one();
                        return true;
                    }
                    if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N')) {
                        std::lock_guard lk(approval_mtx);
                        approval_result = false;
                        approval_cv.notify_one();
                        return true;
                    }
                    if (event.is_character()) return true;
                }

                // History: ↑
                if (event == ftxui::Event::ArrowUp) {
                    if (!tui_state.completions.empty()) {
                        if (tui_state.completion_selected > 0) --tui_state.completion_selected;
                    } else if (!tui_state.history.empty()) {
                        if (tui_state.history_pos < 0) {
                            tui_state.saved_input = input_content;
                            tui_state.history_pos = static_cast<int>(tui_state.history.size()) - 1;
                        } else if (tui_state.history_pos > 0) {
                            --tui_state.history_pos;
                        }
                        input_content = " " + tui_state.history[tui_state.history_pos];
                    }
                    return true;
                }
                if (event == ftxui::Event::ArrowDown) {
                    if (!tui_state.completions.empty()) {
                        if (tui_state.completion_selected < static_cast<int>(tui_state.completions.size()) - 1)
                            ++tui_state.completion_selected;
                    } else if (tui_state.history_pos >= 0) {
                        ++tui_state.history_pos;
                        if (tui_state.history_pos >= static_cast<int>(tui_state.history.size())) {
                            tui_state.history_pos = -1;
                            input_content = tui_state.saved_input;
                        } else {
                            input_content = " " + tui_state.history[tui_state.history_pos];
                        }
                    }
                    return true;
                }
                if (event == ftxui::Event::Tab) {
                    if (tui_state.completion_selected >= 0 &&
                        tui_state.completion_selected < static_cast<int>(tui_state.completions.size())) {
                        input_content = " " + tui_state.completions[tui_state.completion_selected].first + " ";
                        tui_state.completions.clear();
                        tui_state.completion_selected = -1;
                    }
                    return true;
                }
                if (event == ftxui::Event::Escape) {
                    if (!tui_state.completions.empty()) {
                        tui_state.completions.clear();
                        tui_state.completion_selected = -1;
                        return true;
                    }
                    if (tui_state.is_streaming || !tui_state.current_action.empty()) {
                        cancel_requested.store(true);
                        return true;
                    }
                }
                return false;
            });

            // Renderer: adaptive layout — expands downward until reaching
            // terminal bottom, then pins input/status and scrolls content up.
            auto renderer = ftxui::Renderer(component, [&] {
                using namespace ftxui;
                auto now_ms = agent::tui::steady_now_ms();
                auto term_sz = ftxui::Terminal::Size();
                int term_h = term_sz.dimy;
                int term_w = term_sz.dimx;

                // ── Build content elements (history + streaming + active action) ──
                // Trim history for performance (keep at most ~2x terminal height)
                int max_history = std::max(term_h * 2, 40);
                Elements content_elems;

                int est_rows = 0;
                int start_idx = static_cast<int>(tui_state.lines.size());
                for (int i = static_cast<int>(tui_state.lines.size()) - 1; i >= 0; --i) {
                    auto& line = tui_state.lines[i];
                    int h = 1;
                    if (line.type == agent::tui::ChatLine::ToolAction) {
                        h += static_cast<int>(line.sub_steps.size())
                           + static_cast<int>(line.details.size());
                    } else if (line.type == agent::tui::ChatLine::AssistantText) {
                        h = std::max(1, static_cast<int>(line.text.size()) / std::max(term_w - 4, 20) + 1);
                    }
                    if (est_rows + h > max_history) break;
                    est_rows += h;
                    start_idx = i;
                }
                for (int i = start_idx; i < static_cast<int>(tui_state.lines.size()); ++i) {
                    content_elems.push_back(agent::tui::render_chat_line(tui_state.lines[i], now_ms));
                }

                // Active streaming / thinking
                if (tui_state.is_streaming) {
                    if (tui_state.is_thinking && tui_state.streaming_text.empty()) {
                        auto elapsed_str = tui_state.turn_start_ms > 0
                            ? agent::tui::format_duration(now_ms - tui_state.turn_start_ms) : "";
                        content_elems.push_back(hbox({
                            text("  thinking... ") | color(agent::tui::colors::dim()),
                            text(elapsed_str) | color(agent::tui::colors::border()),
                        }));
                    } else if (!tui_state.streaming_text.empty()) {
                        content_elems.push_back(hbox({
                            text("\xe2\x97\x86 ") | color(agent::tui::colors::magenta()),
                            paragraph(tui_state.streaming_text),
                        }));
                    }
                }

                // Active tool action
                if (!tui_state.active_action_text.empty()) {
                    content_elems.push_back(hbox({
                        text("  \xe2\x9a\xa1 ") | color(agent::tui::colors::amber()),
                        text(tui_state.active_action_text) | color(agent::tui::colors::amber()),
                    }));
                    for (auto& ss : tui_state.active_sub_steps) {
                        content_elems.push_back(agent::tui::render_sub_step(ss, now_ms));
                    }
                    if (tui_state.active_progress > 0.01f) {
                        int pct = static_cast<int>(tui_state.active_progress * 100.0f);
                        content_elems.push_back(hbox({
                            text("      \xe2\x96\xb8 ") | color(agent::tui::colors::cyan()),
                            gauge(tui_state.active_progress)
                                | size(WIDTH, EQUAL, 24) | color(agent::tui::colors::cyan()),
                            text(" " + std::to_string(pct) + "%") | bold,
                            text("  " + tui_state.active_progress_name)
                                | color(agent::tui::colors::magenta()),
                            text("  " + tui_state.active_progress_speed)
                                | color(agent::tui::colors::cyan()),
                            text("  " + tui_state.active_progress_eta)
                                | color(agent::tui::colors::dim()),
                        }));
                    }
                    for (auto& d : tui_state.active_details) {
                        content_elems.push_back(text("      " + d) | color(agent::tui::colors::border()));
                    }
                }

                // ── Bottom area: separator + input + separator + completions + status + margin ──
                Elements bottom_elems;
                bottom_elems.push_back(separator() | color(agent::tui::colors::border()));
                if (tui_state.approval_pending) {
                    bottom_elems.push_back(agent::tui::render_approval(tui_state));
                } else {
                    bottom_elems.push_back(hbox({
                        text("> ") | bold | color(agent::tui::colors::cyan()),
                        component->Render() | flex,
                    }) | size(WIDTH, EQUAL, term_w));
                }
                bottom_elems.push_back(separator() | color(agent::tui::colors::border()));
                if (!tui_state.completions.empty()) {
                    bottom_elems.push_back(agent::tui::render_completions(tui_state));
                }
                bottom_elems.push_back(agent::tui::render_status_bar(tui_state, now_ms));
                bottom_elems.push_back(text(""));

                // Count bottom area height: sep(1) + input(1) + sep(1) + status(1) + margin(1) = 5
                int bottom_h = 5;
                if (!tui_state.completions.empty()) {
                    bottom_h += std::min(static_cast<int>(tui_state.completions.size()), 8);
                }
                if (tui_state.approval_pending) bottom_h += 1;

                // Estimate content height (rough: reuse est_rows from trimming + active elements)
                int content_h = est_rows;
                if (tui_state.is_streaming) content_h += 1;
                if (!tui_state.active_action_text.empty()) {
                    content_h += 1 + static_cast<int>(tui_state.active_sub_steps.size())
                        + static_cast<int>(tui_state.active_details.size());
                    if (tui_state.active_progress > 0.01f) content_h += 1;
                }

                int total_h = content_h + bottom_h;

                if (total_h < term_h) {
                    // ── Not yet at bottom: natural expansion, no pinning ──
                    Elements all;
                    for (auto& e : content_elems) all.push_back(std::move(e));
                    for (auto& e : bottom_elems) all.push_back(std::move(e));
                    return vbox(std::move(all));
                } else {
                    // ── Reached bottom: pin bottom area, scroll content up ──
                    auto scroll_area = vbox(std::move(content_elems))
                        | yframe | focusPositionRelative(0, 1) | flex;
                    return vbox({
                        scroll_area,
                        vbox(std::move(bottom_elems)),
                    }) | size(HEIGHT, EQUAL, term_h);
                }
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
                        screen.Post([&] { screen.Exit(); });
                        break;
                    }

                    // Handle slash commands
                    if (!user_input.empty() && user_input[0] == '/' && cmd_registry.execute(user_input)) {
                        continue;
                    }

                    journal.log_llm_turn("user", user_input);

                    // Add user message to lines, set streaming state
                    auto now_ms = agent::tui::steady_now_ms();
                    screen.Post([&, input = user_input, now_ms] {
                        tui_state.lines.push_back({
                            .type = agent::tui::ChatLine::UserMsg,
                            .text = input,
                        });
                        tui_state.is_streaming = true;
                        tui_state.is_thinking = true;
                        tui_state.streaming_text.clear();
                        tui_state.current_action = "thinking...";
                        tui_state.turn_start_ms = now_ms;
                        tui_state.active_progress = 0.0f;
                        tui_state.active_details.clear();
                        if (tui_state.history.empty() || tui_state.history.back() != input) {
                            tui_state.history.push_back(input);
                        }
                    });
                    screen.PostEvent(ftxui::Event::Custom);

                    // Reset cancel flag
                    cancel_requested.store(false);

                    // Run LLM turn with streaming
                    agent::tui::ThinkFilter think_filter;
                    agent::TurnResult turn_result;

                    try {
                        turn_result = agent::run_one_turn(
                            conversation, user_input, system_prompt, tools, bridge, stream, cfg,
                            // Streaming callback
                            [&](std::string_view chunk) {
                                if (cancel_requested.load()) {
                                    throw std::runtime_error("cancelled");
                                }
                                auto filtered = think_filter.filter(chunk);
                                bool thinking = think_filter.in_think;
                                screen.Post([&, f = std::move(filtered), thinking] {
                                    if (thinking && tui_state.streaming_text.empty()) {
                                        tui_state.is_thinking = true;
                                        tui_state.current_action = "thinking...";
                                    }
                                    if (!f.empty()) {
                                        tui_state.is_thinking = false;
                                        tui_state.streaming_text += f;
                                        tui_state.current_action = "responding...";
                                    }
                                });
                                screen.PostEvent(ftxui::Event::Custom);
                            },
                            policy_ptr, confirm_cb,
                            // Tool call callback — flush streaming, track active action
                            [&](int id, std::string_view name, std::string_view args) {
                                auto call_start = agent::tui::steady_now_ms();
                                std::string args_display(args);
                                if (args_display.size() > 60) {
                                    args_display = args_display.substr(0, 57) + "...";
                                }
                                // Flush any streaming text to lines
                                screen.Post([&, n = std::string(name), a = std::move(args_display), call_start] {
                                    if (!tui_state.streaming_text.empty()) {
                                        tui_state.lines.push_back({
                                            .type = agent::tui::ChatLine::AssistantText,
                                            .text = std::move(tui_state.streaming_text),
                                        });
                                        tui_state.streaming_text.clear();
                                    }
                                    tui_state.is_streaming = false;
                                    tui_state.is_thinking = false;
                                    tui_state.current_action = "executing " + n + "...";
                                    tui_state.active_action_text = n + " (" + a + ")";
                                    tui_state.active_action_start = call_start;
                                    tui_state.active_progress = 0.0f;
                                    tui_state.active_details.clear();
                                    // Build sub-steps: close thinking, start execution
                                    tui_state.active_sub_steps.clear();
                                    tui_state.active_sub_steps.push_back({
                                        .label = "thinking",
                                        .start_ms = tui_state.turn_start_ms,
                                        .end_ms = call_start,
                                        .done = true,
                                    });
                                    tui_state.active_sub_steps.push_back({
                                        .label = "execution",
                                        .start_ms = call_start,
                                    });
                                });
                                screen.PostEvent(ftxui::Event::Custom);
                            },
                            // Tool result callback — add completed action to lines
                            [&](int id, std::string_view name, bool is_error) {
                                auto end_ms = agent::tui::steady_now_ms();
                                screen.Post([&, id, n = std::string(name), is_error, end_ms] {
                                    // Close execution sub-step
                                    if (!tui_state.active_sub_steps.empty()) {
                                        auto& last = tui_state.active_sub_steps.back();
                                        if (!last.done) {
                                            last.end_ms = end_ms;
                                            last.done = true;
                                            last.failed = is_error;
                                        }
                                    }
                                    // Total time = sum of all sub-step durations
                                    int64_t total_ms = 0;
                                    for (auto& ss : tui_state.active_sub_steps) {
                                        if (ss.done) total_ms += (ss.end_ms - ss.start_ms);
                                    }
                                    // Build completed action line
                                    agent::tui::ChatLine done_line;
                                    done_line.type = agent::tui::ChatLine::ToolAction;
                                    done_line.text = tui_state.active_action_text;
                                    done_line.action_id = id;
                                    done_line.status = is_error ? agent::tui::ChatLine::Failed
                                                               : agent::tui::ChatLine::Done;
                                    done_line.elapsed_ms = static_cast<int>(total_ms);
                                    done_line.sub_steps = tui_state.active_sub_steps;
                                    done_line.details = tui_state.active_details;
                                    tui_state.lines.push_back(std::move(done_line));

                                    // Reset active state, back to streaming
                                    tui_state.current_action = "thinking...";
                                    tui_state.active_action_text.clear();
                                    tui_state.active_action_start = 0;
                                    tui_state.active_progress = 0.0f;
                                    tui_state.active_details.clear();
                                    tui_state.active_sub_steps.clear();
                                    tui_state.is_streaming = true;
                                    tui_state.is_thinking = true;
                                });
                                screen.PostEvent(ftxui::Event::Custom);
                            },
                            &ctx_mgr, &tracker,
                            // Auto-compact callback
                            [&](int evicted, int freed) {
                                add_hint("\xe2\x97\x88 auto-compact: evicted "
                                    + std::to_string(evicted) + " turns, freed ~"
                                    + agent::TokenTracker::format_tokens(freed) + " tokens");
                            }
                        );
                    } catch (const std::exception& e) {
                        bool was_cancelled = cancel_requested.exchange(false);
                        screen.Post([&, err = std::string(e.what()), was_cancelled] {
                            tui_state.is_streaming = false;
                            tui_state.is_thinking = false;
                            tui_state.streaming_text.clear();
                            tui_state.current_action.clear();
                            tui_state.turn_start_ms = 0;
                            if (was_cancelled) {
                                tui_state.lines.push_back({
                                    .type = agent::tui::ChatLine::Hint,
                                    .text = "  cancelled by user",
                                });
                            } else {
                                tui_state.lines.push_back({
                                    .type = agent::tui::ChatLine::Error,
                                    .text = "LLM request failed: " + err,
                                });
                            }
                            tui_state.lines.push_back({.type = agent::tui::ChatLine::Separator});
                        });
                        screen.PostEvent(ftxui::Event::Custom);
                        // Remove the incomplete user message from conversation
                        if (was_cancelled && !conversation.messages.empty()) {
                            conversation.messages.pop_back();
                        }
                        continue;
                    }

                    // Flush remaining think filter content
                    auto remaining = think_filter.flush();

                    // Turn complete: finalize streaming, update status
                    auto turn_end_ms = agent::tui::steady_now_ms();
                    screen.Post([&, rem = std::move(remaining), tr = turn_result, turn_end_ms] {
                        auto total_elapsed = tui_state.turn_start_ms > 0
                            ? turn_end_ms - tui_state.turn_start_ms : 0LL;
                        std::string final_text = tui_state.streaming_text;
                        if (!rem.empty()) final_text += rem;
                        if (!final_text.empty()) {
                            tui_state.lines.push_back({
                                .type = agent::tui::ChatLine::AssistantText,
                                .text = std::move(final_text),
                            });
                        }
                        tui_state.is_streaming = false;
                        tui_state.is_thinking = false;
                        tui_state.streaming_text.clear();
                        // Show total turn time as hint
                        tui_state.lines.push_back({
                            .type = agent::tui::ChatLine::Hint,
                            .text = "  total: " + agent::tui::format_duration(
                                static_cast<int>(total_elapsed)),
                        });

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
                    screen.PostEvent(ftxui::Event::Custom);

                    journal.log_llm_turn("assistant", turn_result.reply);
                }
            });

            // ─── Timer thread: 24fps re-render for smooth UI and resize handling ───
            std::jthread timer_thread([&](std::stop_token st) {
                while (!st.stop_requested()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(42));
                    if (!st.stop_requested()) {
                        screen.PostEvent(ftxui::Event::Custom);
                    }
                }
            });

            // ─── Ensure cursor is on input from the start ───
            input_component->TakeFocus();

            // ─── ftxui main loop (blocks main thread) ───
            screen.Loop(renderer);

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
            stream.remove_listener(ftxui_data_listener);

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
