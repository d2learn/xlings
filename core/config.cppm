export module xlings.config;

import std;

import xlings.json;
import xlings.platform;
import xlings.utils;

namespace xlings {

export struct Info {
    static constexpr std::string_view VERSION = "0.2.0";
    static constexpr std::string_view REPO = "https://github.com/d2learn/xlings";
};

export class Config {
public:
    struct PathInfo {
        std::filesystem::path homeDir;      // XLINGS_HOME (~/.xlings)
        std::filesystem::path dataDir;      // XLINGS_DATA ($homeDir/data) - global shared
        std::filesystem::path subosDir;     // XLINGS_SUBOS ($homeDir/subos/<active>)
        std::filesystem::path binDir;       // $subosDir/bin
        std::filesystem::path libDir;       // $subosDir/lib
        std::string           activeSubos;  // current active subos name
        bool                  selfContained = false;
    };

private:
    PathInfo paths_;
    std::string mirror_;
    std::string lang_;

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
                auto content = utils::read_file_to_string(configPath.string());
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
                }
            } catch (...) {}
        }

        paths_.activeSubos = activeSubos;

        auto envData = utils::get_env_or_default("XLINGS_DATA");
        if (!envData.empty()) {
            paths_.dataDir = envData;
        } else {
            paths_.dataDir = paths_.homeDir / "data";
        }

        auto envSubos = utils::get_env_or_default("XLINGS_SUBOS");
        if (!envSubos.empty()) {
            paths_.subosDir = envSubos;
        } else {
            paths_.subosDir = paths_.homeDir / "subos" / activeSubos;
        }

        paths_.binDir = paths_.subosDir / "bin";
        paths_.libDir = paths_.subosDir / "lib";
    }

    static Config& instance_() {
        static Config inst;
        return inst;
    }

public:
    [[nodiscard]] static const PathInfo& paths() { return instance_().paths_; }
    [[nodiscard]] static const std::string& mirror() { return instance_().mirror_; }
    [[nodiscard]] static const std::string& lang() { return instance_().lang_; }

    static std::filesystem::path subos_dir(const std::string& name) {
        return instance_().paths_.homeDir / "subos" / name;
    }

    static std::vector<std::string> list_subos_names() {
        std::vector<std::string> names;
        auto dir = instance_().paths_.homeDir / "subos";
        if (!std::filesystem::exists(dir)) return names;
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory())
                names.push_back(entry.path().filename().string());
        }
        std::ranges::sort(names);
        return names;
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
