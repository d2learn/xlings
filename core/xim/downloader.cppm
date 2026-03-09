module;

#include <cstdio>
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/color.hpp"

export module xlings.xim.downloader;

import std;
import xlings.xim.libxpkg.types.type;
import xlings.log;
import xlings.platform;
import xlings.config;
import xlings.tinyhttps;
import xlings.ui;

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

// Download a single file using libcurl with real-time progress callback.
DownloadResult download_one(const DownloadTask& task,
                            std::function<void(double total, double now)> onProgress = nullptr) {
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

    log::info("downloading {} from {}", task.name, task.url);

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

    // Download with libcurl
    tinyhttps::DownloadOptions opts;
    opts.destFile = destFile;
    opts.urls = std::move(urls);
    opts.retryCount = 3;
    opts.connectTimeoutSec = 30;
    opts.maxTimeSec = 600;
    opts.onProgress = onProgress;

    auto dlResult = tinyhttps::download_file(opts);
    if (!dlResult.success) {
        result.error = dlResult.error;
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

// Per-task progress state, shared between download threads and the TUI refresh thread
struct TaskProgress {
    std::string name;
    double totalBytes { 0.0 };
    double downloadedBytes { 0.0 };
    bool started  { false };
    bool finished { false };
    bool success  { false };
};

// Format ETA string from seconds
std::string format_eta_(int seconds) {
    if (seconds < 0) return "";
    if (seconds < 60) return std::to_string(seconds) + "s";
    int min = seconds / 60;
    int sec = seconds % 60;
    return std::to_string(min) + "m" + std::to_string(sec) + "s";
}

// Format speed in human-readable units
std::string format_speed_(double bytesPerSec) {
    if (bytesPerSec < 1024.0)
        return std::to_string(static_cast<int>(bytesPerSec)) + " B/s";
    if (bytesPerSec < 1024.0 * 1024.0) {
        int kb = static_cast<int>(bytesPerSec / 1024.0 * 10.0) ;
        return std::to_string(kb / 10) + "." + std::to_string(kb % 10) + " KB/s";
    }
    int mb = static_cast<int>(bytesPerSec / (1024.0 * 1024.0) * 10.0);
    return std::to_string(mb / 10) + "." + std::to_string(mb % 10) + " MB/s";
}

// Render progress using FTXUI themed elements.
// Layout matches install plan: "    icon name  status"
// Includes overall progress bar + ETA at the bottom.
void render_progress_(const std::vector<TaskProgress>& progState,
                      std::size_t nameWidth,
                      std::chrono::steady_clock::time_point startTime,
                      bool sizesReady) {
    using namespace ftxui;
    constexpr std::size_t statusWidth = 8;

    Elements rows;
    double totalBytes = 0.0;       // sum of all totalBytes (pre-fetched via HEAD)
    double totalDownloaded = 0.0;  // sum of all downloaded bytes

    for (auto& p : progState) {
        Element icon;
        Element nameEl;
        std::string statusStr;

        // Always count totalBytes (pre-fetched by HEAD before download starts)
        totalBytes += p.totalBytes;

        if (!p.started) {
            icon = text("    " + std::string(ui::theme::icon::pending) + " ")
                | color(ui::theme::dim_color());
            nameEl = ui::name_as_progress(p.name, 0.0f,
                ui::theme::dim_color(), ui::theme::border_color(), nameWidth, false);
            statusStr = "pending";
        } else if (!p.finished) {
            float pct = (p.totalBytes > 0)
                ? static_cast<float>(p.downloadedBytes / p.totalBytes)
                : 0.0f;
            totalDownloaded += p.downloadedBytes;
            icon = text("    " + std::string(ui::theme::icon::downloading) + " ")
                | color(ui::theme::cyan());
            nameEl = ui::name_as_progress(p.name, pct,
                ui::theme::cyan(), ui::theme::border_color(), nameWidth, true, true);
            if (pct > 0.0f) {
                int whole = static_cast<int>(pct * 100.0f);
                int frac = static_cast<int>(pct * 1000.0f) % 10;
                statusStr = std::to_string(whole) + "." + std::to_string(frac) + "%";
            } else {
                statusStr = "0.0%";
            }
        } else if (p.success) {
            totalDownloaded += p.totalBytes;
            icon = text("    " + std::string(ui::theme::icon::done) + " ")
                | color(ui::theme::green());
            nameEl = ui::name_as_progress(p.name, 1.0f,
                ui::theme::green(), ui::theme::green(), nameWidth, true);
            statusStr = "done";
        } else {
            totalDownloaded += p.totalBytes;
            icon = text("    " + std::string(ui::theme::icon::failed) + " ")
                | color(ui::theme::red()) | bold;
            nameEl = ui::name_as_progress(p.name, 1.0f,
                ui::theme::red(), ui::theme::red(), nameWidth, true);
            statusStr = "failed";
        }

        while (statusStr.size() < statusWidth) statusStr = " " + statusStr;

        auto statusEl = text(" " + statusStr);
        if (p.finished && p.success) statusEl = statusEl | color(ui::theme::green());
        else if (p.finished) statusEl = statusEl | color(ui::theme::red()) | bold;
        else if (!p.started) statusEl = statusEl | color(ui::theme::dim_color());
        else statusEl = statusEl | color(ui::theme::dim_color());

        rows.push_back(hbox({ icon, nameEl, statusEl }));
    }

    // Overall progress: byte-weighted once sizes are known via HEAD requests
    float overallPct = 0.0f;
    std::string speedStr;
    std::string etaStr;

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto elapsedSec = std::chrono::duration<double>(elapsed).count();

    if (sizesReady && totalBytes > 0.0) {
        overallPct = static_cast<float>(totalDownloaded / totalBytes);
        if (overallPct > 1.0f) overallPct = 1.0f;

        if (elapsedSec > 0.5 && totalDownloaded > 0.0) {
            double speed = totalDownloaded / elapsedSec;
            speedStr = "  " + format_speed_(speed);
        }

        if (overallPct > 0.01f && overallPct < 1.0f && elapsedSec > 1.0) {
            double speed = totalDownloaded / elapsedSec;
            if (speed > 0.0) {
                double remainingBytes = totalBytes - totalDownloaded;
                int remainingSec = static_cast<int>(remainingBytes / speed);
                etaStr = "  ETA " + format_eta_(remainingSec);
            }
        }
    }

    int pctWhole = static_cast<int>(overallPct * 100.0f);
    int pctFrac = static_cast<int>(overallPct * 1000.0f) % 10;
    std::string pctStr = std::to_string(pctWhole) + "." + std::to_string(pctFrac) + "%";

    rows.push_back(text(""));
    rows.push_back(hbox({
        text("  " + std::string(ui::theme::icon::arrow) + " ") | color(ui::theme::cyan()),
        gauge(overallPct) | size(WIDTH, EQUAL, 30) | color(ui::theme::cyan()),
        text("  " + pctStr) | bold | color(ui::theme::text_color()),
        text(speedStr) | color(ui::theme::cyan()),
        text(etaStr) | color(ui::theme::dim_color()),
    }));

    auto doc = vbox(std::move(rows));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);
    screen.Print();
    std::print("\n");
    std::fflush(stdout);
}

// Download all tasks with limited concurrency, real-time per-task progress
std::vector<DownloadResult>
download_all(std::span<const DownloadTask> tasks,
             const DownloaderConfig& config,
             std::function<void(std::string_view name, float progress)> onProgress) {

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
    // Cursor was saved by print_install_plan (\033[s) before package lines.
    // Progress rendering replaces those lines in-place.
    auto startTime = std::chrono::steady_clock::now();

    std::jthread tuiThread([&](std::stop_token stoken) {
        // Hide cursor during download
        std::print("\033[?25l");
        std::fflush(stdout);

        while (!stoken.stop_requested() && !allDone.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Restore to cursor saved by print_install_plan, clear from there
            std::print("\033[u\033[J");

            {
                std::lock_guard lock(mutex);
                render_progress_(progState, nameWidth, startTime, sizesReady.load());
            }
        }

        // Final render
        std::print("\033[u\033[J");
        {
            std::lock_guard lock(mutex);
            render_progress_(progState, nameWidth, startTime, sizesReady.load());
        }
        // Show cursor again
        std::print("\033[?25h");
        std::fflush(stdout);
    });

    std::vector<std::jthread> threads;
    threads.reserve(tasks.size());

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        threads.emplace_back([&, i]() {
            // Wait for concurrency slot
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return activeCount < maxConcur; });
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

            auto result = download_one(tasks[i], taskProgress);

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
        const fs::path winTar = "C:\\Windows\\System32\\tar.exe";
        if (fs::exists(winTar)) {
            cmd = std::format("{} -xf \"{}\" -C \"{}\"",
                              winTar.string(), archive.string(), destDir.string());
        } else {
            auto [unzip_rc, _u] = platform::run_command_capture("where unzip");
            if (unzip_rc == 0) {
                cmd = std::format("unzip -o \"{}\" -d \"{}\"",
                                  archive.string(), destDir.string());
            } else {
                cmd = std::format(
                    "powershell -NoProfile -Command \"Expand-Archive -LiteralPath '{}' -DestinationPath '{}' -Force\"",
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
