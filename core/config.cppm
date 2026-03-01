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

export class Config {
public:
    struct PathInfo {
        std::filesystem::path homeDir;      // XLINGS_HOME (~/.xlings)
        std::filesystem::path dataDir;      // $homeDir/data - global shared
        std::filesystem::path subosDir;     // $homeDir/subos/<active>
        std::filesystem::path binDir;       // $subosDir/bin
        std::filesystem::path libDir;       // $subosDir/lib
        std::string           activeSubos;  // current active subos name
        bool                  selfContained = false;
    };

private:
    PathInfo paths_;
    std::string mirror_;
    std::string lang_;
    xvm::VersionDB versions_;
    xvm::Workspace workspace_;
    xvm::Workspace projectWorkspace_;  // from project .xlings.json
    bool hasProjectConfig_ = false;
    std::filesystem::path projectDir_;      // directory containing project .xlings.json
    std::vector<IndexRepo> indexRepos_;     // from config: index_repos

    Config() {
        namespace fs = std::filesystem;

        auto envHome = utils::get_env_or_default("XLINGS_HOME");
        if (!envHome.empty()) {
            paths_.homeDir = envHome;
        } else {
            auto exePath   = platform::get_executable_path();
            auto exeParent = exePath.parent_path();
            auto candidate = exeParent.parent_path();
            if (!exePath.empty() && fs::exists(candidate / "xim")) {
                paths_.homeDir       = candidate;
                paths_.selfContained = true;
            } else {
                paths_.homeDir = fs::path(platform::get_home_dir()) / ".xlings";
            }
        }

        std::string activeSubos = "default";
        auto configPath = paths_.homeDir / ".xlings.json";
        if (fs::exists(configPath)) {
            try {
                auto content = platform::read_file_to_string(configPath.string());
                auto json    = nlohmann::json::parse(content, nullptr, false);
                if (!json.is_discarded()) {
                    if (json.contains("activeSubos") && json["activeSubos"].is_string()) {
                        auto val = json["activeSubos"].get<std::string>();
                        if (!val.empty()) activeSubos = val;
                    }
                    if (json.contains("mirror") && json["mirror"].is_string())
                        mirror_ = json["mirror"].get<std::string>();
                    if (json.contains("lang") && json["lang"].is_string())
                        lang_ = json["lang"].get<std::string>();
                    // Load global versions
                    if (json.contains("versions") && json["versions"].is_object())
                        versions_ = xvm::versions_from_json(json["versions"]);
                    // Load index repos
                    if (json.contains("index_repos") && json["index_repos"].is_array()) {
                        for (auto it = json["index_repos"].begin(); it != json["index_repos"].end(); ++it) {
                            if (it->is_object() && it->contains("name") && it->contains("url")) {
                                IndexRepo repo;
                                repo.name = (*it)["name"].get<std::string>();
                                repo.url  = (*it)["url"].get<std::string>();
                                indexRepos_.push_back(std::move(repo));
                            }
                        }
                    }
                }
            } catch (...) {}
        }

        paths_.activeSubos = activeSubos;
        paths_.dataDir  = paths_.homeDir / "data";
        paths_.subosDir = paths_.homeDir / "subos" / activeSubos;
        paths_.binDir   = paths_.subosDir / "bin";
        paths_.libDir   = paths_.subosDir / "lib";

        // Load subos workspace
        auto subosConfigPath = paths_.subosDir / ".xlings.json";
        if (fs::exists(subosConfigPath)) {
            try {
                auto content = platform::read_file_to_string(subosConfigPath.string());
                auto json    = nlohmann::json::parse(content, nullptr, false);
                if (!json.is_discarded() && json.contains("workspace") && json["workspace"].is_object()) {
                    workspace_ = xvm::workspace_from_json(json["workspace"]);
                }
            } catch (...) {}
        }

        // Load project-level config (walk up from cwd)
        load_project_config_();
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
                            // Load project-level index_repos (override global)
                            if (json.contains("index_repos") && json["index_repos"].is_array()) {
                                indexRepos_.clear();
                                for (auto it = json["index_repos"].begin(); it != json["index_repos"].end(); ++it) {
                                    if (it->is_object() && it->contains("name") && it->contains("url")) {
                                        IndexRepo repo;
                                        repo.name = (*it)["name"].get<std::string>();
                                        repo.url  = (*it)["url"].get<std::string>();
                                        indexRepos_.push_back(std::move(repo));
                                    }
                                }
                            }
                            // Merge project-level versions into global (project takes priority)
                            if (json.contains("versions") && json["versions"].is_object()) {
                                auto projVersions = xvm::versions_from_json(json["versions"]);
                                for (auto& [target, info] : projVersions) {
                                    // Merge version entries (project supplements global)
                                    auto& globalInfo = versions_[target];
                                    if (globalInfo.type.empty() && !info.type.empty())
                                        globalInfo.type = info.type;
                                    if (globalInfo.filename.empty() && !info.filename.empty())
                                        globalInfo.filename = info.filename;
                                    for (auto& [ver, vdata] : info.versions) {
                                        globalInfo.versions[ver] = std::move(vdata);
                                    }
                                    for (auto& [name, vermap] : info.bindings) {
                                        for (auto& [ver, val] : vermap) {
                                            globalInfo.bindings[name][ver] = val;
                                        }
                                    }
                                }
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
    [[nodiscard]] static const PathInfo& paths() { return instance_().paths_; }
    [[nodiscard]] static const std::string& mirror() { return instance_().mirror_; }
    [[nodiscard]] static const std::string& lang() { return instance_().lang_; }

    [[nodiscard]] static const xvm::VersionDB& versions() { return instance_().versions_; }
    [[nodiscard]] static xvm::VersionDB& versions_mut() { return instance_().versions_; }

    // Effective data dir: project-local if project config exists, otherwise global
    [[nodiscard]] static std::filesystem::path effective_data_dir() {
        auto& self = instance_();
        if (self.hasProjectConfig_ && !self.projectDir_.empty()) {
            return self.projectDir_ / ".xlings" / "data";
        }
        return self.paths_.dataDir;
    }

    [[nodiscard]] static const std::vector<IndexRepo>& index_repos() {
        return instance_().indexRepos_;
    }

    [[nodiscard]] static const std::filesystem::path& project_dir() {
        return instance_().projectDir_;
    }

    // Get effective workspace: project overrides subos
    [[nodiscard]] static xvm::Workspace effective_workspace() {
        auto& self = instance_();
        xvm::Workspace ws = self.workspace_;
        if (self.hasProjectConfig_) {
            for (auto& [k, v] : self.projectWorkspace_) {
                ws[k] = v;
            }
        }
        return ws;
    }

    [[nodiscard]] static const xvm::Workspace& workspace() { return instance_().workspace_; }
    [[nodiscard]] static xvm::Workspace& workspace_mut() { return instance_().workspace_; }
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

        json["versions"] = xvm::versions_to_json(self.versions_);
        platform::write_string_to_file(configPath.string(), json.dump(2));
    }

    // Save current subos workspace (project-local if project config exists)
    static void save_workspace() {
        namespace fs = std::filesystem;
        auto& self = instance_();
        fs::path subosConfigPath;
        if (self.hasProjectConfig_ && !self.projectDir_.empty()) {
            // Project-local anonymous subos
            auto projSubosDir = self.projectDir_ / ".xlings" / "subos" / "_";
            fs::create_directories(projSubosDir);
            subosConfigPath = projSubosDir / ".xlings.json";
        } else {
            subosConfigPath = self.paths_.subosDir / ".xlings.json";
        }

        nlohmann::json json;
        if (fs::exists(subosConfigPath)) {
            try {
                auto content = platform::read_file_to_string(subosConfigPath.string());
                json = nlohmann::json::parse(content, nullptr, false);
                if (json.is_discarded()) json = nlohmann::json::object();
            } catch (...) { json = nlohmann::json::object(); }
        }

        json["workspace"] = xvm::workspace_to_json(self.workspace_);
        platform::write_string_to_file(subosConfigPath.string(), json.dump(2));
    }

    static void print_paths() {
        auto& p = paths();
        std::println("XLINGS_HOME:     {}", p.homeDir.string());
        std::println("XLINGS_DATA:     {}", p.dataDir.string());
        std::println("XLINGS_SUBOS:    {}", p.subosDir.string());
        std::println("  activeSubos:   {}", p.activeSubos);
        std::println("  selfContained: {}", p.selfContained);
        std::println("  bin:           {}", p.binDir.string());
    }
};

} // namespace xlings
