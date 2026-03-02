export module xlings.config;

import std;

import xlings.json;
import xlings.platform;
import xlings.utils;
import xlings.xvm.types;
import xlings.xvm.db;

namespace xlings {

export struct Info {
    static constexpr std::string_view VERSION = "0.4.0";
    static constexpr std::string_view REPO = "https://github.com/d2learn/xlings";
};

export struct IndexRepo {
    std::string name;
    std::string url;
};

using MirrorServerMap = std::unordered_map<std::string, std::vector<std::string>>;

export enum class ProjectSubosMode {
    None,
    Anonymous,
    Named,
};

export class Config {
public:
    struct PathInfo {
        std::filesystem::path homeDir;      // XLINGS_HOME (~/.xlings)
        std::filesystem::path dataDir;      // $homeDir/data - global shared
        std::filesystem::path subosDir;     // effective subos dir
        std::filesystem::path binDir;       // $subosDir/bin
        std::filesystem::path libDir;       // $subosDir/lib
        std::string           activeSubos;  // effective active subos name
        bool                  selfContained = false;
    };

private:
    PathInfo paths_;
    std::string mirror_;
    std::string lang_;
    std::string globalActiveSubos_ = "default";
    xvm::VersionDB globalVersions_;
    xvm::VersionDB projectVersions_;
    xvm::Workspace globalWorkspace_;
    xvm::Workspace projectWorkspace_;       // from project .xlings.json
    xvm::Workspace projectSubosWorkspace_;  // from project-local subos file
    bool hasProjectConfig_ = false;
    std::filesystem::path projectDir_;      // directory containing project .xlings.json
    std::vector<IndexRepo> globalIndexRepos_;
    std::vector<IndexRepo> projectIndexRepos_;
    MirrorServerMap globalResourceServers_;
    MirrorServerMap projectResourceServers_;
    ProjectSubosMode projectSubosMode_ = ProjectSubosMode::None;
    std::string projectSubosName_;
    mutable std::mutex resourceServerMutex_;
    mutable std::unordered_map<std::string, std::string> selectedResourceServerCache_;

    static constexpr std::string_view DEFAULT_INDEX_REPO_NAME = "official";
    static constexpr std::string_view DEFAULT_INDEX_REPO_DIR = "xim-pkgindex";

    static std::vector<IndexRepo> default_global_index_repos_(const std::string& mirror) {
        std::string url = "https://github.com/d2learn/xim-pkgindex.git";
        if (mirror == "CN") {
            url = "https://gitee.com/sunrisepeak/xim-pkgindex.git";
        }
        return { IndexRepo{std::string(DEFAULT_INDEX_REPO_NAME), url} };
    }

    static MirrorServerMap default_resource_servers_() {
        return {
            { "GLOBAL", { "https://github.com/xlings-res" } },
            { "CN", { "https://gitcode.com/xlings-res" } },
        };
    }

    static void load_index_repos_from_json_(const nlohmann::json& json,
                                            std::vector<IndexRepo>& out) {
        out.clear();
        if (!json.contains("index_repos") || !json["index_repos"].is_array()) return;
        for (auto it = json["index_repos"].begin(); it != json["index_repos"].end(); ++it) {
            if (!it->is_object() || !it->contains("name") || !it->contains("url")) continue;
            IndexRepo repo;
            repo.name = (*it)["name"].get<std::string>();
            repo.url  = (*it)["url"].get<std::string>();
            if (!repo.name.empty() && !repo.url.empty()) out.push_back(std::move(repo));
        }
    }

    static std::vector<std::string> parse_server_list_(const nlohmann::json& value) {
        std::vector<std::string> servers;
        auto append_server = [&](std::string server) {
            server = utils::trim_string(server);
            while (server.size() > 1 && server.ends_with('/')) {
                server.pop_back();
            }
            if (server.empty()) return;
            if (std::ranges::find(servers, server) == servers.end()) {
                servers.push_back(std::move(server));
            }
        };

        if (value.is_string()) {
            append_server(value.get<std::string>());
        } else if (value.is_array()) {
            for (auto& item : value) {
                if (item.is_string()) append_server(item.get<std::string>());
            }
        }
        return servers;
    }

    static void merge_resource_servers_into_(MirrorServerMap& dst, const MirrorServerMap& src) {
        for (auto& [mirror, servers] : src) {
            if (!servers.empty()) dst[mirror] = servers;
        }
    }

    static void load_resource_servers_from_json_(const nlohmann::json& json,
                                                 MirrorServerMap& out) {
        out.clear();

        auto normalize_key = [](std::string key) {
            key = utils::trim_string(key);
            if (key == "default" || key == "DEFAULT" || key == "_default") return std::string("DEFAULT");
            return key;
        };

        auto load_object = [&](const nlohmann::json& obj) {
            if (!obj.is_object()) return;
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                auto servers = parse_server_list_(it.value());
                if (!servers.empty()) out[normalize_key(it.key())] = std::move(servers);
            }
        };

        auto load_default_list = [&](const nlohmann::json& value) {
            auto servers = parse_server_list_(value);
            if (!servers.empty()) out["DEFAULT"] = std::move(servers);
        };

        if (json.contains("XLINGS_RES")) {
            if (json["XLINGS_RES"].is_object()) {
                load_object(json["XLINGS_RES"]);
            } else {
                load_default_list(json["XLINGS_RES"]);
            }
        }

        // Backward compatibility for older config shapes.
        if (out.empty()) {
            if (json.contains("resource_server")) {
                load_default_list(json["resource_server"]);
            }

            if (json.contains("resource_servers")) {
                if (json["resource_servers"].is_object()) {
                    load_object(json["resource_servers"]);
                } else {
                    load_default_list(json["resource_servers"]);
                }
            }

            if (json.contains("res_servers")) {
                if (json["res_servers"].is_object()) {
                    load_object(json["res_servers"]);
                } else {
                    load_default_list(json["res_servers"]);
                }
            }

            if (json.contains("xim") && json["xim"].is_object()) {
                auto& xim = json["xim"];
                if (xim.contains("mirrors") && xim["mirrors"].is_object()) {
                    auto& mirrors = xim["mirrors"];
                    if (mirrors.contains("res-server")) {
                        load_object(mirrors["res-server"]);
                    }
                }
            }
        }
    }

    static xvm::Workspace load_workspace_from_file_(const std::filesystem::path& path) {
        namespace fs = std::filesystem;
        if (!fs::exists(path)) return {};
        try {
            auto content = platform::read_file_to_string(path.string());
            auto json = nlohmann::json::parse(content, nullptr, false);
            if (!json.is_discarded() && json.contains("workspace") && json["workspace"].is_object()) {
                return xvm::workspace_from_json(json["workspace"]);
            }
        } catch (...) {}
        return {};
    }

    static std::string load_project_subos_name_(const nlohmann::json& json) {
        if (json.contains("subos") && json["subos"].is_string()) {
            auto val = json["subos"].get<std::string>();
            if (!val.empty()) return val;
        }
        if (json.contains("projectSubos") && json["projectSubos"].is_string()) {
            auto val = json["projectSubos"].get<std::string>();
            if (!val.empty()) return val;
        }
        return {};
    }

    static void merge_versions_into_(xvm::VersionDB& dst, const xvm::VersionDB& src) {
        for (auto& [target, info] : src) {
            auto& dstInfo = dst[target];
            if (dstInfo.type.empty() && !info.type.empty())
                dstInfo.type = info.type;
            if (dstInfo.filename.empty() && !info.filename.empty())
                dstInfo.filename = info.filename;
            for (auto& [ver, vdata] : info.versions) {
                dstInfo.versions[ver] = vdata;
            }
            for (auto& [name, vermap] : info.bindings) {
                for (auto& [ver, value] : vermap) {
                    dstInfo.bindings[name][ver] = value;
                }
            }
        }
    }

    static void merge_workspace_into_(xvm::Workspace& dst, const xvm::Workspace& src) {
        for (auto& [target, version] : src) {
            dst[target] = version;
        }
    }

    static std::string effective_mirror_name_(std::string_view mirror,
                                              std::string_view fallback) {
        auto name = std::string(mirror.empty() ? fallback : mirror);
        if (name.empty()) return "GLOBAL";
        return name;
    }

    static std::vector<std::string> workspace_targets_from_workspace_(const xvm::Workspace& ws) {
        std::vector<std::string> targets;
        targets.reserve(ws.size());
        for (auto& [name, version] : ws) {
            if (name.empty()) continue;
            if (version.empty()) {
                targets.push_back(name);
            } else {
                targets.push_back(name + "@" + version);
            }
        }
        return targets;
    }

    [[nodiscard]] std::filesystem::path project_data_dir_() const {
        return projectDir_.empty() ? std::filesystem::path{} : projectDir_ / ".xlings" / "data";
    }

    [[nodiscard]] std::filesystem::path project_subos_dir_() const {
        if (projectDir_.empty() || projectSubosName_.empty()) return {};
        return projectDir_ / ".xlings" / "subos" / projectSubosName_;
    }

    [[nodiscard]] static std::vector<std::string>
    lookup_resource_servers_in_(const MirrorServerMap& source, std::string_view mirror) {
        auto key = effective_mirror_name_(mirror, "GLOBAL");
        if (auto it = source.find(key); it != source.end() && !it->second.empty()) {
            return it->second;
        }
        if (auto it = source.find("DEFAULT"); it != source.end() && !it->second.empty()) {
            return it->second;
        }
        return {};
    }

    [[nodiscard]] std::vector<std::string>
    candidate_resource_servers_for_(std::string_view mirror) const {
        auto key = effective_mirror_name_(mirror, mirror_);

        auto project = lookup_resource_servers_in_(projectResourceServers_, key);
        if (!project.empty()) return project;

        auto global = lookup_resource_servers_in_(globalResourceServers_, key);
        if (!global.empty()) return global;

        auto defaults = lookup_resource_servers_in_(default_resource_servers_(), key);
        if (!defaults.empty()) return defaults;

        auto fallback = lookup_resource_servers_in_(default_resource_servers_(), "GLOBAL");
        if (!fallback.empty()) return fallback;
        return {};
    }

    static double probe_resource_server_latency_(const std::string& server) {
#if defined(_WIN32)
        constexpr auto kNullDevice = "NUL";
#else
        constexpr auto kNullDevice = "/dev/null";
#endif
        auto cmd = std::format(
            "curl -sS -I -L -o {} --connect-timeout 1 --max-time 2 -w \"%{{time_connect}}\" \"{}\"",
            kNullDevice,
            server);
        auto [rc, out] = platform::run_command_capture(cmd);
        if (rc != 0) return std::numeric_limits<double>::infinity();

        auto latencyText = utils::trim_string(out);
        if (latencyText.empty()) return std::numeric_limits<double>::infinity();

        try {
            return std::stod(latencyText);
        } catch (...) {
            return std::numeric_limits<double>::infinity();
        }
    }

    [[nodiscard]] std::string selected_resource_server_for_(std::string_view mirror) const {
        auto key = effective_mirror_name_(mirror, mirror_);
        {
            std::scoped_lock lock(resourceServerMutex_);
            if (auto it = selectedResourceServerCache_.find(key);
                it != selectedResourceServerCache_.end()) {
                return it->second;
            }
        }

        auto candidates = candidate_resource_servers_for_(key);
        if (candidates.empty()) return {};

        auto selected = candidates.front();
        if (candidates.size() > 1) {
            double bestLatency = std::numeric_limits<double>::infinity();
            for (auto& candidate : candidates) {
                auto latency = probe_resource_server_latency_(candidate);
                if (latency < bestLatency) {
                    bestLatency = latency;
                    selected = candidate;
                }
                if (std::isfinite(latency) && latency <= 0.1) {
                    selected = candidate;
                    break;
                }
            }
        }

        {
            std::scoped_lock lock(resourceServerMutex_);
            selectedResourceServerCache_[key] = selected;
        }
        return selected;
    }

    void update_effective_paths_() {
        paths_.activeSubos = globalActiveSubos_;
        paths_.subosDir = paths_.homeDir / "subos" / globalActiveSubos_;
        if (projectSubosMode_ == ProjectSubosMode::Named && !projectSubosName_.empty()) {
            paths_.activeSubos = projectSubosName_;
            paths_.subosDir = project_subos_dir_();
        }
        paths_.binDir = paths_.subosDir / "bin";
        paths_.libDir = paths_.subosDir / "lib";
    }

    Config() {
        namespace fs = std::filesystem;

        auto envHome = utils::get_env_or_default("XLINGS_HOME");
        if (!envHome.empty()) {
            paths_.homeDir = envHome;
        } else {
            auto exePath   = platform::get_executable_path();
            auto exeParent = exePath.parent_path();
            auto candidate = exeParent.parent_path();
            auto hasRootConfig = !exePath.empty() && fs::exists(candidate / ".xlings.json");
#ifdef _WIN32
            auto hasRootBin = !exePath.empty() && fs::exists(candidate / "bin" / "xlings.exe");
#else
            auto hasRootBin = !exePath.empty() && fs::exists(candidate / "bin" / "xlings");
#endif
            if (hasRootConfig && hasRootBin) {
                paths_.homeDir       = candidate;
                paths_.selfContained = true;
            } else {
                paths_.homeDir = fs::path(platform::get_home_dir()) / ".xlings";
            }
        }

        auto configPath = paths_.homeDir / ".xlings.json";
        if (fs::exists(configPath)) {
            try {
                auto content = platform::read_file_to_string(configPath.string());
                auto json    = nlohmann::json::parse(content, nullptr, false);
                if (!json.is_discarded()) {
                    if (json.contains("activeSubos") && json["activeSubos"].is_string()) {
                        auto val = json["activeSubos"].get<std::string>();
                        if (!val.empty()) globalActiveSubos_ = val;
                    }
                    if (json.contains("mirror") && json["mirror"].is_string())
                        mirror_ = json["mirror"].get<std::string>();
                    if (json.contains("lang") && json["lang"].is_string())
                        lang_ = json["lang"].get<std::string>();
                    // Load global versions
                    if (json.contains("versions") && json["versions"].is_object())
                        globalVersions_ = xvm::versions_from_json(json["versions"]);
                    load_index_repos_from_json_(json, globalIndexRepos_);
                    load_resource_servers_from_json_(json, globalResourceServers_);
                }
            } catch (...) {}
        }
        if (globalIndexRepos_.empty()) {
            globalIndexRepos_ = default_global_index_repos_(mirror_);
        }

        paths_.dataDir  = paths_.homeDir / "data";
        update_effective_paths_();

        // Load subos workspace
        auto subosConfigPath = paths_.homeDir / "subos" / globalActiveSubos_ / ".xlings.json";
        globalWorkspace_ = load_workspace_from_file_(subosConfigPath);

        // Load project-level config (walk up from cwd)
        load_project_config_();
        update_effective_paths_();
    }

    void load_project_config_() {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto cwd = fs::current_path(ec);
        if (ec) return;

        auto homeNorm = fs::weakly_canonical(paths_.homeDir, ec);

        fs::path cur = cwd;
        while (!cur.empty()) {
            auto cfg = cur / ".xlings.json";
            if (fs::exists(cfg, ec) && fs::is_regular_file(cfg, ec)) {
                auto curNorm = fs::weakly_canonical(cur, ec);
                if (curNorm != homeNorm) {
                    try {
                        auto content = platform::read_file_to_string(cfg.string());
                        auto json = nlohmann::json::parse(content, nullptr, false);
                        if (!json.is_discarded()) {
                            projectDir_ = cur;
                            hasProjectConfig_ = true;
                            // Project-level mirror/lang override global
                            if (json.contains("mirror") && json["mirror"].is_string())
                                mirror_ = json["mirror"].get<std::string>();
                            if (json.contains("lang") && json["lang"].is_string())
                                lang_ = json["lang"].get<std::string>();
                            if (json.contains("workspace") && json["workspace"].is_object()) {
                                projectWorkspace_ = xvm::workspace_from_json(json["workspace"]);
                            }
                            load_index_repos_from_json_(json, projectIndexRepos_);
                            load_resource_servers_from_json_(json, projectResourceServers_);
                            if (json.contains("versions") && json["versions"].is_object()) {
                                projectVersions_ = xvm::versions_from_json(json["versions"]);
                            }
                            projectSubosName_ = load_project_subos_name_(json);
                            if (!projectSubosName_.empty()) {
                                projectSubosMode_ = ProjectSubosMode::Named;
                                projectSubosWorkspace_ =
                                    load_workspace_from_file_(project_subos_dir_() / ".xlings.json");
                            } else {
                                projectSubosMode_ = ProjectSubosMode::Anonymous;
                            }
                        }
                    } catch (...) {}
                    return;
                }
            }
            auto parent = cur.parent_path();
            if (parent == cur) break;
            cur = parent;
        }
    }

    static Config& instance_() {
        static Config inst;
        return inst;
    }

public:
    [[nodiscard]] static std::vector<std::string> workspace_install_targets(const xvm::Workspace& ws) {
        return workspace_targets_from_workspace_(ws);
    }

    [[nodiscard]] static xvm::VersionDB merged_versions(const xvm::VersionDB& globalVersions,
                                                        const xvm::VersionDB& projectVersions) {
        auto db = globalVersions;
        merge_versions_into_(db, projectVersions);
        return db;
    }

    [[nodiscard]] static xvm::Workspace merged_workspace(const xvm::Workspace& globalWorkspace,
                                                         const xvm::Workspace& projectWorkspace,
                                                         const xvm::Workspace& projectSubosWorkspace,
                                                         ProjectSubosMode mode) {
        if (mode == ProjectSubosMode::Named) {
            auto ws = projectWorkspace;
            merge_workspace_into_(ws, projectSubosWorkspace);
            return ws;
        }

        if (mode == ProjectSubosMode::Anonymous) {
            auto ws = globalWorkspace;
            merge_workspace_into_(ws, projectWorkspace);
            return ws;
        }

        return globalWorkspace;
    }

    [[nodiscard]] static const PathInfo& paths() { return instance_().paths_; }
    [[nodiscard]] static const std::string& mirror() { return instance_().mirror_; }
    [[nodiscard]] static const std::string& lang() { return instance_().lang_; }
    [[nodiscard]] static std::vector<std::string> resource_servers(std::string_view mirror = {}) {
        return instance_().candidate_resource_servers_for_(mirror);
    }
    [[nodiscard]] static std::string resource_server(std::string_view mirror = {}) {
        return instance_().selected_resource_server_for_(mirror);
    }

    [[nodiscard]] static xvm::VersionDB versions() {
        auto& self = instance_();
        return merged_versions(self.globalVersions_, self.projectVersions_);
    }
    [[nodiscard]] static xvm::VersionDB& versions_mut() {
        auto& self = instance_();
        return self.hasProjectConfig_ ? self.projectVersions_ : self.globalVersions_;
    }
    [[nodiscard]] static const xvm::VersionDB& global_versions() { return instance_().globalVersions_; }
    [[nodiscard]] static const xvm::VersionDB& project_versions() { return instance_().projectVersions_; }

    // Effective data dir: project-local if project config exists, otherwise global
    [[nodiscard]] static std::filesystem::path global_data_dir() {
        return instance_().paths_.dataDir;
    }

    [[nodiscard]] static std::filesystem::path project_data_dir() {
        return instance_().project_data_dir_();
    }

    [[nodiscard]] static std::filesystem::path effective_data_dir() {
        auto& self = instance_();
        if (self.hasProjectConfig_ && !self.projectDir_.empty() && !self.projectIndexRepos_.empty()) {
            return self.project_data_dir_();
        }
        return self.paths_.dataDir;
    }

    [[nodiscard]] static const std::vector<IndexRepo>& global_index_repos() {
        return instance_().globalIndexRepos_;
    }

    [[nodiscard]] static const std::vector<IndexRepo>& project_index_repos() {
        return instance_().projectIndexRepos_;
    }

    [[nodiscard]] static const std::vector<IndexRepo>& index_repos() {
        auto& self = instance_();
        if (self.hasProjectConfig_ && !self.projectIndexRepos_.empty()) {
            return self.projectIndexRepos_;
        }
        return self.globalIndexRepos_;
    }

    [[nodiscard]] static std::filesystem::path repo_dir_for(const IndexRepo& repo,
                                                            bool projectScope) {
        auto root = projectScope ? project_data_dir() : global_data_dir();
        auto dirName = repo.name == DEFAULT_INDEX_REPO_NAME ? DEFAULT_INDEX_REPO_DIR : repo.name;
        return root / dirName;
    }

    [[nodiscard]] static std::filesystem::path resolve_repo_source(const IndexRepo& repo,
                                                                   bool projectScope) {
        namespace fs = std::filesystem;
        auto value = repo.url;
        if (value.rfind("file://", 0) == 0) {
            value.erase(0, 7);
            return fs::path(value).lexically_normal();
        }
        if (value.find("://") != std::string::npos) {
            return {};
        }

        fs::path source(value);
        if (source.is_absolute()) return source.lexically_normal();

        auto base = projectScope ? project_dir() : paths().homeDir;
        if (base.empty()) return source.lexically_normal();
        return (base / source).lexically_normal();
    }

    [[nodiscard]] static bool is_local_repo_source(const IndexRepo& repo,
                                                   bool projectScope) {
        return !resolve_repo_source(repo, projectScope).empty();
    }

    [[nodiscard]] static const std::filesystem::path& project_dir() {
        return instance_().projectDir_;
    }

    [[nodiscard]] static ProjectSubosMode project_subos_mode() {
        return instance_().projectSubosMode_;
    }

    [[nodiscard]] static const std::string& project_subos_name() {
        return instance_().projectSubosName_;
    }

    // Get effective workspace: project overrides subos
    [[nodiscard]] static xvm::Workspace effective_workspace() {
        auto& self = instance_();
        if (!self.hasProjectConfig_) return self.globalWorkspace_;
        return merged_workspace(self.globalWorkspace_,
                                self.projectWorkspace_,
                                self.projectSubosWorkspace_,
                                self.projectSubosMode_);
    }

    [[nodiscard]] static const xvm::Workspace& workspace() {
        auto& self = instance_();
        if (!self.hasProjectConfig_) return self.globalWorkspace_;
        if (self.projectSubosMode_ == ProjectSubosMode::Named) return self.projectSubosWorkspace_;
        return self.projectWorkspace_;
    }
    [[nodiscard]] static xvm::Workspace& workspace_mut() {
        auto& self = instance_();
        if (!self.hasProjectConfig_) return self.globalWorkspace_;
        if (self.projectSubosMode_ == ProjectSubosMode::Named) return self.projectSubosWorkspace_;
        return self.projectWorkspace_;
    }
    [[nodiscard]] static bool has_project_config() { return instance_().hasProjectConfig_; }

    static std::filesystem::path subos_dir(const std::string& name) {
        return instance_().paths_.homeDir / "subos" / name;
    }

    static std::vector<std::string> list_subos_names() {
        std::vector<std::string> names;
        auto dir = instance_().paths_.homeDir / "subos";
        if (!std::filesystem::exists(dir)) return names;
        for (auto& entry : platform::dir_entries(dir)) {
            if (entry.is_directory()) {
                auto name = entry.path().filename().string();
                if (name != "current") names.push_back(name);
            }
        }
        std::ranges::sort(names);
        return names;
    }

    // Save .xlings.json versions section (project-local if project config exists)
    static void save_versions() {
        namespace fs = std::filesystem;
        auto& self = instance_();
        auto configPath = (self.hasProjectConfig_ && !self.projectDir_.empty())
            ? self.projectDir_ / ".xlings.json"
            : self.paths_.homeDir / ".xlings.json";

        nlohmann::json json;
        if (fs::exists(configPath)) {
            try {
                auto content = platform::read_file_to_string(configPath.string());
                json = nlohmann::json::parse(content, nullptr, false);
                if (json.is_discarded()) json = nlohmann::json::object();
            } catch (...) { json = nlohmann::json::object(); }
        }

        auto& versions = self.hasProjectConfig_ ? self.projectVersions_ : self.globalVersions_;
        json["versions"] = xvm::versions_to_json(versions);
        platform::write_string_to_file(configPath.string(), json.dump(2));
    }

    // Save current subos workspace (project-local if project config exists)
    static void save_workspace() {
        namespace fs = std::filesystem;
        auto& self = instance_();
        fs::path subosConfigPath;
        if (self.hasProjectConfig_ && self.projectSubosMode_ == ProjectSubosMode::Named) {
            auto projSubosDir = self.project_subos_dir_();
            fs::create_directories(projSubosDir);
            fs::create_directories(projSubosDir / "bin");
            fs::create_directories(projSubosDir / "lib");
            fs::create_directories(projSubosDir / "usr");
            fs::create_directories(projSubosDir / "generations");
            subosConfigPath = projSubosDir / ".xlings.json";
        } else if (self.hasProjectConfig_ && !self.projectDir_.empty()) {
            subosConfigPath = self.projectDir_ / ".xlings.json";
        } else {
            subosConfigPath = self.paths_.homeDir / "subos" / self.globalActiveSubos_ / ".xlings.json";
        }

        nlohmann::json json;
        if (fs::exists(subosConfigPath)) {
            try {
                auto content = platform::read_file_to_string(subosConfigPath.string());
                json = nlohmann::json::parse(content, nullptr, false);
                if (json.is_discarded()) json = nlohmann::json::object();
            } catch (...) { json = nlohmann::json::object(); }
        }

        auto& workspace = self.hasProjectConfig_
            ? (self.projectSubosMode_ == ProjectSubosMode::Named ? self.projectSubosWorkspace_ : self.projectWorkspace_)
            : self.globalWorkspace_;
        json["workspace"] = xvm::workspace_to_json(workspace);
        platform::write_string_to_file(subosConfigPath.string(), json.dump(2));
    }

    static void print_paths() {
        auto& p = paths();
        std::println("XLINGS_HOME:     {}", p.homeDir.string());
        std::println("XLINGS_DATA:     {}", p.dataDir.string());
        if (has_project_config() && !project_index_repos().empty()) {
            std::println("XLINGS_DATA_PROJECT: {}", project_data_dir().string());
        }
        std::println("XLINGS_SUBOS:    {}", p.subosDir.string());
        std::println("  activeSubos:   {}", p.activeSubos);
        std::println("  selfContained: {}", p.selfContained);
        std::println("  bin:           {}", p.binDir.string());
    }
};

} // namespace xlings
