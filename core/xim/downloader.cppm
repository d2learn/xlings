export module xlings.xim.downloader;

import std;
import xlings.xim.types;
import xlings.log;
import xlings.platform;
import xlings.config;

export namespace xlings::xim {

// Check if a URL is a git repository URL
bool is_git_url(const std::string& url) {
    return url.ends_with(".git");
}

// Clone a git repository
DownloadResult git_clone_one(const DownloadTask& task) {
    namespace fs = std::filesystem;

    DownloadResult result;
    result.name = task.name;

    // Ensure dest directory exists
    std::error_code ec;
    fs::create_directories(task.destDir, ec);
    if (ec) {
        result.error = std::format("failed to create directory {}: {}",
                                   task.destDir.string(), ec.message());
        return result;
    }

    // Derive directory name from URL: "https://github.com/user/repo.git" -> "repo"
    std::string url = task.url;
    std::string repoName;
    auto lastSlash = url.rfind('/');
    if (lastSlash != std::string::npos) {
        repoName = url.substr(lastSlash + 1);
        if (repoName.ends_with(".git")) {
            repoName = repoName.substr(0, repoName.size() - 4);
        }
    }
    if (repoName.empty()) repoName = task.name;

    auto destDir = task.destDir / repoName;
    result.localFile = destDir;

    // If already cloned, pull latest
    if (fs::exists(destDir / ".git")) {
        log::info("already cloned {}, pulling latest...", task.name);
        auto cmd = std::format("git -C \"{}\" pull --ff-only", destDir.string());
        auto rc = platform::exec(cmd);
        if (rc == 0) {
            result.success = true;
            return result;
        }
        // Pull failed, remove and re-clone
        log::warn("pull failed for {}, re-cloning...", task.name);
        fs::remove_all(destDir, ec);
    }

    log::info("cloning {} from {}", task.name, url);
    auto cmd = std::format(
        "git clone --depth 1 --recursive \"{}\" \"{}\"",
        url, destDir.string());
    auto rc = platform::exec(cmd);
    if (rc != 0) {
        result.error = std::format("git clone failed (rc={})", rc);
        return result;
    }

    result.success = true;
    return result;
}

// Download a single file using curl.
// showProgress: true  -> use curl -# (ASCII progress bar, only safe when one download at a time)
//               false -> use curl -s (silent, used for concurrent downloads)
// TODO: replace curl subprocess with libcurl for proper per-task progress callbacks
//       regardless of concurrency level
DownloadResult download_one(const DownloadTask& task, bool showProgress = false) {
    namespace fs = std::filesystem;

    DownloadResult result;
    result.name = task.name;

    // Ensure dest directory exists
    std::error_code ec;
    fs::create_directories(task.destDir, ec);
    if (ec) {
        result.error = std::format("failed to create directory {}: {}",
                                   task.destDir.string(), ec.message());
        return result;
    }

    // Git clone for .git URLs
    if (is_git_url(task.url)) {
        return git_clone_one(task);
    }

    // Extract filename from URL
    std::string url = task.url;
    std::string filename;
    auto lastSlash = url.rfind('/');
    if (lastSlash != std::string::npos) {
        filename = url.substr(lastSlash + 1);
        auto q = filename.find('?');
        if (q != std::string::npos) filename = filename.substr(0, q);
    }
    if (filename.empty()) filename = task.name + ".download";

    auto destFile = task.destDir / filename;
    result.localFile = destFile;

    // Skip if already downloaded and SHA matches
    if (fs::exists(destFile) && !task.sha256.empty()) {
        auto cmd = std::format("sha256sum \"{}\"", destFile.string());
        auto [rc, output] = platform::run_command_capture(cmd);
        if (rc == 0 && output.find(task.sha256) != std::string::npos) {
            log::debug("already downloaded: {}", destFile.string());
            result.success = true;
            return result;
        }
    }

    // Build ordered list of URLs to try (primary + fallbacks)
    std::vector<std::string> urls;
    urls.push_back(url);
    for (auto& fb : task.fallbackUrls) urls.push_back(fb);

    // Download with curl, trying each URL in order
    bool downloaded = false;
    for (auto& tryUrl : urls) {
        log::info("downloading {} from {}", task.name, tryUrl);
        const char* progressFlag = showProgress ? "-#" : "-s";
        auto cmd = std::format(
            "curl -fL{} --retry 3 --connect-timeout 30 --max-time 600 -o \"{}\" \"{}\"",
            progressFlag, destFile.string(), tryUrl);
        auto rc = platform::exec(cmd);
        if (rc == 0) {
            downloaded = true;
            break;
        }
        result.error = std::format("curl failed (rc={})", rc);
        if (&tryUrl != &urls.back()) {
            log::warn("download failed for {}, trying next server...", task.name);
        }
    }
    if (!downloaded) {
        return result;
    }

    // Verify SHA256 if provided
    if (!task.sha256.empty()) {
        auto shaCmd = std::format("sha256sum \"{}\"", destFile.string());
        auto [shaRc, shaOut] = platform::run_command_capture(shaCmd);
        if (shaRc != 0 || shaOut.find(task.sha256) == std::string::npos) {
            result.error = std::format("SHA256 mismatch for {}", task.name);
            fs::remove(destFile, ec);
            return result;
        }
    }

    result.success = true;
    return result;
}

// Download all tasks with limited concurrency using thread pool
std::vector<DownloadResult>
download_all(std::span<const DownloadTask> tasks,
             const DownloaderConfig& config,
             std::function<void(std::string_view name, float progress)> onProgress) {

    if (tasks.empty()) return {};

    std::vector<DownloadResult> results(tasks.size());
    std::mutex mutex;
    std::condition_variable cv;
    int activeCount = 0;
    int maxConcur = std::max(1, config.maxConcurrency);

    std::vector<std::jthread> threads;
    threads.reserve(tasks.size());

    // Show curl ASCII progress bar only when a single package is being downloaded.
    // With multiple concurrent downloads the progress bars would interleave on the
    // terminal and become unreadable, so we keep them silent in that case.
    // TODO: switch to libcurl for proper multi-task progress reporting.
    const bool showProgress = (tasks.size() == 1);
    if (!showProgress) {
        log::info("downloading {} packages concurrently, please wait...", tasks.size());
    }

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        threads.emplace_back([&, i]() {
            // Wait for concurrency slot
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return activeCount < maxConcur; });
                ++activeCount;
            }

            if (onProgress) {
                onProgress(tasks[i].name, 0.0f);
            }

            auto result = download_one(tasks[i], showProgress);

            if (onProgress) {
                onProgress(tasks[i].name, result.success ? 1.0f : -1.0f);
            }

            {
                std::lock_guard lock(mutex);
                results[i] = std::move(result);
                --activeCount;
            }
            cv.notify_one();
        });
    }

    // jthread destructor joins automatically
    threads.clear();
    return results;
}

// Extract an archive (tar.gz, tar.xz, tar.bz2, zip)
std::expected<std::filesystem::path, std::string>
extract_archive(const std::filesystem::path& archive,
                const std::filesystem::path& destDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(destDir, ec);

    auto ext = archive.extension().string();
    auto stem = archive.stem().string();
    std::string cmd;

    if (ext == ".gz" || ext == ".xz" || ext == ".bz2" || ext == ".tgz") {
        cmd = std::format("tar xf \"{}\" -C \"{}\"",
                          archive.string(), destDir.string());
    } else if (ext == ".zip") {
#ifdef _WIN32
        // Probe available tools in priority order:
        //   1. Windows built-in bsdtar (C:\Windows\System32\tar.exe, present since Win10 1803).
        //      Use the absolute path — not "where tar" — to avoid picking up GNU tar from
        //      MSYS2/Git-for-Windows which does NOT support ZIP (only bsdtar/libarchive does).
        //      Check existence with fs::exists, no shell round-trip needed.
        //   2. unzip (available with Git for Windows / MSYS2 toolchains)
        //   3. PowerShell Expand-Archive (fallback; uses & { } script-block to avoid
        //      cmd.exe double-quote stripping that causes "cmdlet not found" errors)
        const fs::path winTar = "C:\\Windows\\System32\\tar.exe";
        if (fs::exists(winTar)) {
            cmd = std::format("\"{}\" -xf \"{}\" -C \"{}\"",
                              winTar.string(), archive.string(), destDir.string());
        } else {
            auto [unzip_rc, _u] = platform::run_command_capture("where unzip");
            if (unzip_rc == 0) {
                cmd = std::format("unzip -o \"{}\" -d \"{}\"",
                                  archive.string(), destDir.string());
            } else {
                cmd = std::format(
                    "powershell -NoProfile -Command \"& {{Expand-Archive -LiteralPath '{}' -DestinationPath '{}' -Force}}\"",
                    archive.string(), destDir.string());
            }
        }
#else
        cmd = std::format("unzip -o \"{}\" -d \"{}\"",
                          archive.string(), destDir.string());
#endif
    } else if (stem.ends_with(".tar")) {
        cmd = std::format("tar xf \"{}\" -C \"{}\"",
                          archive.string(), destDir.string());
    } else {
        return std::unexpected(std::format("unsupported archive format: {}", ext));
    }

    auto [rc, output] = platform::run_command_capture(cmd);
    if (rc != 0) {
        return std::unexpected(std::format("extraction failed: {}", output));
    }

    return destDir;
}

} // namespace xlings::xim
