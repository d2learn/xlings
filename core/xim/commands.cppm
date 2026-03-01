export module xlings.xim.commands;

import std;
import xlings.xim.types;
import xlings.xim.index;
import xlings.xim.repo;
import xlings.xim.resolver;
import xlings.xim.downloader;
import xlings.xim.installer;
import xlings.log;
import xlings.config;
import xlings.ui;
import xlings.i18n;

export namespace xlings::xim {

// Shared IndexManager instance (lazy-initialized)
IndexManager& get_index() {
    static IndexManager mgr;
    static bool initialized = false;
    if (!initialized) {
        auto repoDir = xim::main_repo_dir();
        mgr.set_repo_dir(repoDir);
        auto result = mgr.rebuild();
        if (!result) {
            log::error("failed to build index: {}", result.error());
            log::info("try running: xlings update");
        }
        initialized = true;
    }
    return mgr;
}

std::string detect_platform() {
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

// === install command ===
int cmd_install(std::span<const std::string> targets, bool yes, bool noDeps) {
    auto& index = get_index();
    if (!index.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto platform = detect_platform();
    std::vector<std::string> targetVec(targets.begin(), targets.end());

    // Resolve dependencies
    auto planResult = resolve(index, targetVec, platform);
    if (!planResult) {
        log::error("dependency resolution failed: {}", planResult.error());
        return 1;
    }

    auto& plan = *planResult;
    if (plan.has_errors()) {
        for (auto& err : plan.errors) {
            log::error("{}", err);
        }
        return 1;
    }

    auto pending = plan.pending_count();
    if (pending == 0) {
        std::println("all packages already installed");
        return 0;
    }

    // Show plan
    std::println("packages to install ({}):", pending);
    for (auto& node : plan.nodes) {
        if (!node.alreadyInstalled) {
            std::println("  {} {}", node.name,
                         node.version.empty() ? "" : "@" + node.version);
        }
    }

    // Confirm
    if (!yes) {
        std::print("\nproceed? [Y/n] ");
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty() && input[0] != 'y' && input[0] != 'Y') {
            std::println("cancelled");
            return 0;
        }
    }

    // Execute install
    Installer installer(index);
    DownloaderConfig dlConfig;
    auto mirror = Config::mirror();
    if (!mirror.empty()) dlConfig.preferredMirror = mirror;

    auto result = installer.execute(plan, dlConfig,
        [](const InstallStatus& status) {
            switch (status.phase) {
                case InstallPhase::Downloading:
                    log::info("[{}] downloading...", status.name);
                    break;
                case InstallPhase::Installing:
                    log::info("[{}] installing...", status.name);
                    break;
                case InstallPhase::Configuring:
                    log::info("[{}] configuring...", status.name);
                    break;
                case InstallPhase::Done:
                    log::info("[{}] done", status.name);
                    break;
                case InstallPhase::Failed:
                    log::error("[{}] failed: {}", status.name, status.message);
                    break;
                default:
                    break;
            }
        });

    if (!result) {
        log::error("install failed: {}", result.error());
        return 1;
    }

    std::println("\n{} package(s) installed successfully", pending);
    return 0;
}

// === remove command ===
int cmd_remove(const std::string& target) {
    auto& index = get_index();
    if (!index.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto resolved = index.resolve(target);
    auto match = index.match_version(resolved);
    if (!match) {
        log::error("package '{}' not found", target);
        return 1;
    }

    Installer installer(index);
    auto result = installer.uninstall(*match);
    if (!result) {
        log::error("uninstall failed: {}", result.error());
        return 1;
    }

    return 0;
}

// === search command ===
int cmd_search(const std::string& keyword) {
    auto& index = get_index();
    if (!index.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto results = index.search(keyword);
    if (results.empty()) {
        std::println("no packages found matching '{}'", keyword);
        return 0;
    }

    // Display results with descriptions
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& name : results) {
        auto* entry = index.find_entry(name);
        std::string desc = entry ? entry->description : "";
        items.emplace_back(name, desc);
    }

    xlings::ui::print_search_results(items);
    return 0;
}

// === list command ===
int cmd_list(const std::string& filter) {
    auto& index = get_index();
    if (!index.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto names = filter.empty() ? index.all_names() : index.search(filter);

    if (names.empty()) {
        std::println("no packages found");
        return 0;
    }

    for (auto& name : names) {
        auto* entry = index.find_entry(name);
        std::string status = (entry && entry->installed) ? "[installed]" : "";
        std::string desc = entry ? std::string(entry->description) : std::string{};
        // Note: avoid width specifiers — GCC 15 modules bug with std::formatter
        if (status.empty())
            std::println("  {}  {}", name, desc);
        else
            std::println("  {}  {}  {}", name, status, desc);
    }
    std::println("\ntotal: {} packages", names.size());
    return 0;
}

// === info command ===
int cmd_info(const std::string& target) {
    auto& index = get_index();
    if (!index.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto resolved = index.resolve(target);
    auto match = index.match_version(resolved);
    if (!match) {
        log::error("package '{}' not found", target);
        return 1;
    }

    auto pkg = index.load_package(*match);
    if (!pkg) {
        log::error("failed to load package: {}", pkg.error());
        return 1;
    }

    std::println("name:        {}", pkg->name);
    std::println("description: {}", pkg->description);
    if (!pkg->homepage.empty())
        std::println("homepage:    {}", pkg->homepage);
    if (!pkg->repo.empty())
        std::println("repo:        {}", pkg->repo);
    if (!pkg->licenses.empty()) {
        std::print("licenses:    ");
        for (auto& l : pkg->licenses) std::print("{} ", l);
        std::println("");
    }
    if (!pkg->categories.empty()) {
        std::print("categories:  ");
        for (auto& c : pkg->categories) std::print("{} ", c);
        std::println("");
    }

    // Show platform versions
    auto platform = detect_platform();
    auto platformIt = pkg->xpm.entries.find(platform);
    if (platformIt != pkg->xpm.entries.end()) {
        std::println("versions ({}):", platform);
        for (auto& [ver, res] : platformIt->second) {
            std::println("  {}{}", ver,
                         res.ref.empty() ? "" : " -> " + res.ref);
        }
    }

    // Show deps
    auto depsIt = pkg->xpm.deps.find(platform);
    if (depsIt != pkg->xpm.deps.end() && !depsIt->second.empty()) {
        std::print("deps:        ");
        for (auto& d : depsIt->second) std::print("{} ", d);
        std::println("");
    }

    auto* entry = index.find_entry(*match);
    if (entry) {
        std::println("installed:   {}", entry->installed ? "yes" : "no");
    }

    return 0;
}

// === update command ===
int cmd_update(const std::string& target) {
    // Sync repos
    if (!sync_all_repos(true)) {
        log::error("failed to sync repositories");
        return 1;
    }

    // Rebuild index
    auto& index = get_index();
    auto repoDir = xim::main_repo_dir();
    index.set_repo_dir(repoDir);
    auto result = index.rebuild();
    if (!result) {
        log::error("failed to rebuild index: {}", result.error());
        return 1;
    }

    std::println("index updated: {} packages", index.size());

    if (!target.empty()) {
        // TODO: Upgrade specific package
        std::println("package upgrade not yet implemented");
    }

    return 0;
}

} // namespace xlings::xim
