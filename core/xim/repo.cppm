module;
#include <ctime>

export module xlings.xim.repo;

import std;
import xlings.log;
import xlings.platform;
import xlings.config;

export namespace xlings::xim {

// Sync a single git repository (clone or pull)
// Returns true on success
bool sync_repo(const std::filesystem::path& localDir,
               const std::string& url,
               bool force = false) {
    namespace fs = std::filesystem;

    if (!fs::exists(localDir / ".git")) {
        log::info("cloning index repo: {}", url);
        auto cmd = std::format("git clone --depth 1 \"{}\" \"{}\"",
                               url, localDir.string());
        auto [rc, output] = platform::run_command_capture(cmd);
        if (rc != 0) {
            log::error("git clone failed: {}", output);
            return false;
        }
        return true;
    }

    // Check throttle: skip if pulled within 7 days (unless forced)
    if (!force) {
        auto stampFile = localDir / ".xlings-sync-stamp";
        if (fs::exists(stampFile)) {
            auto lastWrite = fs::last_write_time(stampFile);
            auto now = fs::file_time_type::clock::now();
            auto age = std::chrono::duration_cast<std::chrono::hours>(now - lastWrite);
            if (age.count() < 7 * 24) {
                log::debug("skipping sync for {} (last sync {}h ago)",
                           localDir.string(), age.count());
                return true;
            }
        }
    }

    log::info("updating index repo: {}", localDir.filename().string());
    auto cmd = std::format("git -C \"{}\" pull --ff-only", localDir.string());
    auto [rc, output] = platform::run_command_capture(cmd);
    if (rc != 0) {
        log::warn("git pull failed, trying reset: {}", output);
        cmd = std::format("git -C \"{}\" fetch origin && git -C \"{}\" reset --hard origin/HEAD",
                          localDir.string(), localDir.string());
        auto [rc2, out2] = platform::run_command_capture(cmd);
        if (rc2 != 0) {
            log::error("git reset failed: {}", out2);
            return false;
        }
    }

    // Update stamp file
    auto stampFile = localDir / ".xlings-sync-stamp";
    std::ofstream(stampFile).put('.');
    return true;
}

// Sync all index repos from config's index_repos list
// Uses effective_data_dir() which respects project-level config
bool sync_all_repos(bool force = false) {
    namespace fs = std::filesystem;
    auto dataDir = Config::effective_data_dir();
    auto mirror = Config::mirror();

    // Read index_repos from config
    auto& repos = Config::index_repos();

    if (repos.empty()) {
        // Fallback: default repo
        auto mainRepoDir = dataDir / "xim-pkgindex";
        std::string mainUrl = "https://github.com/d2learn/xim-pkgindex.git";
        if (mirror == "CN") {
            mainUrl = "https://gitee.com/sunrisepeak/xim-pkgindex.git";
        }
        fs::create_directories(dataDir);
        return sync_repo(mainRepoDir, mainUrl, force);
    }

    fs::create_directories(dataDir);
    for (auto& repo : repos) {
        auto repoDir = dataDir / repo.name;
        std::string url = repo.url;
        // Apply CN mirror substitution if url is github
        if (mirror == "CN" && url.find("github.com") != std::string::npos) {
            // Replace github.com with gitee.com equivalent
            auto pos = url.find("github.com");
            url.replace(pos, 10, "gitee.com");
        }
        if (!sync_repo(repoDir, url, force)) {
            return false;
        }
    }
    return true;
}

// Get the main index repo directory path (respects project-local config)
std::filesystem::path main_repo_dir() {
    auto& repos = Config::index_repos();
    auto dataDir = Config::effective_data_dir();
    if (!repos.empty()) {
        return dataDir / repos[0].name;
    }
    return dataDir / "xim-pkgindex";
}

} // namespace xlings::xim
