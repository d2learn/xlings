module;

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

#ifdef __unix__
#include <poll.h>
#endif

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
import xlings.core.utf8;

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
        // Skip CLI rendering in TUI mode (agent TUI handles progress separately)
        if (platform::is_tui_mode()) return;
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

// ─────────────────────────────────────────────────────────────────
// xlings interface — programmatic JSON API (NDJSON over stdio)
// Spec: docs/plans/2026-04-25-interface-api-v1.md
// ─────────────────────────────────────────────────────────────────

constexpr const char* kProtocolVersion = "1.0";

// Convert any Event variant to one NDJSON line (no trailing newline).
// Returns "" for events not surfaced to wire (e.g. CompletedEvent).
std::string event_to_ndjson_line_(const Event& e) {
    nlohmann::json line;
    if (auto* p = std::get_if<ProgressEvent>(&e)) {
        line = {{"kind", "progress"}, {"phase", p->phase},
                {"percent", p->percent}, {"message", p->message}};
    } else if (auto* l = std::get_if<LogEvent>(&e)) {
        const char* lvl = l->level == LogLevel::debug ? "debug"
                        : l->level == LogLevel::info  ? "info"
                        : l->level == LogLevel::warn  ? "warn" : "error";
        line = {{"kind", "log"}, {"level", lvl}, {"message", l->message}};
    } else if (auto* d = std::get_if<DataEvent>(&e)) {
        auto payload = nlohmann::json::parse(d->json, nullptr, false);
        line = {{"kind", "data"}, {"dataKind", d->kind},
                {"payload", payload.is_discarded() ? nlohmann::json(d->json) : payload}};
    } else if (auto* pr = std::get_if<PromptEvent>(&e)) {
        line = {{"kind", "prompt"}, {"id", pr->id},
                {"question", pr->question}, {"options", pr->options},
                {"defaultValue", pr->defaultValue}};
    } else if (auto* er = std::get_if<ErrorEvent>(&e)) {
        line = {{"kind", "error"}, {"code", er->code},
                {"message", er->message}, {"recoverable", er->recoverable}};
    } else if (std::get_if<CompletedEvent>(&e)) {
        return "";  // Internal terminator; cli emits explicit kind:result line instead
    } else {
        return "";
    }
    return line.dump();
}

// Coordinator for one `xlings interface <cap>` invocation.
// Owns: stdout writer mutex, heartbeat timer thread, stdin control reader thread,
// CancellationToken driving capability execution.
class InterfaceSession {
public:
    InterfaceSession(EventStream& stream, CancellationToken& token)
        : stream_(stream), token_(token),
          last_emit_(std::chrono::steady_clock::now()) {
        heartbeat_thread_ = std::jthread([this](std::stop_token st) { heartbeat_loop_(st); });
        stdin_thread_     = std::jthread([this](std::stop_token st) { stdin_loop_(st); });
    }

    ~InterfaceSession() {
        // Order matters: heartbeat first (cooperative), then stdin (poll-based)
        if (heartbeat_thread_.joinable()) heartbeat_thread_.request_stop();
        if (stdin_thread_.joinable())     stdin_thread_.request_stop();
        // jthread destructor joins
    }

    InterfaceSession(const InterfaceSession&) = delete;
    InterfaceSession& operator=(const InterfaceSession&) = delete;

    // Emit any in-protocol Event (skips events that don't map to wire).
    void emit_event(const Event& e) {
        auto line = event_to_ndjson_line_(e);
        if (!line.empty()) emit_raw_line_(line);
    }

    // Emit the terminal `result` line. Caller passes the JSON string returned
    // from Capability::execute (typically "{\"exitCode\":N}"). If unparseable
    // we still wrap it so the wire format stays consistent.
    void emit_result(int exitCode, std::string_view raw_content) {
        nlohmann::json line;
        line["kind"]     = "result";
        line["exitCode"] = exitCode;
        auto parsed = nlohmann::json::parse(raw_content, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            // Lift any non-exitCode fields into result.data (forward compatible).
            nlohmann::json data = nlohmann::json::object();
            for (auto it = parsed.begin(); it != parsed.end(); ++it) {
                if (it.key() != "exitCode") data[it.key()] = it.value();
            }
            if (!data.empty()) line["data"] = std::move(data);
        }
        emit_raw_line_(line.dump());
    }

private:
    void emit_raw_line_(std::string_view s) {
        std::lock_guard lock(io_mtx_);
        std::cout << s << "\n" << std::flush;
        last_emit_.store(std::chrono::steady_clock::now(),
                         std::memory_order_release);
    }

    void heartbeat_loop_(std::stop_token st) {
        using namespace std::chrono;
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(milliseconds(500));
            if (st.stop_requested()) break;
            auto now = steady_clock::now();
            auto last = last_emit_.load(std::memory_order_acquire);
            if (now - last >= seconds(5)) {
                auto t = system_clock::to_time_t(system_clock::now());
                std::string ts(32, '\0');
                auto n = std::strftime(ts.data(), ts.size(), "%FT%TZ", std::gmtime(&t));
                ts.resize(n);
                nlohmann::json line = {{"kind", "heartbeat"}, {"ts", ts}};
                emit_raw_line_(line.dump());
            }
        }
    }

    void stdin_loop_(std::stop_token st) {
        // Poll-based read so we can respect stop_token. POSIX-only.
#ifdef __unix__
        while (!st.stop_requested()) {
            ::pollfd pfd{0 /*stdin*/, POLLIN, 0};
            int rc = ::poll(&pfd, 1, 100);  // 100ms wakeup
            if (rc <= 0) continue;
            if (pfd.revents & (POLLHUP | POLLERR)) return;
            if (!(pfd.revents & POLLIN)) continue;
            std::string line;
            if (!std::getline(std::cin, line)) return;  // EOF
            handle_stdin_line_(line);
        }
#else
        // Non-Unix: blocking read. Process exits after capability completes,
        // OS closes stdin, getline returns false.
        std::string line;
        while (!st.stop_requested() && std::getline(std::cin, line)) {
            handle_stdin_line_(line);
        }
#endif
    }

    void handle_stdin_line_(std::string_view line) {
        if (line.empty()) return;
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("action")) return;
        auto action = j["action"].is_string() ? j["action"].get<std::string>() : "";
        if (action == "cancel")        token_.cancel();
        else if (action == "pause")    token_.pause();
        else if (action == "resume")   token_.resume();
        else if (action == "prompt-reply") {
            stream_.respond(j.value("id", ""), j.value("value", ""));
        } else {
            emit_event(Event{LogEvent{LogLevel::warn,
                "interface: unknown stdin action: " + action}});
        }
    }

    EventStream& stream_;
    CancellationToken& token_;
    std::mutex io_mtx_;
    std::atomic<std::chrono::steady_clock::time_point> last_emit_;
    std::jthread heartbeat_thread_;
    std::jthread stdin_thread_;
};

int cmd_interface_(const mcpplibs::cmdline::ParsedArgs& args,
                   EventStream& stream, int tui_listener,
                   capability::Registry& registry) {
    // interface mode disables TUI rendering — stdout is pure NDJSON.
    // Also flip the global tui_mode flag so stray log::println / log::print
    // calls inside capabilities suppress their stdout writes (they would
    // otherwise pollute the NDJSON stream).
    stream.set_enabled(tui_listener, false);
    platform::set_tui_mode(true);

    // --version: emit protocol version and exit
    if (args.is_flag_set("version")) {
        std::cout << nlohmann::json({{"protocol_version", kProtocolVersion}}).dump()
                  << "\n" << std::flush;
        return 0;
    }

    // --list: emit all capability specs
    if (args.is_flag_set("list")) {
        auto specs = registry.list_all();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& s : specs) {
            auto inputSchema = nlohmann::json::parse(s.inputSchema, nullptr, false);
            auto outputSchema = nlohmann::json::parse(s.outputSchema, nullptr, false);
            arr.push_back({
                {"name", s.name},
                {"description", s.description},
                {"destructive", s.destructive},
                {"inputSchema", inputSchema.is_discarded() ? nlohmann::json(s.inputSchema) : inputSchema},
                {"outputSchema", outputSchema.is_discarded() ? nlohmann::json(s.outputSchema) : outputSchema},
            });
        }
        nlohmann::json out = {{"protocol_version", kProtocolVersion},
                              {"capabilities", arr}};
        std::cout << out.dump() << "\n" << std::flush;
        return 0;
    }

    // Need a capability name from here on
    if (args.positional_count() == 0) {
        std::cout << R"({"kind":"result","exitCode":1,"error":"capability name required. Use --list to see available capabilities."})"
                  << "\n" << std::flush;
        return 1;
    }

    auto cap_name = std::string(args.positional(0));
    std::string cap_args = "{}";
    if (auto a = args.value("args")) cap_args = std::string(*a);

    auto* cap = registry.get(cap_name);
    if (!cap) {
        nlohmann::json err = {
            {"kind", "error"},
            {"code", 404},
            {"message", "unknown capability: " + cap_name},
            {"recoverable", false}
        };
        std::cout << err.dump() << "\n";
        nlohmann::json done = {{"kind", "result"}, {"exitCode", 1}};
        std::cout << done.dump() << "\n" << std::flush;
        return 1;
    }

    CancellationToken token;
    InterfaceSession session(stream, token);
    int listener_id = stream.on_event([&session](const Event& e) {
        session.emit_event(e);
    });

    capability::Result result;
    int exit_code = 0;
    try {
        result = cap->execute(cap_args, stream, &token);
    } catch (const CancelledException&) {
        result = nlohmann::json({{"exitCode", 130}}).dump();
        exit_code = 130;
    } catch (const std::exception& e) {
        nlohmann::json err = {
            {"kind", "error"}, {"code", 500},
            {"message", std::string("internal: ") + e.what()},
            {"recoverable", false}
        };
        std::cout << err.dump() << "\n";
        result = nlohmann::json({{"exitCode", 1}}).dump();
        exit_code = 1;
    }
    stream.remove_listener(listener_id);

    if (exit_code == 0) {
        auto parsed = nlohmann::json::parse(result, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("exitCode")) {
            exit_code = parsed["exitCode"].is_number_integer()
                ? parsed["exitCode"].get<int>() : 0;
        }
    }
    session.emit_result(exit_code, result);
    return exit_code;
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

            if (h) {
                ui::print_subcommand_help(h->name, h->desc, h->args, h->opts, h->subcmds);
                return 0;
            }
        }

        // Detect unknown commands early — show TUI error + help
        {
            static constexpr std::string_view known_cmds[] = {
                "install", "remove", "update", "search", "list",
                "info", "use", "config", "subos", "self", "script",
                "interface",
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
            })

        // interface — programmatic JSON API (NDJSON over stdio).
        // See docs/plans/2026-04-25-interface-api-v1.md for full protocol spec.
        .subcommand("interface")
            .description("Programmatic JSON API for external tools (NDJSON over stdio)")
            .option(cmdline::Option("args").takes_value().value_name("JSON")
                .help("Capability arguments as JSON string"))
            .option(cmdline::Option("list").help("List all available capabilities with schemas"))
            .option(cmdline::Option("version").help("Print protocol version and exit"))
            .arg("capability").help("Capability name to invoke")
            .action([&stream, tui_listener, &registry](const cmdline::ParsedArgs& args) -> int {
                return cmd_interface_(args, stream, tui_listener, registry);
            });

    return app.run(argc, argv);
}

} // namespace xlings::cli
