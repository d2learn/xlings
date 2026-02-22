export module xlings.config;

import std;

import xlings.json;
import xlings.platform;
import xlings.utils;

namespace xlings {

export struct Info {
    static constexpr std::string_view VERSION = "0.0.5";
    static constexpr std::string_view REPO = "https://github.com/d2learn/xlings";
};

export class Config {
public:
    struct PathInfo {
        std::filesystem::path homeDir;   // ~/.xlings
        std::filesystem::path dataDir;   // ~/.xlings/data (configurable via .xlings.json)
        std::filesystem::path binDir;    // $dataDir/bin
        std::filesystem::path libDir;    // $dataDir/lib
    };

private:
    PathInfo paths_;
    std::string mirror_;
    std::string lang_;

    Config() {
        auto envHome = utils::get_env_or_default("XLINGS_HOME");
        if (!envHome.empty()) {
            paths_.homeDir = envHome;
        } else {
            paths_.homeDir = std::filesystem::path(platform::get_home_dir()) / ".xlings";
        }

        std::string customData;
        auto configPath = paths_.homeDir / ".xlings.json";
        if (std::filesystem::exists(configPath)) {
            try {
                auto content = utils::read_file_to_string(configPath.string());
                auto json = nlohmann::json::parse(content, nullptr, false);
                if (!json.is_discarded() && json.contains("data") && json["data"].is_string())
                    customData = json["data"].get<std::string>();
            } catch (...) {}
        }

        auto envData = utils::get_env_or_default("XLINGS_DATA");
        if (!envData.empty()) {
            paths_.dataDir = envData;
        } else if (!customData.empty()) {
            paths_.dataDir = customData;
        } else {
            paths_.dataDir = paths_.homeDir / "data";
        }

        paths_.binDir = paths_.dataDir / "bin";
        paths_.libDir = paths_.dataDir / "lib";
    }

    static Config& instance_() {
        static Config inst;
        return inst;
    }

public:
    [[nodiscard]] static const PathInfo& paths() { return instance_().paths_; }
    [[nodiscard]] static const std::string& mirror() { return instance_().mirror_; }
    [[nodiscard]] static const std::string& lang() { return instance_().lang_; }

    static void print_paths() {
        auto& p = paths();
        std::println("XLINGS_HOME: {}", p.homeDir.string());
        std::println("XLINGS_DATA: {}", p.dataDir.string());
        std::println("  bin:       {}", p.binDir.string());
        std::println("  lib:       {}", p.libDir.string());
    }
};

} // namespace xlings
