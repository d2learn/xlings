export module xlings.xim.commands;

import std;
import xlings.xim.libxpkg.types.type;
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
import xlings.tinyhttps;
import xlings.xvm.db;
import xlings.xvm.commands;
import xlings.profile;

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
int cmd_install(std::span<const std::string> targets, bool yes, bool noDeps,
                bool forceGlobal = false) {
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
            // Explicit namespace (e.g. scode:linux-headers) -- don't fuzzy-match
            // across other namespaces, which can cause infinite recursion
            if (target.find(':') != std::string::npos) {
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
                // Single fuzzy match -- use it directly
                match = fuzzy.front();
            } else if (yes) {
                // -y mode: auto-select first match
                match = fuzzy.front();
            } else {
                // Interactive selection using themed UI
                std::vector<std::pair<std::string, std::string>> items;
                for (auto& f : fuzzy) {
                    items.emplace_back(
                        f.canonicalName + "@" + f.version,
                        ""
                    );
                }
                auto chosen = ui::select_package(items);
                if (!chosen) {
                    log::println("cancelled");
                    return 0;
                }
                // Find matching fuzzy result
                for (auto& f : fuzzy) {
                    if ((f.canonicalName + "@" + f.version) == *chosen) {
                        match = f;
                        break;
                    }
                }
                if (!match) {
                    log::println("cancelled");
                    return 0;
                }
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

    // -g: force all packages to install into global scope
    if (forceGlobal) {
        auto globalXpkgs = Config::global_data_dir() / "xpkgs";
        for (auto& node : plan.nodes) {
            node.scope = PackageScope::Global;
            node.storeRoot = globalXpkgs;
        }
        for (auto& match : requestedMatches) {
            match.scope = PackageScope::Global;
            match.storeRoot = globalXpkgs;
        }
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
            log::debug("version: {}", match.version);
            if (requestedAlreadyInstalled[plan_key(match)]) {
                log::debug("{}@{} already installed", match.canonicalName, match.version);
            } else {
                log::debug("{}@{} installed", match.canonicalName, match.version);
            }
        }
    };

    auto pending = plan.pending_count();
    auto allAlreadyInstalled = (pending == 0);
    if (allAlreadyInstalled) {
        log::println("all packages already installed");
    }

    // Show install plan with themed UI
    if (!allAlreadyInstalled) {
        std::vector<std::pair<std::string, std::string>> planItems;
        for (auto& node : plan.nodes) {
            if (!node.alreadyInstalled) {
                std::string nameVer = node.canonicalName;
                if (!node.version.empty()) nameVer += "@" + node.version;
                planItems.emplace_back(nameVer, "");
            }
        }
        ui::print_install_plan(planItems);
    }

    // Confirm with themed prompt
    if (!allAlreadyInstalled && !yes) {
        if (!ui::confirm("Proceed with installation?", true)) {
            log::println("cancelled");
            return 0;
        }
    }

    // Execute install
    Installer installer(catalog);
    DownloaderConfig dlConfig;
    auto mirror = Config::mirror();
    if (!mirror.empty()) dlConfig.preferredMirror = mirror;

    int successCount = 0;
    int failedCount = 0;

    auto result = installer.execute(plan, dlConfig,
        [&](const InstallStatus& status) {
            switch (status.phase) {
                case InstallPhase::Downloading:
                    break;  // TUI progress bar handles this
                case InstallPhase::Installing:
                    log::debug("[{}] installing...", status.name);
                    break;
                case InstallPhase::Configuring:
                    log::debug("[{}] configuring...", status.name);
                    break;
                case InstallPhase::Done:
                    log::debug("[{}] done", status.name);
                    ++successCount;
                    break;
                case InstallPhase::Failed:
                    log::error("[{}] failed: {}", status.name, status.message);
                    ++failedCount;
                    break;
                default:
                    break;
            }
        },
        // Process deferred pkgmanager.install()/remove() requests synchronously
        // between install and config hooks so config can access sub-dependencies
        [forceGlobal](const std::vector<mcpplibs::xpkg::InstallRequest>& reqs) {
            for (auto& req : reqs) {
                if (req.op == "install") {
                    log::debug("installing sub-dependency: {}", req.target);
                    std::vector<std::string> subTargets = { req.target };
                    cmd_install(subTargets, /*yes=*/true, /*noDeps=*/false, forceGlobal);
                } else if (req.op == "remove") {
                    log::debug("removing sub-dependency: {}", req.target);
                    cmd_remove(req.target);
                }
            }
        });

    if (!result) {
        log::error("install failed: {}", result.error());
        return 1;
    }

    activate_requested_targets();
    if (!allAlreadyInstalled) {
        ui::print_install_summary(successCount, failedCount);
    }
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

    ui::print_remove_summary(target);
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
        log::println("no packages found matching '{}'", keyword);
        return 0;
    }

    // Display results with descriptions (same style as list)
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& match : results) {
        auto pkg = catalog.load_package(match);
        std::string desc = pkg ? pkg->description : "";
        items.emplace_back(match.canonicalName, desc);
    }

    ui::print_styled_list("Search results:", items, true);
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
        log::println("no installed packages found");
        return 0;
    }

    // Use themed list display
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& match : installed) {
        auto pkg = catalog.load_package(match);
        std::string desc = pkg ? std::string(pkg->description) : std::string{};
        items.emplace_back(match.canonicalName + "@" + match.version, desc);
    }
    ui::print_styled_list("Installed packages:", items, true);
    log::println("total: {} installed", installed.size());
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

    // Build info fields
    std::vector<ui::InfoField> fields;
    fields.push_back({"description", std::string(pkg->description)});
    if (!pkg->homepage.empty())
        fields.push_back({"homepage", std::string(pkg->homepage)});
    if (!pkg->repo.empty())
        fields.push_back({"repo", std::string(pkg->repo)});
    if (!pkg->licenses.empty()) {
        std::string licStr;
        for (auto& l : pkg->licenses) {
            if (!licStr.empty()) licStr += " ";
            licStr += l;
        }
        fields.push_back({"licenses", licStr});
    }
    if (!pkg->categories.empty()) {
        std::string catStr;
        for (auto& c : pkg->categories) {
            if (!catStr.empty()) catStr += " ";
            catStr += c;
        }
        fields.push_back({"categories", catStr});
    }

    // Show platform versions
    auto platform = detect_platform();
    auto platformIt = pkg->xpm.entries.find(platform);
    if (platformIt != pkg->xpm.entries.end()) {
        std::string verStr;
        for (auto& [ver, res] : platformIt->second) {
            if (!verStr.empty()) verStr += ", ";
            verStr += ver;
            if (!res.ref.empty()) verStr += " -> " + res.ref;
        }
        fields.push_back({"versions", verStr});
    }

    // Show deps
    auto depsIt = pkg->xpm.deps.find(platform);
    if (depsIt != pkg->xpm.deps.end() && !depsIt->second.empty()) {
        std::string depStr;
        for (auto& d : depsIt->second) {
            if (!depStr.empty()) depStr += " ";
            depStr += d;
        }
        fields.push_back({"deps", depStr});
    }

    fields.push_back({"installed", match->installed ? "yes" : "no", match->installed});

    // Build installation detail section if installed
    std::vector<ui::InfoField> installFields;
    if (match->installed) {
        auto db = Config::versions();
        auto ws = Config::effective_workspace();
        auto target = match->name;

        auto activeVer = xvm::get_active_version(ws, target);
        if (!activeVer.empty()) {
            installFields.push_back({"active", activeVer, true});
        }

        auto allVers = xvm::get_all_versions(db, target);
        if (!allVers.empty()) {
            std::string verList;
            for (auto& v : allVers) {
                if (!verList.empty()) verList += ", ";
                verList += v;
                if (v == activeVer) verList += " *";
            }
            installFields.push_back({"versions", verList});
        }

        // xpkg store path
        auto storeName = package_store_name(match->namespaceName, match->name);
        auto storePath = match->storeRoot / storeName;
        installFields.push_back({"xpkg path", storePath.string()});

        // shim path
        auto binDir = Config::global_subos_bin_dir();
        auto shimPath = binDir / target;
        if (std::filesystem::exists(shimPath)) {
            installFields.push_back({"shim", shimPath.string()});
        }

        // bindings
        auto* vinfo = xvm::get_vinfo(db, target);
        if (vinfo && !vinfo->bindings.empty()) {
            std::string bindStr;
            for (auto& [bindName, verMap] : vinfo->bindings) {
                if (!bindStr.empty()) bindStr += ", ";
                bindStr += bindName;
                // Show the executable for active version if available
                if (!activeVer.empty()) {
                    auto vit = verMap.find(activeVer);
                    if (vit != verMap.end()) {
                        bindStr += " -> " + vit->second;
                    }
                }
            }
            installFields.push_back({"bindings", bindStr});
        }

        // subos references
        auto subosRefs = profile::find_subos_referencing(
            Config::paths().homeDir, target);
        if (!subosRefs.empty()) {
            std::string refStr;
            for (auto& s : subosRefs) {
                if (!refStr.empty()) refStr += ", ";
                refStr += s;
            }
            installFields.push_back({"subos", refStr});
        }
    }

    ui::print_info_panel(match->canonicalName, fields, installFields);
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
        if (!tinyhttps::fetch_to_file(fileOrUrl, tmpFile)) {
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

    log::println("add xpkg - {}", luaFile.string());
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

    log::println("index updated");

    if (!target.empty()) {
        // TODO: Upgrade specific package
        log::println("package upgrade not yet implemented");
    }

    return 0;
}

} // namespace xlings::xim
