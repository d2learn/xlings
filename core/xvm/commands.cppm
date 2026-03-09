export module xlings.xvm.commands;

import std;

import xlings.common;
import xlings.config;
import xlings.log;
import xlings.platform;
import xlings.xself;
import xlings.xvm.types;
import xlings.xvm.db;

export namespace xlings::xvm {

namespace fs = std::filesystem;

// Cross-platform link: symlink on Unix, directory junction or copy on Windows
void create_link_(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
#if defined(_WIN32)
    if (fs::is_directory(src)) {
        // Use directory junction on Windows (no admin required)
        platform::create_directory_link(dst.string(), src.string());
    } else {
        fs::create_hard_link(src, dst, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }
#else
    fs::create_symlink(src, dst, ec);
#endif
    if (ec) log::warn("[xvm] link failed: {} -> {}", dst.string(), src.string());
}

// Install header symlinks from source includedir into sysroot include/
void install_headers(const std::string& includedir, const fs::path& sysroot_include) {
    fs::create_directories(sysroot_include);
    std::error_code ec;
    fs::path src(includedir);
    if (!fs::exists(src, ec)) return;
    for (auto& entry : platform::dir_entries(src)) {
        auto target = sysroot_include / entry.path().filename();
        if (fs::exists(target, ec) || fs::is_symlink(target, ec)) {
            log::debug("[xvm] overwriting header: {}", entry.path().filename().string());
            fs::remove_all(target, ec);
        }
        create_link_(entry.path(), target);
    }
}

// Remove header symlinks that point into the given source includedir
void remove_headers(const std::string& includedir, const fs::path& sysroot_include) {
    if (includedir.empty()) return;
    fs::path src(includedir);
    std::error_code ec;
    if (!fs::exists(src, ec)) return;
    for (auto& entry : platform::dir_entries(src)) {
        auto target = sysroot_include / entry.path().filename();
        if (fs::is_symlink(target, ec)) {
            fs::remove(target, ec);
#if defined(_WIN32)
        } else if (fs::exists(target, ec)) {
            // On Windows, directory junctions appear as dirs, not symlinks
            fs::remove_all(target, ec);
#endif
        }
    }
}

// Install library symlinks from source libdir into sysroot lib/
void install_libs(const std::string& libdir, const fs::path& sysroot_lib,
                  const std::vector<std::string>& libs) {
    fs::create_directories(sysroot_lib);
    std::error_code ec;
    for (auto& lib : libs) {
        auto src = fs::path(libdir) / lib;
        auto dst = sysroot_lib / lib;
        if (fs::exists(dst, ec)) fs::remove(dst, ec);
        if (fs::exists(src, ec)) create_link_(src, dst);
    }
}

// Install all library entries from source libdir into sysroot lib/
void install_libdir(const std::string& libdir, const fs::path& sysroot_lib) {
    fs::create_directories(sysroot_lib);
    std::error_code ec;
    fs::path src(libdir);
    if (!fs::exists(src, ec)) return;
    for (auto& entry : platform::dir_entries(src)) {
        auto target = sysroot_lib / entry.path().filename();
        if (fs::exists(target, ec) || fs::is_symlink(target, ec)) {
            fs::remove_all(target, ec);
        }
        create_link_(entry.path(), target);
    }
}

// Remove library symlinks that were installed from source libdir
void remove_libdir(const std::string& libdir, const fs::path& sysroot_lib) {
    if (libdir.empty()) return;
    fs::path src(libdir);
    std::error_code ec;
    if (!fs::exists(src, ec)) return;
    for (auto& entry : platform::dir_entries(src)) {
        auto target = sysroot_lib / entry.path().filename();
        if (fs::is_symlink(target, ec)) {
            fs::remove(target, ec);
#if defined(_WIN32)
        } else if (fs::exists(target, ec)) {
            fs::remove_all(target, ec);
#endif
        }
    }
}

// xlings use <target> <version>
// Updates the active subos workspace and creates/updates bin/ hardlinks
int cmd_use(const std::string& target, const std::string& version) {
    auto db = Config::versions();
    auto& p  = Config::paths();

    if (!has_target(db, target)) {
        log::error("[xlings:use] '{}' not found in version database", target);
        log::error("  hint: install it first with `xlings install {}`", target);
        return 1;
    }

    // Fuzzy match version
    auto resolved = match_version(db, target, version);
    if (resolved.empty()) {
        log::error("[xlings:use] version '{}' not found for '{}'", version, target);
        auto all = get_all_versions(db, target);
        if (!all.empty()) {
            std::string avail = "  available:";
            for (auto& v : all) avail += " " + v;
            log::error("{}", avail);
        }
        return 1;
    }

    log::debug("fuzzy version match: {} -> {}", version, resolved);

    // Header & lib switching: remove old, install new
    auto sysroot_include = p.subosDir / "usr" / "include";
    auto sysroot_lib     = p.subosDir / "usr" / "lib";
    auto workspace = Config::effective_workspace();
    auto old_active = get_active_version(workspace, target);
    log::debug("switching headers: {} -> {}", old_active.empty() ? "(none)" : old_active, resolved);
    if (!old_active.empty() && old_active != resolved) {
        auto old_vdata = get_vdata(db, target, old_active);
        if (old_vdata && !old_vdata->includedir.empty())
            remove_headers(old_vdata->includedir, sysroot_include);
        if (old_vdata && !old_vdata->libdir.empty())
            remove_libdir(old_vdata->libdir, sysroot_lib);
    }
    auto new_vdata = get_vdata(db, target, resolved);
    if (new_vdata && !new_vdata->includedir.empty())
        install_headers(new_vdata->includedir, sysroot_include);
    if (new_vdata && !new_vdata->libdir.empty())
        install_libdir(new_vdata->libdir, sysroot_lib);

    // Update workspace
    Config::workspace_mut()[target] = resolved;
    Config::save_workspace();

    // Create/update shim in subos bin/ using unified create_shim()
#ifdef _WIN32
    auto xlings_bin = p.homeDir / "bin" / "xlings.exe";
    constexpr std::string_view shim_ext = ".exe";
#else
    auto xlings_bin = p.homeDir / "bin" / "xlings";
    constexpr std::string_view shim_ext = "";
#endif
    if (!fs::exists(xlings_bin)) {
        xlings_bin = p.homeDir / "xlings";
    }

    if (fs::exists(xlings_bin)) {
        auto vinfo = get_vinfo(db, target);
        std::string shim_name = (vinfo && !vinfo->filename.empty()) ? vinfo->filename : target;
        if (!shim_ext.empty() && !shim_name.ends_with(shim_ext))
            shim_name += shim_ext;

        fs::create_directories(p.binDir);
        auto result = xself::create_shim(xlings_bin, p.binDir / shim_name);
        if (result == xself::LinkResult::Failed) {
            log::warn("[xlings:use] failed to create shim for '{}'", shim_name);
        }

        common::mirror_shim_to_global_bin(xlings_bin, shim_name);

        // Create shims for bindings
        if (vinfo) {
            for (auto& [binding_name, vermap] : vinfo->bindings) {
                std::string bind_name{binding_name};
                if (!shim_ext.empty() && !bind_name.ends_with(shim_ext))
                    bind_name += shim_ext;
                xself::create_shim(xlings_bin, p.binDir / bind_name);
                common::mirror_shim_to_global_bin(xlings_bin, bind_name);
            }
        }
    }

    log::println("[xlings:use] {} -> {}", target, resolved);
    return 0;
}

// List versions for a target (used by `xlings use <target>` without version)
int cmd_list_versions(const std::string& target) {
    auto db = Config::versions();

    if (!has_target(db, target)) {
        log::error("[xlings] '{}' not found in version database", target);
        return 1;
    }

    auto workspace = Config::effective_workspace();
    auto active = get_active_version(workspace, target);
    auto all = get_all_versions(db, target);

    std::println("[xlings] versions for '{}':", target);
    for (auto& ver : all) {
        auto vdata = get_vdata(db, target, ver);
        std::string marker = (ver == active) ? " *" : "  ";
        std::string path_info;
        if (vdata && !vdata->path.empty()) {
            path_info = " (" + vdata->path + ")";
        }
        std::println(" {} {}{}", marker, ver, path_info);
    }

    return 0;
}

// Register a version in the global database (called after xim install)
void register_version(const std::string& target,
                      const std::string& version,
                      const std::string& path,
                      const std::string& type,
                      const std::string& filename) {
    add_version(Config::versions_mut(), target, version, path, type, filename);
    Config::save_versions();
}

} // namespace xlings::xvm
