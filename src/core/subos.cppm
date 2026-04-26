export module xlings.core.subos;

import std;

import xlings.core.config;
import xlings.libs.json;
import xlings.core.log;
import xlings.platform;
import xlings.runtime;
import xlings.core.utils;
import xlings.core.xself;

namespace xlings::subos {

namespace fs = std::filesystem;

export struct SubosInfo {
    std::string   name;
    fs::path      dir;
    bool          isActive;
    int           toolCount;
};

nlohmann::json read_config_json_(const fs::path& path) {
    if (!fs::exists(path)) return nlohmann::json::object();
    try {
        auto content = platform::read_file_to_string(path.string());
        auto json = nlohmann::json::parse(content, nullptr, false);
        return json.is_discarded() ? nlohmann::json::object() : json;
    } catch (...) { return nlohmann::json::object(); }
}

void write_config_json_(const fs::path& path, const nlohmann::json& json) {
    platform::write_string_to_file(path.string(), json.dump(2));
}

export std::vector<SubosInfo> list_all() {
    auto& p = Config::paths();
    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);

    std::vector<SubosInfo> result;

    if (json.contains("subos") && json["subos"].is_object()) {
        for (auto it = json["subos"].begin(); it != json["subos"].end(); ++it) {
            auto name = it.key();
            auto dir  = Config::subos_dir(name);
            int toolCount = 0;
            auto binDir   = dir / "bin";
            if (fs::exists(binDir)) {
                for (auto& e : platform::dir_entries(binDir)) {
                    auto stem = e.path().stem().string();
                    if (!xself::is_builtin_shim(stem) && stem != "xvm-alias")
                        ++toolCount;
                }
            }
            result.push_back({name, dir, p.activeSubos == name, toolCount});
        }
    } else {
        result.push_back({"default", Config::subos_dir("default"),
                          p.activeSubos == "default", 0});
    }

    std::ranges::sort(result, {}, &SubosInfo::name);
    return result;
}

void update_current_symlink_(EventStream& stream,
                              const fs::path& homeDir,
                              const fs::path& targetDir) {
    auto linkPath = homeDir / "subos" / "current";
    std::error_code ec;
    fs::remove(linkPath, ec);
    fs::create_directory_symlink(targetDir, linkPath, ec);
    if (ec) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::Permission,
            .message = "failed to update current symlink: " + ec.message(),
            .recoverable = true,
        });
    }
}

export int create(const std::string& name, const fs::path& customDir,
                  EventStream& stream) {
    auto& p = Config::paths();

    if (name == "current") {
        stream.emit(ErrorEvent{
            .code = ErrorCode::InvalidInput,
            .message = "'current' is a reserved subos name",
            .recoverable = false,
        });
        return 1;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            stream.emit(ErrorEvent{
                .code = ErrorCode::InvalidInput,
                .message = "invalid subos name: '" + name
                           + "' (allowed: alphanumeric, underscore, dash)",
                .recoverable = false,
            });
            return 1;
        }
    }

    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);
    if (json.contains("subos") && json["subos"].contains(name)) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::InvalidInput,
            .message = "subos '" + name + "' already exists",
            .recoverable = false,
        });
        return 1;
    }

    auto dir = customDir.empty() ? (p.homeDir / "subos" / name) : customDir;

    fs::create_directories(dir / "bin");
    fs::create_directories(dir / "lib");
    fs::create_directories(dir / "usr");
    fs::create_directories(dir / "generations");

    // Create empty .xlings.json with workspace
    auto subosConfig = dir / ".xlings.json";
    if (!fs::exists(subosConfig)) {
        nlohmann::json j;
        j["workspace"] = nlohmann::json::object();
        write_config_json_(subosConfig, j);
    }

    // Create shim hardlinks from xlings binary
    auto xlingsBin = p.homeDir / "xlings";
    if (!fs::exists(xlingsBin))
        xlingsBin = p.homeDir / "bin" / "xlings";
    if (fs::exists(xlingsBin)) {
        xself::ensure_subos_shims(dir / "bin", xlingsBin, p.homeDir);
    }

    if (!json.contains("subos")) json["subos"] = nlohmann::json::object();
    json["subos"][name] = {{"dir", customDir.empty() ? "" : customDir.string()}};
    write_config_json_(configPath, json);

    nlohmann::json payload;
    payload["name"] = name;
    payload["dir"]  = dir.string();
    stream.emit(DataEvent{"subos_created", payload.dump()});
    return 0;
}

export int use(const std::string& name, EventStream& stream) {
    auto& p = Config::paths();
    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);

    if (!json.contains("subos") || !json["subos"].contains(name)) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::NotFound,
            .message = "subos '" + name + "' not found",
            .recoverable = true,
            .hint = "create it first: xlings subos new " + name,
        });
        return 1;
    }

    json["activeSubos"] = name;
    write_config_json_(configPath, json);

    auto dir = Config::subos_dir(name);
    update_current_symlink_(stream, p.homeDir, dir);

    nlohmann::json payload;
    payload["name"] = name;
    payload["dir"]  = dir.string();
    stream.emit(DataEvent{"subos_switched", payload.dump()});
    return 0;
}

export int remove(const std::string& name, EventStream& stream) {
    if (name == "default") {
        stream.emit(ErrorEvent{
            .code = ErrorCode::InvalidInput,
            .message = "cannot remove the 'default' subos",
            .recoverable = false,
        });
        return 1;
    }

    auto& p = Config::paths();
    if (p.activeSubos == name) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::InvalidInput,
            .message = "cannot remove the active subos '" + name + "'",
            .recoverable = true,
            .hint = "switch first: xlings subos use default",
        });
        return 1;
    }

    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);

    if (!json.contains("subos") || !json["subos"].contains(name)) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::NotFound,
            .message = "subos '" + name + "' not found",
            .recoverable = true,
        });
        return 1;
    }

    auto dir = Config::subos_dir(name);
    if (fs::exists(dir)) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        if (ec) {
            stream.emit(ErrorEvent{
                .code = ErrorCode::Permission,
                .message = "failed to remove " + dir.string() + ": " + ec.message(),
                .recoverable = false,
            });
            return 1;
        }
    }

    json["subos"].erase(name);
    write_config_json_(configPath, json);

    nlohmann::json payload;
    payload["name"] = name;
    stream.emit(DataEvent{"subos_removed", payload.dump()});
    return 0;
}

export std::optional<SubosInfo> info(const std::string& name) {
    auto& p = Config::paths();
    auto dir = Config::subos_dir(name);
    if (!fs::exists(dir)) return std::nullopt;

    int toolCount = 0;
    auto binDir = dir / "bin";
    if (fs::exists(binDir)) {
        for (auto& e : platform::dir_entries(binDir)) {
            auto stem = e.path().stem().string();
            if (!xself::is_builtin_shim(stem) && stem != "xvm-alias")
                ++toolCount;
        }
    }
    return SubosInfo{name, dir, p.activeSubos == name, toolCount};
}

int run_list_(EventStream& stream) {
    auto all = list_all();
    std::vector<std::tuple<std::string, std::string, int, bool>> entries;
    for (auto& s : all) {
        entries.emplace_back(s.name, s.dir.string(), s.toolCount, s.isActive);
    }
    nlohmann::json entriesJson = nlohmann::json::array();
    for (auto& [n, d, tc, active] : entries) {
        entriesJson.push_back({{"name", n}, {"dir", d}, {"pkgCount", tc}, {"active", active}});
    }
    nlohmann::json payload;
    payload["entries"] = std::move(entriesJson);
    stream.emit(DataEvent{"subos_list", payload.dump()});
    return 0;
}

int run_info_(const std::string& name, EventStream& stream) {
    auto& p = Config::paths();
    auto target = name.empty() ? p.activeSubos : name;
    auto si = info(target);
    if (!si) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::NotFound,
            .message = "subos '" + target + "' not found",
            .recoverable = true,
        });
        return 1;
    }
    nlohmann::json fieldsJson = nlohmann::json::array();
    fieldsJson.push_back({{"label", "active"}, {"value", si->isActive ? "yes" : "no"}, {"highlight", si->isActive}});
    fieldsJson.push_back({{"label", "dir"}, {"value", si->dir.string()}, {"highlight", false}});
    fieldsJson.push_back({{"label", "tools"}, {"value", std::to_string(si->toolCount)}, {"highlight", false}});
    nlohmann::json payload;
    payload["title"] = si->name;
    payload["fields"] = std::move(fieldsJson);
    stream.emit(DataEvent{"info_panel", payload.dump()});
    return 0;
}

export int run(int argc, char* argv[], EventStream& stream) {
    if (argc < 3) return run_list_(stream);

    std::string sub = argv[2];
    if (sub == "ls") sub = "list";
    if (sub == "rm") sub = "remove";
    if (sub == "i")  sub = "info";

    auto usageError = [&](std::string_view detail) {
        stream.emit(ErrorEvent{
            .code = ErrorCode::InvalidInput,
            .message = std::string(detail),
            .recoverable = false,
            .hint = "usage: xlings subos <new|use|list|ls|remove|rm|info|i> [name]",
        });
    };

    if (sub == "new") {
        if (argc < 4) { usageError("missing <name> for: xlings subos new"); return 1; }
        return create(argv[3], {}, stream);
    }
    if (sub == "use") {
        if (argc < 4) { usageError("missing <name> for: xlings subos use"); return 1; }
        return use(argv[3], stream);
    }
    if (sub == "list")   return run_list_(stream);
    if (sub == "remove") {
        if (argc < 4) { usageError("missing <name> for: xlings subos remove|rm"); return 1; }
        return remove(argv[3], stream);
    }
    if (sub == "info")   return run_info_(argc > 3 ? argv[3] : "", stream);

    usageError("unknown subcommand: " + sub);
    return 1;
}

} // namespace xlings::subos
