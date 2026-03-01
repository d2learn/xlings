export module xlings.xim.installer;

import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;
import xlings.xim.types;
import xlings.xim.index;
import xlings.xim.resolver;
import xlings.xim.downloader;
import xlings.log;
import xlings.platform;
import xlings.config;
import xlings.xvm.types;
import xlings.xvm.db;
import xlings.xvm.commands;

export namespace xlings::xim {

class Installer {
    IndexManager& index_;

public:
    explicit Installer(IndexManager& index) : index_(index) {}

    // Execute an install plan
    std::expected<void, std::string>
    execute(const InstallPlan& plan,
            const DownloaderConfig& dlConfig,
            std::function<void(const InstallStatus&)> onStatus) {

        if (plan.has_errors()) {
            return std::unexpected(
                std::format("plan has errors: {}", plan.errors[0]));
        }

        auto dataDir = Config::effective_data_dir();
        auto platform = detect_platform_();

        // Phase 1: Collect download tasks for non-installed packages
        std::vector<DownloadTask> dlTasks;
        for (auto& node : plan.nodes) {
            if (node.alreadyInstalled) continue;

            auto pkg = index_.load_package(node.name);
            if (!pkg) {
                log::warn("skipping {}: {}", node.name, pkg.error());
                continue;
            }

            // Find URL for the resolved version
            auto platformIt = pkg->xpm.entries.find(platform);
            if (platformIt == pkg->xpm.entries.end()) continue;

            std::string version = node.version;
            // Follow ref chain
            auto verIt = platformIt->second.find(version);
            if (verIt != platformIt->second.end() && !verIt->second.ref.empty()) {
                version = verIt->second.ref;
                verIt = platformIt->second.find(version);
            }

            if (verIt == platformIt->second.end()) continue;

            auto& res = verIt->second;
            if (res.url.empty() || res.url == "XLINGS_RES") continue;

            DownloadTask task;
            task.name = node.name;
            task.url = res.url;
            task.sha256 = res.sha256;
            task.destDir = dataDir / "xpkgs" / node.name / version;
            dlTasks.push_back(std::move(task));
        }

        // Download all
        if (!dlTasks.empty()) {
            log::info("downloading {} packages...", dlTasks.size());
            auto results = download_all(dlTasks, dlConfig,
                [&](std::string_view name, float progress) {
                    if (onStatus) {
                        InstallStatus status;
                        status.name = std::string(name);
                        status.phase = progress >= 0
                            ? InstallPhase::Downloading
                            : InstallPhase::Failed;
                        status.progress = std::max(0.0f, progress);
                        onStatus(status);
                    }
                });

            for (auto& r : results) {
                if (!r.success) {
                    log::error("download failed for {}: {}", r.name, r.error);
                }
            }
        }

        // Phase 2: Install each package in topological order
        for (auto& node : plan.nodes) {
            if (node.alreadyInstalled) {
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Done, 1.0f, "already installed" });
                }
                continue;
            }

            if (onStatus) {
                onStatus({ node.name, InstallPhase::Installing, 0.5f, "" });
            }

            // Create executor and run hooks
            auto execResult = mcpplibs::xpkg::create_executor(node.pkgFile);
            if (!execResult) {
                log::error("failed to create executor for {}: {}",
                           node.name, execResult.error());
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Failed, 0.0f,
                               execResult.error() });
                }
                continue;
            }

            auto& executor = *execResult;

            // Build execution context
            mcpplibs::xpkg::ExecutionContext ctx;
            ctx.pkg_name = node.name;
            ctx.version = node.version;
            ctx.platform = platform;
            ctx.install_dir = dataDir / "xpkgs" / node.name / node.version;
            ctx.install_file = node.pkgFile;
            ctx.bin_dir = Config::paths().binDir;
            ctx.run_dir = ctx.install_dir;
            ctx.xpkg_dir = node.pkgFile.parent_path();
            ctx.subos_sysrootdir = Config::paths().subosDir.string();

            // Check if already installed via hook
            if (executor.has_hook(mcpplibs::xpkg::HookType::Installed)) {
                auto hookResult = executor.check_installed(ctx);
                if (hookResult.success && !hookResult.version.empty()) {
                    log::info("{} already installed (version {})",
                              node.name, hookResult.version);
                    index_.mark_installed(node.name, true);
                    if (onStatus) {
                        onStatus({ node.name, InstallPhase::Done, 1.0f,
                                   "already installed" });
                    }
                    continue;
                }
            }

            // Run install hook
            if (executor.has_hook(mcpplibs::xpkg::HookType::Install)) {
                log::info("installing {}...", node.name);
                auto hookResult = executor.run_hook(
                    mcpplibs::xpkg::HookType::Install, ctx);
                if (!hookResult.success) {
                    log::error("install hook failed for {}: {}",
                               node.name, hookResult.error);
                    if (onStatus) {
                        onStatus({ node.name, InstallPhase::Failed, 0.0f,
                                   hookResult.error });
                    }
                    continue;
                }
            }

            // Run config hook
            if (executor.has_hook(mcpplibs::xpkg::HookType::Config)) {
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Configuring, 0.8f, "" });
                }
                auto hookResult = executor.run_hook(
                    mcpplibs::xpkg::HookType::Config, ctx);
                if (!hookResult.success) {
                    log::warn("config hook failed for {}: {}",
                              node.name, hookResult.error);
                }
            }

            index_.mark_installed(node.name, true);

            // Process xvm operations collected by Lua hooks
            auto xvm_ops = executor.xvm_operations();
            auto sysroot_include = Config::paths().subosDir / "usr" / "include";
            auto sysroot_lib = Config::paths().libDir;

            for (auto& op : xvm_ops) {
                if (op.op == "add") {
                    std::string ver = op.version.empty() ? node.version : op.version;
                    std::string p = op.bindir.empty()
                        ? (dataDir / "xpkgs" / node.name / node.version).string()
                        : op.bindir;
                    std::string type = op.type.empty() ? "program" : op.type;
                    xvm::add_version(Config::versions_mut(),
                                     op.name, ver, p, type, op.filename);
                } else if (op.op == "headers") {
                    xvm::install_headers(op.includedir, sysroot_include);
                    auto& vdata = Config::versions_mut()[node.name].versions[node.version];
                    vdata.includedir = op.includedir;
                }
            }
            Config::save_versions();

            if (onStatus) {
                onStatus({ node.name, InstallPhase::Done, 1.0f, "" });
            }
            log::info("{}@{} installed successfully", node.name, node.version);
        }

        return {};
    }

    // Uninstall a package
    std::expected<void, std::string>
    uninstall(const std::string& name) {
        auto* entry = index_.find_entry(name);
        if (!entry) {
            return std::unexpected(
                std::format("package '{}' not found", name));
        }

        auto execResult = mcpplibs::xpkg::create_executor(entry->path);
        if (!execResult) {
            return std::unexpected(execResult.error());
        }

        auto& executor = *execResult;
        auto platform = detect_platform_();

        mcpplibs::xpkg::ExecutionContext ctx;
        ctx.pkg_name = name;
        ctx.platform = platform;
        ctx.bin_dir = Config::paths().binDir;
        ctx.install_dir = Config::effective_data_dir() / "xpkgs" / name;
        ctx.xpkg_dir = entry->path.parent_path();
        ctx.subos_sysrootdir = Config::paths().subosDir.string();

        if (executor.has_hook(mcpplibs::xpkg::HookType::Uninstall)) {
            log::info("uninstalling {}...", name);
            auto result = executor.run_hook(
                mcpplibs::xpkg::HookType::Uninstall, ctx);
            if (!result.success) {
                return std::unexpected(
                    std::format("uninstall hook failed: {}", result.error));
            }
        }

        // Process xvm operations collected by uninstall hook
        auto xvm_ops = executor.xvm_operations();
        auto sysroot_include = Config::paths().subosDir / "usr" / "include";

        for (auto& op : xvm_ops) {
            if (op.op == "remove") {
                if (op.version.empty()) {
                    Config::versions_mut().erase(op.name);
                } else {
                    xvm::remove_version(Config::versions_mut(), op.name, op.version);
                }
            } else if (op.op == "remove_headers") {
                xvm::remove_headers(op.includedir, sysroot_include);
            }
        }
        Config::save_versions();

        // Clean up workspace entries that reference removed packages
        auto& ws = Config::workspace_mut();
        for (auto it = ws.begin(); it != ws.end(); ) {
            if (!xvm::has_target(Config::versions(), it->first)) {
                it = ws.erase(it);
            } else {
                ++it;
            }
        }
        Config::save_workspace();

        index_.mark_installed(name, false);
        log::info("{} uninstalled", name);
        return {};
    }

private:
    static std::string detect_platform_() {
        #if defined(__linux__)
            return "linux";
        #elif defined(__APPLE__)
            return "macosx";
        #elif defined(_WIN32)
            return "windows";
        #else
            return "unknown";
        #endif
    }
};

} // namespace xlings::xim
