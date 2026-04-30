export module xlings.core.xself;

import std;

export import :init;
export import :install;

import xlings.core.config;
import xlings.libs.json;
import xlings.core.log;
import xlings.platform;
import xlings.core.profile;
import xlings.runtime;
import xlings.core.utils;
import xlings.core.xvm.types;
import xlings.core.xvm.db;

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

static int cmd_config(EventStream& stream) {
    auto& p = Config::paths();
    nlohmann::json fieldsJson = nlohmann::json::array();
    auto addField = [&](const std::string& label, const std::string& value, bool hl = false) {
        fieldsJson.push_back({{"label", label}, {"value", value}, {"highlight", hl}});
    };
    addField("XLINGS_HOME", p.homeDir.string());
    addField("XLINGS_DATA", p.dataDir.string());
    addField("XLINGS_SUBOS", p.subosDir.string());
    addField("active subos", p.activeSubos, true);
    addField("bin", p.binDir.string());

    auto mirror = Config::mirror();
    if (!mirror.empty()) addField("mirror", mirror);
    auto lang = Config::lang();
    if (!lang.empty()) addField("lang", lang);

    auto& repos = Config::global_index_repos();
    for (auto& repo : repos) {
        addField("index-repo", repo.name + " : " + repo.url);
    }

    if (Config::has_project_config()) {
        addField("project data", Config::project_data_dir().string());
        auto& projectRepos = Config::project_index_repos();
        for (auto& repo : projectRepos) {
            addField("project repo", repo.name + " : " + repo.url);
        }
    }

    nlohmann::json payload;
    payload["title"] = "xlings config";
    payload["fields"] = std::move(fieldsJson);
    stream.emit(DataEvent{"info_panel", payload.dump()});
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

// `xlings self doctor` — verify the workspace ↔ shim file invariant.
//
// Background: every program registered in the active workspace must have a
// corresponding shim at `<binDir>/<name>` (the file that gets dispatched
// through the bootstrap multiplexer). The two layers — workspace pointer
// (logical) and shim file (physical) — must stay in sync, otherwise users
// see "command not found" for a program their workspace says is active.
//
// Doctor verifies:
//   1. Every program in workspace has its shim file present (and report
//      missing ones).
//   2. Every program-typed shim under binDir has a workspace entry (and
//      report orphans).
//
// With `--fix`, it self-heals: missing shims are recreated from the
// bootstrap binary; orphan shims are deleted.
static int cmd_doctor(EventStream& stream, bool fix) {
    auto& p   = Config::paths();
    auto db   = Config::versions();
    auto ws   = Config::effective_workspace();

#ifdef _WIN32
    constexpr std::string_view shim_ext = ".exe";
    auto xlings_bin = p.homeDir / "bin" / "xlings.exe";
#else
    constexpr std::string_view shim_ext = "";
    auto xlings_bin = p.homeDir / "bin" / "xlings";
#endif
    if (!fs::exists(xlings_bin)) {
        xlings_bin = p.homeDir / "xlings";
    }

    auto shim_filename = [&](const std::string& name) {
        std::string fn = name;
        if (!shim_ext.empty() && !fn.ends_with(shim_ext)) fn += shim_ext;
        return fn;
    };

    nlohmann::json fields = nlohmann::json::array();
    auto add_field = [&](std::string_view label, std::string value, bool hl = false) {
        fields.push_back({{"label", std::string(label)},
                          {"value", std::move(value)},
                          {"highlight", hl}});
    };

    int missing = 0;
    int orphans = 0;
    int healed  = 0;

    // Check 1: every workspace program has its shim.
    for (auto& [name, version] : ws) {
        if (version.empty()) continue;
        auto* vi = xvm::get_vinfo(db, name);
        if (!vi || vi->type != "program") continue;

        auto shim_path = p.binDir / shim_filename(name);
        if (fs::exists(shim_path) || fs::is_symlink(shim_path)) continue;

        ++missing;
        std::string detail = std::format("workspace[{}]={} but {} missing",
                                         name, version, shim_path.string());
        if (fix && fs::exists(xlings_bin)) {
            std::error_code ec;
            fs::create_directories(p.binDir, ec);
            auto r = create_shim(xlings_bin, shim_path);
            if (r != LinkResult::Failed) {
                ++healed;
                detail += " — recreated";
            } else {
                detail += " — recreate failed";
            }
        }
        add_field("✗ missing shim", std::move(detail));
    }

    // Check 2: orphan shims (program shim file present, workspace doesn't
    // know about it). Only consider names that are registered as type
    // "program" in the version DB — random files under binDir aren't ours.
    if (fs::exists(p.binDir)) {
        for (auto& entry : platform::dir_entries(p.binDir)) {
            std::error_code ec;
            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec)) continue;
            auto fname = entry.path().filename().string();
            std::string base = fname;
            if (!shim_ext.empty() && base.ends_with(shim_ext)) {
                base = base.substr(0, base.size() - shim_ext.size());
            }

            auto* vi = xvm::get_vinfo(db, base);
            if (!vi || vi->type != "program") continue;

            auto wit = ws.find(base);
            bool active_present = (wit != ws.end() && !wit->second.empty());
            if (active_present) continue;

            ++orphans;
            std::string detail = std::format(
                "{} exists but workspace has no active version for {}",
                entry.path().string(), base);
            if (fix) {
                ec.clear();
                fs::remove(entry.path(), ec);
                if (!ec) {
                    ++healed;
                    detail += " — removed";
                } else {
                    detail += " — remove failed";
                }
            }
            add_field("✗ orphan shim", std::move(detail));
        }
    }

    int issues = missing + orphans;
    if (issues == 0) {
        add_field("status", "OK — workspace and shim files are consistent", true);
    } else {
        add_field("missing shims", std::to_string(missing));
        add_field("orphan shims",  std::to_string(orphans));
        if (fix) add_field("healed", std::to_string(healed), true);
        else     add_field("hint", "rerun with `--fix` to repair", true);
    }

    nlohmann::json payload;
    payload["title"]  = "xlings self doctor";
    payload["fields"] = std::move(fields);
    stream.emit(DataEvent{"info_panel", payload.dump()});

    // Exit non-zero only when issues remain after the (optional) fix pass.
    int unresolved = issues - (fix ? healed : 0);
    return unresolved == 0 ? 0 : 1;
}

static int cmd_help(EventStream& stream) {
    nlohmann::json payload;
    payload["name"] = "self";
    payload["description"] = "Manage xlings itself";
    payload["args"] = nlohmann::json::array();
    payload["opts"] = nlohmann::json::array({
        {{"name", "install"},  {"desc", "Install xlings from release package"}},
        {{"name", "init"},     {"desc", "Create home/data/subos dirs"}},
        {{"name", "update"},   {"desc", "Update index + install latest xlings"}},
        {{"name", "config"},   {"desc", "Show configuration details"}},
        {{"name", "clean"},    {"desc", "Remove cache + gc orphaned packages (--dry-run)"}},
        {{"name", "migrate"},  {"desc", "Migrate old layout to subos/default"}},
        {{"name", "doctor"},   {"desc", "Verify workspace/shim consistency (--fix to repair)"}},
    });
    stream.emit(DataEvent{"help", payload.dump()});
    return 0;
}

export int run(int argc, char* argv[], EventStream& stream) {
    std::string action = (argc >= 3) ? argv[2] : "help";
    if (action == "install") return cmd_install();
    if (action == "init") return cmd_init();
    if (action == "update") return cmd_update();
    if (action == "config") return cmd_config(stream);
    if (action == "clean") {
        bool dryRun = argc >= 4 && std::string(argv[3]) == "--dry-run";
        return cmd_clean(dryRun);
    }
    if (action == "migrate") return cmd_migrate();
    if (action == "doctor") {
        bool fix = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--fix") { fix = true; break; }
        }
        return cmd_doctor(stream, fix);
    }
    return cmd_help(stream);
}

} // namespace xlings::xself
