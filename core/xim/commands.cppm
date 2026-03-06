export module xlings.xim.commands;

import std;
import xlings.xim.types;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;
import mcpplibs.xpkg.loader;
import xlings.xim.catalog;
import xlings.xim.repo;
import xlings.xim.resolver;
import xlings.xim.downloader;
import xlings.xim.installer;
import xlings.log;
import xlings.config;
import xlings.ui;
import xlings.i18n;
import xlings.platform;
import xlings.xvm.db;
import xlings.xvm.commands;

namespace xpkg = mcpplibs::xpkg;

export namespace xlings::xim {

// Shared IndexManager instance (lazy-initialized)
PackageCatalog& get_catalog() {
    static PackageCatalog mgr;
    static bool initialized = false;
    if (!initialized) {
        auto result = mgr.rebuild();
        if (!result) {
            log::error("failed to build catalog: {}", result.error());
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

// Forward declaration for deferred install request processing
int cmd_remove(const std::string& target);

// === install command ===
int cmd_install(std::span<const std::string> targets, bool yes, bool noDeps) {
    auto& catalog = get_catalog();
    if (!catalog.is_loaded()) {
        log::info("package index not available, updating...");
        sync_all_repos(true);
        auto rebuildResult = catalog.rebuild();
        if (!rebuildResult || !catalog.is_loaded()) {
            log::error("package index not available");
            return 1;
        }
    }

    auto platform = detect_platform();
    std::vector<std::string> targetVec(targets.begin(), targets.end());
    std::vector<PackageMatch> requestedMatches;

    for (auto& target : targetVec) {
        auto match = catalog.resolve_target(target, platform);
        if (!match) {
            // Ambiguous matches: show error directly, don't fall through to fuzzy
            if (match.error().contains("ambiguous")) {
                log::error("{}", match.error());
                return 1;
            }
            // Try fuzzy search for suggestions
            auto fuzzy = catalog.search(target, platform);
            if (fuzzy.empty()) {
                log::error("{}", match.error());
                return 1;
            }
            if (fuzzy.size() > 5) fuzzy.resize(5);

            if (fuzzy.size() == 1) {
                // Single fuzzy match — use it directly
                match = fuzzy.front();
            } else if (yes) {
                // -y mode: auto-select first match
                match = fuzzy.front();
            } else {
                // Interactive selection
                std::println("did you mean one of these?");
                for (std::size_t i = 0; i < fuzzy.size(); ++i) {
                    std::println("  {}. {}@{}", i + 1,
                                 fuzzy[i].canonicalName, fuzzy[i].version);
                }
                std::print("select [1-{}] or 0 to cancel: ", fuzzy.size());
                std::cout.flush();
                std::string input;
                std::getline(std::cin, input);
                int choice = 0;
                try { choice = std::stoi(input); } catch (...) {}
                if (choice < 1 || choice > static_cast<int>(fuzzy.size())) {
                    std::println("cancelled");
                    return 0;
                }
                match = fuzzy[static_cast<std::size_t>(choice - 1)];
            }
            // Update target so dependency resolution uses the resolved name
            target = match->canonicalName;
            if (!match->version.empty()) {
                target += "@" + match->version;
            }
        }
        requestedMatches.push_back(*match);
    }

    // Resolve dependencies
    auto planResult = resolve(catalog, targetVec, platform);
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

    auto plan_key = [](const PackageMatch& match) {
        return match.canonicalName + "@" + match.version;
    };

    std::unordered_map<std::string, bool> requestedAlreadyInstalled;
    for (auto& match : requestedMatches) {
        requestedAlreadyInstalled[plan_key(match)] = match.installed;
    }

    auto activate_requested_targets = [&]() {
        auto db = Config::versions();
        for (auto& match : requestedMatches) {
            auto active = xvm::get_active_version(Config::effective_workspace(), match.name);
            if (xvm::has_version(db, match.name, match.version) &&
                active != match.version) {
                auto useRet = xvm::cmd_use(match.name, match.version);
                if (useRet != 0) {
                    log::warn("failed to activate {}@{} in current subos",
                              match.name, match.version);
                }
            }
            std::println("version: {}", match.version);
            if (requestedAlreadyInstalled[plan_key(match)]) {
                std::println("{}@{} already installed", match.name, match.version);
            } else {
                std::println("{}@{} installed", match.name, match.version);
            }
        }
    };

    auto pending = plan.pending_count();
    if (pending == 0) {
        std::println("all packages already installed");
        activate_requested_targets();
        return 0;
    }

    // Show plan
    std::println("packages to install ({}):", pending);
    for (auto& node : plan.nodes) {
        if (!node.alreadyInstalled) {
            std::println("  {} {}", node.canonicalName,
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
    Installer installer(catalog);
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
        },
        // Process deferred pkgmanager.install()/remove() requests synchronously
        // between install and config hooks so config can access sub-dependencies
        [](const std::vector<mcpplibs::xpkg::InstallRequest>& reqs) {
            for (auto& req : reqs) {
                if (req.op == "install") {
                    log::info("installing sub-dependency: {}", req.target);
                    std::vector<std::string> subTargets = { req.target };
                    cmd_install(subTargets, /*yes=*/true, /*noDeps=*/false);
                } else if (req.op == "remove") {
                    log::info("removing sub-dependency: {}", req.target);
                    cmd_remove(req.target);
                }
            }
        });

    if (!result) {
        log::error("install failed: {}", result.error());
        return 1;
    }

    activate_requested_targets();
    std::println("\n{} package(s) installed successfully", pending);
    return 0;
}

// === remove command ===
int cmd_remove(const std::string& target) {
    auto& catalog = get_catalog();
    if (!catalog.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    Installer installer(catalog);
    auto result = installer.uninstall(target);
    if (!result) {
        log::error("uninstall failed: {}", result.error());
        return 1;
    }

    return 0;
}

// === search command ===
int cmd_search(const std::string& keyword) {
    auto& catalog = get_catalog();
    if (!catalog.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto results = catalog.search(keyword, detect_platform());
    if (results.empty()) {
        std::println("no packages found matching '{}'", keyword);
        return 0;
    }

    // Display results with descriptions
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& match : results) {
        auto pkg = catalog.load_package(match);
        std::string desc = pkg ? pkg->description : "";
        items.emplace_back(match.canonicalName, desc);
    }

    xlings::ui::print_search_results(items);
    return 0;
}

// === list command ===
int cmd_list(const std::string& filter) {
    auto& catalog = get_catalog();
    if (!catalog.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto results = catalog.search(filter.empty() ? "" : filter, detect_platform());

    // Filter to only installed packages
    std::vector<PackageMatch> installed;
    for (auto& match : results) {
        if (match.installed) installed.push_back(std::move(match));
    }

    if (installed.empty()) {
        std::println("no installed packages found");
        return 0;
    }

    for (auto& match : installed) {
        auto pkg = catalog.load_package(match);
        std::string desc = pkg ? std::string(pkg->description) : std::string{};
        std::println("  {}@{}  {}", match.canonicalName, match.version, desc);
    }
    std::println("\ntotal: {} installed", installed.size());
    return 0;
}

// === info command ===
int cmd_info(const std::string& target) {
    auto& catalog = get_catalog();
    if (!catalog.is_loaded()) {
        log::error("package index not available");
        return 1;
    }

    auto match = catalog.resolve_target(target, detect_platform());
    if (!match) {
        log::error("{}", match.error());
        return 1;
    }

    auto pkg = catalog.load_package(*match);
    if (!pkg) {
        log::error("failed to load package: {}", pkg.error());
        return 1;
    }

    std::println("name:        {}", match->canonicalName);
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

    std::println("installed:   {}", match->installed ? "yes" : "no");

    return 0;
}

// === add-xpkg command ===
int cmd_add_xpkg(const std::string& fileOrUrl) {
    namespace fs = std::filesystem;
    auto localRepoDir = Config::global_data_dir() / "xim-pkgindex-local";
    auto pkgsDir = localRepoDir / "pkgs";
    fs::path luaFile;

    if (fileOrUrl.starts_with("http://") || fileOrUrl.starts_with("https://")) {
        auto filename = fileOrUrl.substr(fileOrUrl.rfind('/') + 1);
        if (auto q = filename.find('?'); q != std::string::npos)
            filename = filename.substr(0, q);
        // Download to temp location first, then move to letter subdir
        auto tmpFile = pkgsDir / filename;
        fs::create_directories(pkgsDir);
        if (fs::exists(tmpFile)) fs::remove(tmpFile);
        auto cmd = std::format("curl -fLs --retry 3 -o \"{}\" \"{}\"",
                               tmpFile.string(), fileOrUrl);
        if (platform::exec(cmd) != 0) {
            log::error("download failed: {}", fileOrUrl);
            return 1;
        }
        luaFile = tmpFile;
    } else {
        fs::path src(fileOrUrl);
        if (!src.is_absolute()) src = fs::current_path() / src;
        if (!fs::exists(src)) {
            log::error("file not found: {}", src.string());
            return 1;
        }
        luaFile = pkgsDir / src.filename();
        fs::create_directories(pkgsDir);
        fs::copy_file(src, luaFile, fs::copy_options::overwrite_existing);
    }

    // Validate xpkg file
    auto pkg = xpkg::load_package(luaFile);
    if (!pkg) {
        log::error("invalid xpkg: {}", pkg.error());
        fs::remove(luaFile);
        return 1;
    }

    // Move to letter subdirectory (build_index expects pkgs/<letter>/<name>.lua)
    auto name = pkg->name;
    if (name.empty()) name = luaFile.stem().string();
    std::string letter(1, std::tolower(static_cast<unsigned char>(name[0])));
    auto letterDir = pkgsDir / letter;
    fs::create_directories(letterDir);
    auto destFile = letterDir / luaFile.filename();
    if (destFile != luaFile) {
        fs::rename(luaFile, destFile);
        luaFile = destFile;
    }

    std::println("add xpkg - {}", luaFile.string());
    // Rebuild index so the new package is immediately available
    auto& catalog = get_catalog();
    // get_catalog() already triggers a rebuild on first call,
    // but the local repo was just modified so we need a fresh rebuild
    catalog.rebuild();
    return 0;
}

// === update command ===
int cmd_update(const std::string& target) {
    // Sync repos
    if (!sync_all_repos(true)) {
        log::error("failed to sync repositories");
        return 1;
    }

    // Force rebuild index (writes fresh cache)
    auto& catalog = get_catalog();
    auto result = catalog.rebuild(true);
    if (!result) {
        log::error("failed to rebuild catalog: {}", result.error());
        return 1;
    }

    std::println("index updated");

    if (!target.empty()) {
        // TODO: Upgrade specific package
        std::println("package upgrade not yet implemented");
    }

    return 0;
}

} // namespace xlings::xim
