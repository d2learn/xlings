module;

#include <cstdio>

export module xlings.core.xim.downloader;

import std;
import xlings.core.xim.libxpkg.types.type;
import xlings.core.log;
import xlings.platform;
import xlings.core.config;
import xlings.libs.tinyhttps;
import xlings.runtime.cancellation;
// Re-export extract_archive so existing importers (installer) keep working.
export import xlings.core.xim.extract;

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
        log::debug("already cloned {}, pulling latest...", task.name);
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

    log::debug("cloning {} from {}", task.name, url);
    auto cmd = std::format(
        "git clone --depth 1 --recursive --quiet \"{}\" \"{}\"",
        url, destDir.string());
    auto rc = platform::exec(cmd);
    if (rc != 0) {
        result.error = std::format("git clone failed (rc={})", rc);
        return result;
    }

    result.success = true;
    return result;
}

// Download a single file using libcurl with real-time progress callback.
DownloadResult download_one(const DownloadTask& task,
                            std::function<void(double total, double now)> onProgress = nullptr,
                            CancellationToken* cancel = nullptr) {
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
        // When cancellable, run git clone as subprocess for kill support
        if (cancel) {
            namespace fs = std::filesystem;
            std::string repoName;
            auto lastSlashGit = task.url.rfind('/');
            if (lastSlashGit != std::string::npos) {
                repoName = task.url.substr(lastSlashGit + 1);
                if (repoName.ends_with(".git"))
                    repoName = repoName.substr(0, repoName.size() - 4);
            }
            if (repoName.empty()) repoName = task.name;
            auto destDir = task.destDir / repoName;
            result.localFile = destDir;
            auto cmd = std::format("git clone --depth 1 --recursive --quiet \"{}\" \"{}\"",
                                   task.url, destDir.string());
            auto h = platform::spawn_command(cmd);
            if (h.pid <= 0) { result.error = "failed to spawn git"; return result; }
            auto [code, output] = platform::wait_or_kill(
                h, cancel, std::chrono::minutes{10});
            if (cancel->is_paused() || cancel->is_cancelled()) { result.error = "cancelled"; return result; }
            result.success = (code == 0);
            if (!result.success) result.error = output;
            return result;
        }
        return git_clone_one(task);
    }

    log::debug("downloading {} from {}", task.name, task.url);

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

    // Use in-process tinyhttps for all downloads (streaming progress).
    // When a CancellationToken is available, wire isCancelled so ESC aborts.
    {
        tinyhttps::DownloadOptions opts;
        opts.destFile = destFile;
        opts.urls = std::move(urls);
        opts.retryCount = 3;
        opts.connectTimeoutSec = 30;
        opts.maxTimeSec = 600;
        opts.onProgress = onProgress;
        if (cancel) {
            opts.isCancelled = [cancel] { return cancel->is_paused() || cancel->is_cancelled(); };
        }

        auto dlResult = tinyhttps::download_file(opts);
        if (!dlResult.success) {
            result.error = dlResult.error;
            return result;
        }
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

// Per-task progress state, shared between download threads and the TUI refresh thread
struct TaskProgress {
    std::string name;
    double totalBytes { 0.0 };
    double downloadedBytes { 0.0 };
    bool started  { false };
    bool finished { false };
    bool success  { false };
};

// Callback for rendering download progress.
// Called from the TUI refresh thread (under mutex) every ~200ms.
// prevLines: number of lines from the previous frame (0 on first call or when
//            rewriting is unsupported). The renderer should move cursor up by
//            prevLines and overwrite in a single write to avoid flicker.
// Returns the number of terminal lines rendered (for the next cursor-up).
using DownloadProgressRenderer = std::function<int(
    std::span<const TaskProgress> state,
    std::size_t nameWidth,
    double elapsedSec,
    bool sizesReady,
    int prevLines)>;

// Download all tasks with limited concurrency, real-time per-task progress
std::vector<DownloadResult>
download_all(std::span<const DownloadTask> tasks,
             const DownloaderConfig& config,
             DownloadProgressRenderer onRender,
             std::function<void(std::string_view name, float progress)> onProgress,
             CancellationToken* cancel = nullptr) {

    if (tasks.empty()) return {};

    tinyhttps::global_init();

    std::vector<DownloadResult> results(tasks.size());
    std::mutex mutex;
    std::condition_variable cv;
    int activeCount = 0;
    int maxConcur = std::max(1, config.maxConcurrency);

    // Shared progress state for TUI
    std::vector<TaskProgress> progState(tasks.size());
    for (std::size_t i = 0; i < tasks.size(); ++i)
        progState[i].name = tasks[i].name;

    std::atomic<bool> allDone { false };
    std::atomic<bool> sizesReady { false };

    // Compute max name width for alignment
    std::size_t nameWidth = 20;
    for (auto& t : tasks) {
        if (t.name.size() > nameWidth) nameWidth = t.name.size();
    }
    nameWidth += 2; // padding

    // Background HEAD requests to pre-fetch Content-Length for byte-weighted progress.
    // Runs in parallel with downloads — does not block startup.
    std::jthread headThread([&]() {
        std::vector<std::jthread> headWorkers;
        headWorkers.reserve(tasks.size());
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            if (is_git_url(tasks[i].url)) continue;
            headWorkers.emplace_back([&, i]() {
                auto len = tinyhttps::query_content_length(tasks[i].url);
                if (len > 0) {
                    std::lock_guard lock(mutex);
                    // Only set if download hasn't already reported a size
                    if (progState[i].totalBytes <= 0.0)
                        progState[i].totalBytes = static_cast<double>(len);
                }
            });
        }
        // jthread destructors join all HEAD workers
        headWorkers.clear();
        sizesReady.store(true);
    });

    // TUI refresh thread: redraws progress every 200ms using FTXUI.
    // Uses relative cursor movement (\033[<N>A) to overwrite previous frame in-place.
    auto startTime = std::chrono::steady_clock::now();

    bool canRewrite = platform::supports_rewrite_output() && !platform::is_tui_mode();
    int lastLines = 0;  // lines rendered in previous frame (for cursor-up)

    std::jthread tuiThread([&](std::stop_token stoken) {
        if (!onRender) return;  // No renderer — skip TUI

        if (canRewrite) {
            // Hide cursor during download (CLI mode only)
            std::print("\033[?25l");
            std::fflush(stdout);
        }

        while (!stoken.stop_requested() && !allDone.load() &&
               !(cancel && (cancel->is_paused() || cancel->is_cancelled()))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto elapsedSec = std::chrono::duration<double>(elapsed).count();
            {
                std::lock_guard lock(mutex);
                lastLines = onRender(progState, nameWidth, elapsedSec,
                                     sizesReady.load(), canRewrite ? lastLines : 0);
            }
        }

        // Final render
        {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto elapsedSec = std::chrono::duration<double>(elapsed).count();
            std::lock_guard lock(mutex);
            onRender(progState, nameWidth, elapsedSec,
                     sizesReady.load(), canRewrite ? lastLines : 0);
        }
        if (canRewrite) {
            // Show cursor again
            std::print("\033[?25h");
            std::fflush(stdout);
        }
    });

    std::vector<std::jthread> threads;
    threads.reserve(tasks.size());

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        threads.emplace_back([&, i]() {
            // Wait for concurrency slot
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return activeCount < maxConcur || (cancel && (cancel->is_paused() || cancel->is_cancelled()));
                });
                if (cancel && (cancel->is_paused() || cancel->is_cancelled())) {
                    std::lock_guard lk(mutex);
                    results[i].name = tasks[i].name;
                    results[i].error = "cancelled";
                    progState[i].finished = true;
                    cv.notify_one();
                    return;
                }
                ++activeCount;
                progState[i].started = true;
            }

            if (onProgress) {
                onProgress(tasks[i].name, 0.0f);
            }

            // Per-task progress callback updates shared state
            auto taskProgress = [&](double total, double now) {
                std::lock_guard lock(mutex);
                progState[i].totalBytes = total;
                progState[i].downloadedBytes = now;
            };

            auto result = download_one(tasks[i], taskProgress, cancel);

            {
                std::lock_guard lock(mutex);
                progState[i].finished = true;
                progState[i].success = result.success;
                results[i] = std::move(result);
                --activeCount;
            }

            if (onProgress) {
                onProgress(tasks[i].name, results[i].success ? 1.0f : -1.0f);
            }

            cv.notify_one();
        });
    }

    // jthread destructor joins automatically
    threads.clear();
    allDone.store(true);

    // Stop TUI thread
    tuiThread.request_stop();
    tuiThread.join();

    tinyhttps::global_cleanup();
    return results;
}

// extract_archive lives in xlings.core.xim.extract (libarchive-backed).
// Re-exported above so existing importers keep working without changes.

} // namespace xlings::xim
