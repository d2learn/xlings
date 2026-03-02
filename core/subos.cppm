export module xlings.subos;

import std;

import xlings.config;
import xlings.json;
import xlings.log;
import xlings.platform;
import xlings.utils;
import xlings.xself;

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

void update_current_symlink_(const fs::path& homeDir, const fs::path& targetDir) {
    auto linkPath = homeDir / "subos" / "current";
    std::error_code ec;
    fs::remove(linkPath, ec);
    fs::create_directory_symlink(targetDir, linkPath, ec);
    if (ec) log::error("[xlings:subos] failed to update current symlink: {}", ec.message());
}

export int create(const std::string& name, const fs::path& customDir = {}) {
    auto& p = Config::paths();

    if (name == "current") {
        log::error("[xlings:subos] 'current' is reserved");
        return 1;
    }

    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            log::error("[xlings:subos] invalid name: {}", name);
            return 1;
        }
    }

    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);
    if (json.contains("subos") && json["subos"].contains(name)) {
        log::error("[xlings:subos] '{}' already exists", name);
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

    std::println("[xlings:subos] created '{}'", name);
    std::println("  dir: {}", dir.string());
    return 0;
}

export int use(const std::string& name) {
    auto& p = Config::paths();
    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);

    if (!json.contains("subos") || !json["subos"].contains(name)) {
        log::error("[xlings:subos] '{}' not found", name);
        log::error("  run: xlings subos new {}", name);
        return 1;
    }

    json["activeSubos"] = name;
    write_config_json_(configPath, json);

    auto dir = Config::subos_dir(name);
    update_current_symlink_(p.homeDir, dir);

    std::println("[xlings:subos] switched to '{}' ({})", name, dir.string());
    return 0;
}

export int remove(const std::string& name) {
    if (name == "default") {
        log::error("[xlings:subos] cannot remove the default subos");
        return 1;
    }

    auto& p = Config::paths();
    if (p.activeSubos == name) {
        log::error("[xlings:subos] cannot remove the active subos '{}'", name);
        log::error("  switch first: xlings subos use default");
        return 1;
    }

    auto configPath = p.homeDir / ".xlings.json";
    auto json = read_config_json_(configPath);

    if (!json.contains("subos") || !json["subos"].contains(name)) {
        log::error("[xlings:subos] '{}' not found", name);
        return 1;
    }

    auto dir = Config::subos_dir(name);
    if (fs::exists(dir)) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        if (ec) {
            log::error("[xlings:subos] failed to remove {}: {}", dir.string(), ec.message());
            return 1;
        }
    }

    json["subos"].erase(name);
    write_config_json_(configPath, json);

    std::println("[xlings:subos] removed '{}'", name);
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

int run_list_() {
    auto all = list_all();
    std::println("[xlings:subos] list:");
    for (auto& s : all) {
        std::println("  {}{}  ({}  tools: {})",
            s.isActive ? "* " : "  ",
            s.name, s.dir.string(), s.toolCount);
    }
    return 0;
}

int run_info_(const std::string& name) {
    auto& p = Config::paths();
    auto target = name.empty() ? p.activeSubos : name;
    auto si = info(target);
    if (!si) {
        log::error("[xlings:subos] '{}' not found", target);
        return 1;
    }
    std::println("[xlings:subos] info for '{}':", si->name);
    std::println("  active: {}", si->isActive);
    std::println("  dir:    {}", si->dir.string());
    std::println("  tools:  {}", si->toolCount);
    return 0;
}

export int run(int argc, char* argv[]) {
    if (argc < 3) return run_list_();

    std::string sub = argv[2];
    if (sub == "ls") sub = "list";
    if (sub == "rm") sub = "remove";
    if (sub == "i")  sub = "info";

    if (sub == "new") {
        if (argc < 4) { log::error("usage: xlings subos new <name>"); return 1; }
        return create(argv[3]);
    }
    if (sub == "use") {
        if (argc < 4) { log::error("usage: xlings subos use <name>"); return 1; }
        return use(argv[3]);
    }
    if (sub == "list")   return run_list_();
    if (sub == "remove") {
        if (argc < 4) { log::error("usage: xlings subos remove|rm <name>"); return 1; }
        return remove(argv[3]);
    }
    if (sub == "info")   return run_info_(argc > 3 ? argv[3] : "");

    log::error("[xlings:subos] unknown subcommand: {}", sub);
    log::error("usage: xlings subos <new|use|list|ls|remove|rm|info|i> [name]");
    return 1;
}

} // namespace xlings::subos
