module;
#include <ctime>

export module xlings.xim.repo;

import std;
import xlings.log;
import xlings.platform;
import xlings.config;

export namespace xlings::xim {

namespace detail_ {

bool ensure_local_repo_link_(const std::filesystem::path& localDir,
                             const std::filesystem::path& sourceDir) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(sourceDir, ec) || !fs::exists(sourceDir / "pkgs", ec)) {
        log::error("local index repo missing pkgs/: {}", sourceDir.string());
        return false;
    }

    if (fs::exists(localDir, ec) || fs::is_symlink(localDir, ec)) {
        auto canonicalLocal = fs::weakly_canonical(localDir, ec);
        ec.clear();
        auto canonicalSource = fs::weakly_canonical(sourceDir, ec);
        if (!ec && canonicalLocal == canonicalSource) {
            return true;
        }
        ec.clear();
        fs::remove_all(localDir, ec);
        if (ec) {
            log::error("failed to replace local index repo mapping {}: {}",
                       localDir.string(), ec.message());
            return false;
        }
    }

    fs::create_directories(localDir.parent_path(), ec);
    if (ec) {
        log::error("failed to create repo parent directory {}: {}",
                   localDir.parent_path().string(), ec.message());
        return false;
    }

#if defined(_WIN32)
    if (!platform::create_directory_link(localDir, sourceDir)) {
        log::error("failed to create directory link: {} -> {}",
                   localDir.string(), sourceDir.string());
        return false;
    }
#else
    fs::create_directory_symlink(sourceDir, localDir, ec);
    if (ec) {
        log::error("failed to create symlink: {} -> {} ({})",
                   localDir.string(), sourceDir.string(), ec.message());
        return false;
    }
#endif

    log::info("linked local index repo: {} -> {}", localDir.string(), sourceDir.string());
    return true;
}

}  // namespace detail_

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

// Sync all configured repos.
// Global repos live under XLINGS_HOME/data, project repos under project .xlings/data.
bool sync_all_repos(bool force = false) {
    namespace fs = std::filesystem;
    auto mirror = Config::mirror();

    auto syncRepos = [&](const std::vector<IndexRepo>& repos, bool projectScope) {
        auto rootDir = projectScope ? Config::project_data_dir() : Config::global_data_dir();
        if (rootDir.empty()) return true;
        fs::create_directories(rootDir);
        for (auto& repo : repos) {
            auto repoDir = Config::repo_dir_for(repo, projectScope);
            if (Config::is_local_repo_source(repo, projectScope)) {
                auto sourceDir = Config::resolve_repo_source(repo, projectScope);
                if (!detail_::ensure_local_repo_link_(repoDir, sourceDir)) {
                    return false;
                }
                continue;
            }

            std::string url = repo.url;
            if (mirror == "CN" && url.find("github.com") != std::string::npos) {
                auto pos = url.find("github.com");
                url.replace(pos, 10, "gitee.com");
            }
            if (!sync_repo(repoDir, url, force)) {
                return false;
            }
        }
        return true;
    };

    if (!syncRepos(Config::global_index_repos(), false)) return false;
    if (Config::has_project_config() && !Config::project_index_repos().empty()) {
        if (!syncRepos(Config::project_index_repos(), true)) return false;
    }
    return true;
}

// Get the main index repo directory path (always global)
std::filesystem::path main_repo_dir() {
    auto& repos = Config::global_index_repos();
    if (!repos.empty()) {
        return Config::repo_dir_for(repos[0], false);
    }
    return Config::global_data_dir() / "xim-pkgindex";
}

} // namespace xlings::xim
