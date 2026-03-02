export module xlings.xim.installer;

import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;
import mcpplibs.capi.lua;
import xlings.xim.types;
import xlings.xim.index;
import xlings.xim.catalog;
import xlings.xim.resolver;
import xlings.xim.downloader;
import xlings.log;
import xlings.platform;
import xlings.config;
import xlings.json;
import xlings.xvm.types;
import xlings.xvm.db;
import xlings.xvm.commands;

export namespace xlings::xim {

namespace detail_ {

namespace lua = mcpplibs::capi::lua;

std::string effective_store_name_(std::string_view namespaceName, std::string_view name) {
    return package_store_name(namespaceName, name);
}

std::string effective_store_name_(const PlanNode& node) {
    return effective_store_name_(node.namespaceName, node.name);
}

std::string effective_store_name_(const PackageMatch& match) {
    return effective_store_name_(match.namespaceName, match.name);
}

std::string plan_key_(const PlanNode& node) {
    auto name = node.canonicalName.empty()
        ? canonical_package_name(node.namespaceName, node.name)
        : node.canonicalName;
    if (node.version.empty()) return name;
    return name + "@" + node.version;
}

std::filesystem::path data_root_for_(const std::filesystem::path& targetRoot) {
    if (targetRoot.filename() == "xpkgs") return targetRoot.parent_path();
    return targetRoot;
}

std::filesystem::path download_cache_dir_(const PlanNode& node,
                                          const std::filesystem::path& fallbackDataDir) {
    auto targetRoot = node.storeRoot.empty() ? (fallbackDataDir / "xpkgs") : node.storeRoot;
    auto dataRoot = data_root_for_(targetRoot);
    return dataRoot / "runtimedir" / "downloads" / effective_store_name_(node) / node.version;
}

std::filesystem::path extract_work_dir_(const PlanNode& node,
                                        const std::filesystem::path& fallbackDataDir) {
    auto targetRoot = node.storeRoot.empty() ? (fallbackDataDir / "xpkgs") : node.storeRoot;
    auto dataRoot = data_root_for_(targetRoot);
    return dataRoot / "runtimedir" / "extract" / effective_store_name_(node) / node.version;
}

bool is_archive_(const std::filesystem::path& path) {
    auto filename = path.filename().string();
    return filename.ends_with(".tar.gz")
        || filename.ends_with(".tar.xz")
        || filename.ends_with(".tar.bz2")
        || filename.ends_with(".tgz")
        || filename.ends_with(".zip");
}

std::string detect_arch_() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

std::string default_res_server_() {
    auto server = Config::resource_server();
    if (!server.empty()) return server;
    return "https://github.com/xlings-res";
}

std::string build_xlings_res_url_(std::string_view pkgName,
                                  std::string_view version,
                                  std::string_view platform) {
    auto ext = std::string(platform) == "windows" ? "zip" : "tar.gz";
    return std::format("{}/{}/releases/download/{}/{}-{}-{}-{}.{}",
                       default_res_server_(),
                       pkgName,
                       version,
                       pkgName,
                       version,
                       platform,
                       detect_arch_(),
                       ext);
}

bool has_directory_entries_(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return false;
    return std::filesystem::directory_iterator(dir, ec) != std::filesystem::directory_iterator{};
}

bool stage_extracted_payload_(const std::filesystem::path& extractRoot,
                              const std::filesystem::path& installDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(extractRoot, ec) || !fs::is_directory(extractRoot, ec)) return false;

    std::vector<fs::path> entries;
    for (fs::directory_iterator it(extractRoot, ec), end; !ec && it != end; it.increment(ec)) {
        entries.push_back(it->path());
    }
    if (ec || entries.empty()) return false;

    fs::path payloadRoot = extractRoot;
    if (entries.size() == 1 && fs::is_directory(entries.front(), ec) && !ec) {
        payloadRoot = entries.front();
    }

    if (fs::exists(installDir, ec)) {
        if (fs::is_empty(installDir, ec)) {
            ec.clear();
            fs::remove_all(installDir, ec);
            if (ec) return false;
        } else {
            return true;
        }
    }

    fs::create_directories(installDir.parent_path(), ec);
    if (ec) return false;

    if (payloadRoot != extractRoot) {
        fs::rename(payloadRoot, installDir, ec);
        if (!ec) return true;
        ec.clear();
    }

    fs::create_directories(installDir, ec);
    if (ec) return false;

    auto move_entry = [&](const fs::path& source) -> bool {
        auto dest = installDir / source.filename();
        fs::rename(source, dest, ec);
        if (!ec) return true;
        ec.clear();
        fs::copy(source, dest,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                 ec);
        if (ec) return false;
        ec.clear();
        fs::remove_all(source, ec);
        return !ec;
    };

    if (payloadRoot == extractRoot) {
        for (auto& entry : entries) {
            if (!move_entry(entry)) return false;
        }
        return true;
    }

    std::vector<fs::path> payloadEntries;
    for (fs::directory_iterator it(payloadRoot, ec), end; !ec && it != end; it.increment(ec)) {
        payloadEntries.push_back(it->path());
    }
    if (ec) return false;
    for (auto& entry : payloadEntries) {
        if (!move_entry(entry)) return false;
    }
    return true;
}

bool normalize_file_install_(const std::filesystem::path& installPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(installPath, ec) || !fs::is_regular_file(installPath, ec)) return true;

    auto parent = installPath.parent_path();
    auto fileName = installPath.filename();
    auto tempFile = parent / (fileName.string() + ".xlings.tmp");

    fs::rename(installPath, tempFile, ec);
    if (ec) return false;

    fs::create_directories(installPath, ec);
    if (ec) {
        ec.clear();
        fs::rename(tempFile, installPath, ec);
        return false;
    }

    fs::rename(tempFile, installPath / fileName, ec);
    if (!ec) return true;

    ec.clear();
    fs::copy_file(tempFile, installPath / fileName, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    ec.clear();
    fs::remove(tempFile, ec);
    return !ec;
}

class ScopedCurrentDir_ {
    std::filesystem::path oldDir_;
    bool changed_ { false };

public:
    explicit ScopedCurrentDir_(const std::filesystem::path& newDir) {
        std::error_code ec;
        oldDir_ = std::filesystem::current_path(ec);
        if (ec || newDir.empty()) return;
        std::filesystem::create_directories(newDir, ec);
        if (ec) return;
        std::filesystem::current_path(newDir, ec);
        changed_ = !ec;
    }

    ~ScopedCurrentDir_() {
        if (!changed_) return;
        std::error_code ec;
        std::filesystem::current_path(oldDir_, ec);
    }
};

std::filesystem::path current_workspace_config_path_() {
    if (Config::has_project_config()) {
        if (Config::project_subos_mode() == ProjectSubosMode::Named) {
            return Config::project_dir() / ".xlings" / "subos" / Config::project_subos_name() / ".xlings.json";
        }
        return Config::project_dir() / ".xlings.json";
    }
    return Config::paths().homeDir / "subos" / Config::paths().activeSubos / ".xlings.json";
}

xvm::Workspace load_workspace_file_(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    try {
        auto content = platform::read_file_to_string(path.string());
        auto json = nlohmann::json::parse(content, nullptr, false);
        if (json.is_discarded() || !json.is_object()) return {};
        if (!json.contains("workspace") || !json["workspace"].is_object()) return {};
        return xvm::workspace_from_json(json["workspace"]);
    } catch (...) {
        return {};
    }
}

std::vector<std::filesystem::path> workspace_config_paths_for_scope_(PackageScope scope) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;

    if (scope == PackageScope::Project && Config::has_project_config()) {
        auto projectRoot = Config::project_dir();
        if (!projectRoot.empty()) {
            paths.push_back(projectRoot / ".xlings.json");
            auto subosRoot = projectRoot / ".xlings" / "subos";
            std::error_code ec;
            if (fs::exists(subosRoot, ec) && fs::is_directory(subosRoot, ec)) {
                for (auto& entry : platform::dir_entries(subosRoot)) {
                    if (entry.is_directory()) {
                        paths.push_back(entry.path() / ".xlings.json");
                    }
                }
            }
        }
        return paths;
    }

    auto subosRoot = Config::paths().homeDir / "subos";
    std::error_code ec;
    if (fs::exists(subosRoot, ec) && fs::is_directory(subosRoot, ec)) {
        for (auto& entry : platform::dir_entries(subosRoot)) {
            if (entry.is_directory()) {
                auto name = entry.path().filename().string();
                if (name != "current") {
                    paths.push_back(entry.path() / ".xlings.json");
                }
            }
        }
    }
    return paths;
}

bool is_version_referenced_anywhere_(PackageScope scope,
                                     const std::string& target,
                                     const std::string& version,
                                     const std::filesystem::path& excludePath = {}) {
    std::error_code ec;
    auto excludeCanonical = excludePath.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(excludePath, ec);
    for (auto& configPath : workspace_config_paths_for_scope_(scope)) {
        auto canonical = std::filesystem::weakly_canonical(configPath, ec);
        if (!excludeCanonical.empty() && !ec && canonical == excludeCanonical) {
            continue;
        }
        auto workspace = load_workspace_file_(configPath);
        auto it = workspace.find(target);
        if (it != workspace.end() && it->second == version) return true;
    }
    return false;
}

void remove_target_shims_(const std::string& target, const std::string& version) {
    namespace fs = std::filesystem;
    auto db = Config::versions();
    auto* vinfo = xvm::get_vinfo(db, target);
    auto& binDir = Config::paths().binDir;
    std::error_code ec;

    auto mainName = (vinfo && !vinfo->filename.empty()) ? vinfo->filename : target;
    auto mainShim = binDir / mainName;
    if (fs::exists(mainShim, ec) || fs::is_symlink(mainShim, ec)) {
        ec.clear();
        fs::remove(mainShim, ec);
    }

    if (!vinfo) return;
    for (auto& [bindingName, vermap] : vinfo->bindings) {
        auto vit = vermap.find(version);
        if (vit == vermap.end()) continue;
        auto bindPath = binDir / bindingName;
        ec.clear();
        if (fs::exists(bindPath, ec) || fs::is_symlink(bindPath, ec)) {
            ec.clear();
            fs::remove(bindPath, ec);
        }
    }
}

void detach_current_subos_(const std::string& target, const std::string& version) {
    auto& ws = Config::workspace_mut();
    auto wit = ws.find(target);
    if (wit == ws.end() || wit->second != version) return;

    auto db = Config::versions();
    auto sysroot_include = Config::paths().subosDir / "usr" / "include";
    auto sysroot_lib = Config::paths().libDir;

    if (auto* vdata = xvm::get_vdata(db, target, version)) {
        if (!vdata->includedir.empty()) {
            xvm::remove_headers(vdata->includedir, sysroot_include);
        }
        if (!vdata->libdir.empty()) {
            xvm::remove_libdir(vdata->libdir, sysroot_lib);
        }
    }

    remove_target_shims_(target, version);
    ws.erase(wit);
    Config::save_workspace();
}

void process_xvm_operations_(const PlanNode& node,
                             const std::filesystem::path& dataDir,
                             mcpplibs::xpkg::PackageExecutor& executor) {
    auto xvm_ops = executor.xvm_operations();
    auto sysroot_include = Config::paths().subosDir / "usr" / "include";
    auto sysroot_lib = Config::paths().libDir;

    for (auto& op : xvm_ops) {
        if (op.op == "add") {
            std::string ver = op.version.empty() ? node.version : op.version;
            std::string p = op.bindir.empty()
                ? ((node.storeRoot.empty() ? (dataDir / "xpkgs") : node.storeRoot)
                    / detail_::effective_store_name_(node)
                    / node.version).string()
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
}

bool run_config_hook_(const PlanNode& node,
                      const std::filesystem::path& dataDir,
                      mcpplibs::xpkg::PackageExecutor& executor,
                      mcpplibs::xpkg::ExecutionContext& ctx,
                      std::function<void(const InstallStatus&)> onStatus) {
    if (!executor.has_hook(mcpplibs::xpkg::HookType::Config)) return true;
    if (onStatus) {
        onStatus({ node.name, InstallPhase::Configuring, 0.8f, "" });
    }
    ScopedCurrentDir_ configCwd(ctx.install_dir);
    auto hookResult = executor.run_hook(mcpplibs::xpkg::HookType::Config, ctx);
    if (!hookResult.success) {
        log::warn("config hook failed for {}: {}", node.name, hookResult.error);
        return false;
    }
    process_xvm_operations_(node, dataDir, executor);
    return true;
}

bool register_platform_loader_sandbox_(lua::State* L, const std::string& platform) {
    auto quoted = "'" + platform + "'";
    auto script =
        "import = function(...) return setmetatable({}, { __index = function() return function() end end }) end\n"
        "function is_host(name) return name == " + quoted + " end\n"
        "format = string.format\n"
        "_RUNTIME = { platform = " + quoted + " }\n"
        "os.host = function() return " + quoted + " end\n"
        "os.arch = os.arch or function() return 'arm64' end\n"
        "os.isfile = os.isfile or function() return false end\n"
        "os.isdir = os.isdir or function() return false end\n"
        "os.scriptdir = os.scriptdir or function() return '.' end\n"
        "os.dirs = os.dirs or function() return {} end\n"
        "os.files = os.files or function() return {} end\n"
        "os.exists = os.exists or function() return false end\n"
        "os.tryrm = os.tryrm or function() end\n"
        "os.trymv = os.trymv or function() end\n"
        "os.mv = os.mv or function() return true end\n"
        "os.cp = os.cp or function() return true end\n"
        "os.iorun = os.iorun or function() return nil end\n"
        "os.cd = os.cd or function() end\n"
        "os.mkdir = os.mkdir or function() end\n"
        "os.sleep = os.sleep or function() end\n"
        "path = path or {}\n"
        "path.join = path.join or function(...) "
        "  local parts = {} "
        "  for i = 1, select('#', ...) do "
        "    local v = select(i, ...) "
        "    if v ~= nil then parts[#parts+1] = tostring(v) end "
        "  end "
        "  return table.concat(parts, '/') "
        "end\n"
        "path.filename = path.filename or function(p) return type(p)=='string' and (p:match('[^/\\\\]+$') or p) or '' end\n"
        "path.directory = path.directory or function(p) return type(p)=='string' and (p:match('(.*)[/\\\\]') or '.') or '.' end\n"
        "path.basename = path.basename or function(p) return type(p)=='string' and (p:match('[^/\\\\]+$') or p) or '' end\n"
        "io.readfile = io.readfile or function() return '' end\n"
        "io.writefile = io.writefile or function() end\n"
        "try = try or function(block) pcall(block[1]) end\n"
        "cprint = cprint or print\n"
        "string.replace = string.replace or function(s, old, new) return s:gsub(old, new) end\n"
        "string.split = string.split or function(s, sep) "
        "  local r = {} "
        "  for m in (s .. sep):gmatch('(.-)' .. sep) do r[#r+1] = m end "
        "  return r "
        "end\n"
        "raise = raise or function() end\n"
        "runtime = setmetatable({}, { __index = function() return function() return '' end end })\n"
        "system = setmetatable({}, { __index = function() return function() return '' end end })\n"
        "libxpkg = setmetatable({}, { __index = function() return setmetatable({}, { __index = function() return function() return '' end end }) end })\n";

    return lua::L_dostring(L, script.c_str()) == lua::OK;
}

std::unordered_map<std::string, mcpplibs::xpkg::PlatformResource>
load_platform_entries_(const std::filesystem::path& pkgFile, const std::string& platform) {
    std::unordered_map<std::string, mcpplibs::xpkg::PlatformResource> entries;

    auto* L = lua::L_newstate();
    if (!L) return entries;
    lua::L_openlibs(L);

    auto closeLua = [&]() {
        if (L) {
            lua::close(L);
            L = nullptr;
        }
    };

    if (!register_platform_loader_sandbox_(L, platform)) {
        closeLua();
        return entries;
    }
    if (lua::L_dofile(L, pkgFile.string().c_str()) != lua::OK) {
        closeLua();
        return entries;
    }

    lua::getglobal(L, "package");
    if (lua::type(L, -1) != lua::TTABLE) {
        closeLua();
        return entries;
    }

    auto packageIdx = lua::gettop(L);
    lua::getfield(L, packageIdx, "xpm");
    if (lua::type(L, -1) != lua::TTABLE) {
        closeLua();
        return entries;
    }

    auto xpmIdx = lua::gettop(L);
    lua::getfield(L, xpmIdx, platform.c_str());
    if (lua::type(L, -1) != lua::TTABLE) {
        closeLua();
        return entries;
    }

    auto platformIdx = lua::gettop(L);
    lua::pushnil(L);
    while (lua::next(L, platformIdx)) {
        std::string version;
        if (lua::type(L, -2) == lua::TSTRING) version = lua::tostring(L, -2);
        if (!version.empty() && version != "deps" && version != "inherits") {
            mcpplibs::xpkg::PlatformResource res;
            if (lua::type(L, -1) == lua::TTABLE) {
                auto read_field = [&](const char* key) -> std::string {
                    lua::getfield(L, -1, key);
                    std::string val;
                    if (lua::type(L, -1) == lua::TSTRING) val = lua::tostring(L, -1);
                    lua::pop(L, 1);
                    return val;
                };
                res.url = read_field("url");
                res.sha256 = read_field("sha256");
                res.ref = read_field("ref");
            } else if (lua::type(L, -1) == lua::TSTRING) {
                res.url = lua::tostring(L, -1);
            }
            entries[version] = std::move(res);
        }
        lua::pop(L, 1);
    }

    closeLua();
    return entries;
}

}  // namespace detail_

class Installer {
    IndexManager* index_ { nullptr };
    PackageCatalog* catalog_ { nullptr };

public:
    explicit Installer(IndexManager& index) : index_(&index) {}
    explicit Installer(PackageCatalog& catalog) : catalog_(&catalog) {}

    // Execute an install plan
    std::expected<void, std::string>
    execute(const InstallPlan& plan,
            const DownloaderConfig& dlConfig,
            std::function<void(const InstallStatus&)> onStatus) {

        if (plan.has_errors()) {
            return std::unexpected(
                std::format("plan has errors: {}", plan.errors[0]));
        }

        auto dataDir = Config::paths().dataDir;
        auto platform = detect_platform_();

        // Phase 1: Collect download tasks for non-installed packages
        std::vector<DownloadTask> dlTasks;
        std::unordered_set<std::string> plannedDownloads;
        for (auto& node : plan.nodes) {
            if (node.alreadyInstalled) continue;

            std::expected<mcpplibs::xpkg::Package, std::string> pkg =
                catalog_
                    ? catalog_->load_package(PackageMatch{
                        .rawName = node.rawName,
                        .name = node.name,
                        .version = node.version,
                        .namespaceName = node.namespaceName,
                        .canonicalName = node.canonicalName,
                        .repoName = node.repoName,
                        .pkgFile = node.pkgFile,
                        .storeRoot = node.storeRoot,
                        .scope = node.scope,
                        .installed = node.alreadyInstalled,
                    })
                    : index_->load_package(node.name);
            if (!pkg) {
                log::warn("skipping {}: {}", node.name, pkg.error());
                continue;
            }

            auto platformEntries = detail_::load_platform_entries_(node.pkgFile, platform);
            if (platformEntries.empty()) {
                auto platformIt = pkg->xpm.entries.find(platform);
                if (platformIt != pkg->xpm.entries.end()) {
                    platformEntries = platformIt->second;
                }
            }
            if (platformEntries.empty()) continue;

            std::string version = node.version;
            // Follow ref chain
            auto verIt = platformEntries.find(version);
            if (verIt != platformEntries.end() && !verIt->second.ref.empty()) {
                version = verIt->second.ref;
                verIt = platformEntries.find(version);
            }

            if (verIt == platformEntries.end()) continue;

            auto& res = verIt->second;
            if (res.url == "XLINGS_RES") {
                res.url = detail_::build_xlings_res_url_(node.name, version, platform);
            }
            if (res.url.empty()) continue;

            DownloadTask task;
            task.name = detail_::plan_key_(node);
            task.url = res.url;
            task.sha256 = res.sha256;
            task.destDir = detail_::download_cache_dir_(node, dataDir);
            plannedDownloads.insert(task.name);
            dlTasks.push_back(std::move(task));
        }

        std::unordered_map<std::string, DownloadResult> downloadResults;

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
                } else {
                    downloadResults[r.name] = r;
                }
            }
        }

        // Phase 2: Install each package in topological order
        for (auto& node : plan.nodes) {
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
            auto targetRoot = node.storeRoot.empty() ? (dataDir / "xpkgs") : node.storeRoot;
            ctx.install_dir = targetRoot / detail_::effective_store_name_(node) / node.version;
            ctx.bin_dir = Config::paths().binDir;
            ctx.xpkg_dir = node.pkgFile.parent_path();
            ctx.subos_sysrootdir = Config::paths().subosDir.string();

            auto planKey = detail_::plan_key_(node);
            auto dlIt = downloadResults.find(planKey);
            std::optional<std::filesystem::path> extractedRoot;
            if (plannedDownloads.contains(planKey) && dlIt == downloadResults.end()) {
                log::error("download artifact missing for {}", node.name);
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Failed, 0.0f, "download artifact missing" });
                }
                continue;
            }
            if (dlIt != downloadResults.end()) {
                ctx.install_file = dlIt->second.localFile;
                ctx.run_dir = dlIt->second.localFile.parent_path();
                if (detail_::is_archive_(dlIt->second.localFile)) {
                    if (onStatus) {
                        onStatus({ node.name, InstallPhase::Extracting, 0.35f, "" });
                    }
                    auto extractDir = detail_::extract_work_dir_(node, dataDir);
                    std::error_code ec;
                    std::filesystem::remove_all(extractDir, ec);
                    auto extracted = extract_archive(dlIt->second.localFile, extractDir);
                    if (!extracted) {
                        log::error("extract failed for {}: {}", node.name, extracted.error());
                        if (onStatus) {
                            onStatus({ node.name, InstallPhase::Failed, 0.0f, extracted.error() });
                        }
                        continue;
                    }
                    extractedRoot = *extracted;
                    ctx.run_dir = *extracted;
                }
            } else {
                ctx.install_file = node.pkgFile;
                ctx.run_dir = ctx.install_dir;
            }

            bool payloadInstalled = node.alreadyInstalled;

            // Check if already installed via hook
            if (!payloadInstalled && executor.has_hook(mcpplibs::xpkg::HookType::Installed)) {
                auto hookResult = executor.check_installed(ctx);
                if (hookResult.success && !hookResult.version.empty()) {
                    log::info("{} already installed (version {})",
                              node.name, hookResult.version);
                    payloadInstalled = true;
                }
            }

            // Run install hook
            if (!payloadInstalled && executor.has_hook(mcpplibs::xpkg::HookType::Install)) {
                log::info("installing {}...", node.name);
                detail_::ScopedCurrentDir_ installCwd(ctx.run_dir);
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

            if (!payloadInstalled && extractedRoot && !detail_::has_directory_entries_(ctx.install_dir)) {
                if (!detail_::stage_extracted_payload_(*extractedRoot, ctx.install_dir)) {
                    log::error("failed to stage extracted payload for {}", node.name);
                    if (onStatus) {
                        onStatus({ node.name, InstallPhase::Failed, 0.0f,
                                   "failed to stage extracted payload" });
                    }
                    continue;
                }
            }

            if (!payloadInstalled && !detail_::normalize_file_install_(ctx.install_dir)) {
                log::error("failed to normalize file install layout for {}", node.name);
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Failed, 0.0f,
                               "failed to normalize file install layout" });
                }
                continue;
            }

            if (!detail_::run_config_hook_(node, dataDir, executor, ctx, onStatus)) {
                if (onStatus) {
                    onStatus({ node.name, InstallPhase::Failed, 0.0f,
                               "config hook failed" });
                }
                continue;
            }

            if (catalog_) {
                catalog_->mark_installed(PackageMatch{
                    .rawName = node.rawName,
                    .name = node.name,
                    .version = node.version,
                    .namespaceName = node.namespaceName,
                    .canonicalName = node.canonicalName,
                    .repoName = node.repoName,
                    .pkgFile = node.pkgFile,
                    .storeRoot = node.storeRoot,
                    .scope = node.scope,
                    .installed = true,
                }, true);
            } else {
                index_->mark_installed(node.name, true);
            }

            if (onStatus) {
                onStatus({ node.name, InstallPhase::Done, 1.0f,
                           payloadInstalled ? "already installed" : "" });
            }
            if (payloadInstalled) {
                log::info("{}@{} attached to current subos", node.name, node.version);
            } else {
                log::info("{}@{} installed successfully", node.name, node.version);
            }
        }

        return {};
    }

    // Uninstall a package
    std::expected<void, std::string>
    uninstall(const std::string& name) {
        auto platform = detect_platform_();
        auto currentWorkspacePath = detail_::current_workspace_config_path_();

        auto parse_target = [](std::string target) {
            auto at = target.find('@');
            if (at == std::string::npos) return std::pair{target, std::string{}};
            return std::pair{target.substr(0, at), target.substr(at + 1)};
        };

        auto [targetName, requestedVersion] = parse_target(name);
        if (requestedVersion.empty()) {
            requestedVersion = xvm::get_active_version(Config::effective_workspace(), targetName);
        }
        auto resolvedTarget = requestedVersion.empty() ? targetName : targetName + "@" + requestedVersion;

        std::filesystem::path pkgFile;
        std::filesystem::path installDir;
        std::optional<PackageMatch> resolvedMatch;

        if (catalog_) {
            auto match = catalog_->resolve_target(resolvedTarget, platform);
            if (!match) return std::unexpected(match.error());
            resolvedMatch = *match;
            pkgFile = match->pkgFile;
            installDir = (match->storeRoot.empty() ? (Config::paths().dataDir / "xpkgs") : match->storeRoot)
                / detail_::effective_store_name_(*match)
                / match->version;
        } else {
            auto* entry = index_->find_entry(name);
            if (!entry) {
                return std::unexpected(std::format("package '{}' not found", name));
            }
            pkgFile = entry->path;
            installDir = Config::paths().dataDir / "xpkgs" / name;
        }

        auto detachTarget = resolvedMatch ? resolvedMatch->name : targetName;
        auto detachVersion = resolvedMatch ? resolvedMatch->version : requestedVersion;
        auto stillReferenced = !detachVersion.empty()
            && detail_::is_version_referenced_anywhere_(
                resolvedMatch ? resolvedMatch->scope : PackageScope::Global,
                detachTarget,
                detachVersion,
                currentWorkspacePath);

        if (!detachVersion.empty()) {
            detail_::detach_current_subos_(detachTarget, detachVersion);
        }

        if (stillReferenced) {
            log::info("{}@{} detached from current subos; payload retained",
                      detachTarget, detachVersion);
            return {};
        }

        auto execResult = mcpplibs::xpkg::create_executor(pkgFile);
        if (!execResult) {
            return std::unexpected(execResult.error());
        }

        auto& executor = *execResult;

        mcpplibs::xpkg::ExecutionContext ctx;
        ctx.pkg_name = resolvedMatch ? resolvedMatch->name : name;
        ctx.version = resolvedMatch ? resolvedMatch->version : std::string{};
        ctx.platform = platform;
        ctx.bin_dir = Config::paths().binDir;
        ctx.install_dir = installDir;
        ctx.xpkg_dir = pkgFile.parent_path();
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

        if (catalog_) {
            catalog_->mark_installed(*resolvedMatch, false);
        } else {
            index_->mark_installed(name, false);
        }
        std::error_code ec;
        std::filesystem::remove_all(installDir, ec);
        if (ec) {
            log::warn("failed to remove payload dir {}: {}", installDir.string(), ec.message());
        }
        log::info("{} uninstalled", resolvedTarget);
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
