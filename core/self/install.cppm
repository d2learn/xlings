module;

#include <cstdio>

export module xlings.xself:install;

import std;

import xlings.config;
import xlings.json;
import xlings.platform;
import xlings.utils;

namespace xlings::xself {

namespace fs = std::filesystem;

static std::string read_version_from_json(const fs::path& homeDir) {
    auto conf = homeDir / ".xlings.json";
    if (!fs::exists(conf)) return "";
    try {
        auto content = platform::read_file_to_string(conf.string());
        auto j = nlohmann::json::parse(content, nullptr, false);
        if (!j.is_discarded() && j.contains("version") && j["version"].is_string()) {
            auto v = j["version"].get<std::string>();
            if (v.starts_with("v")) v = v.substr(1);
            return v;
        }
    } catch (...) {}
    return "";
}

/// True if path is under a temp dir (e.g. /tmp, $TMPDIR, $TEMP, $RUNNER_TEMP). Used to detect
/// quick_install extract dir — we must install to ~/.xlings, not "fix links" in place.
static bool is_under_temp_dir(const fs::path& p) {
    auto s = p.generic_string();  // forward slashes for portable comparison
    // quick_install uses "xlings-install" in temp dir name — definitive indicator
    if (s.find("xlings-install") != std::string::npos) return true;
    if (s.find("/tmp/") == 0 || s.find("/tmp") == 0) return true;
#if defined(__APPLE__)
    // macOS: /var/folders/... is system temp; canonical paths may use /private/var
    if (s.find("/var/folders/") != std::string::npos ||
        s.find("/private/var/folders/") != std::string::npos)
        return true;
#endif
#if defined(_WIN32)
    // Windows: match path segments /Temp/ or /Tmp/ (GetTempPath uses these)
    if (s.find("/Temp/") != std::string::npos || s.find("/Tmp/") != std::string::npos ||
        s.find("/temp/") != std::string::npos || s.find("/tmp/") != std::string::npos)
        return true;
#endif
    for (const char* env : {"TMPDIR", "TEMP", "TMP", "RUNNER_TEMP"}) {
        if (const char* v = std::getenv(env)) {
            auto prefix = fs::path(v).generic_string();
            if (prefix.empty()) continue;
            auto prefixSlash = prefix;
            if (prefixSlash.back() != '/') prefixSlash += '/';
            if (s.starts_with(prefixSlash) || s == prefix) return true;
#if defined(__APPLE__)
            // macOS: /var resolves to /private/var; canonical paths may have /private prefix
            if (prefix.starts_with("/var/") && s.starts_with("/private" + prefixSlash))
                return true;
#endif
        }
    }
    return false;
}

static fs::path detect_source_dir() {
    // Check CWD first: on Windows/PowerShell, `xlings` resolves to the PATH-installed
    // binary even when the user is inside a new release package directory.
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (!ec && !cwd.empty() && fs::exists(cwd / "xim") && fs::exists(cwd / "bin")) {
        return fs::weakly_canonical(cwd);
    }

    auto exe = platform::get_executable_path();
    if (exe.empty()) return {};
    auto binDir = exe.parent_path();
    auto candidate = binDir.parent_path();
    std::println("[xlings:self]: candidate: {}", candidate.string());
    if (fs::exists(candidate / "xim") && fs::exists(candidate / "bin")) {
        return fs::weakly_canonical(candidate);
    }
    return {};
}

static fs::path detect_existing_home() {
    auto envHome = utils::get_env_or_default("XLINGS_HOME");
    if (!envHome.empty()) {
        auto p = fs::path(envHome);
        if (fs::exists(p / "bin") && fs::exists(p / "subos"))
            return fs::weakly_canonical(p);
    }
    auto [rc, out] = platform::run_command_capture(
#ifdef _WIN32
        "where xlings 2>nul"
#else
        "command -v xlings 2>/dev/null"
#endif
    );
    (void)rc;
    // "where"/"command -v" may return multiple lines; use first valid path
    auto lines = utils::split_string(out, '\n');
    for (const auto& line : lines) {
        auto binPath = utils::trim_string(line);
        if (binPath.empty() || !fs::exists(binPath)) continue;
        auto p = fs::weakly_canonical(fs::path(binPath));
        // Walk upward from the executable directory and locate install root by markers.
        // This is more robust than hard-coding parent depth across different layouts.
        for (auto cur = p.parent_path(); !cur.empty(); cur = cur.parent_path()) {
            if (!fs::exists(cur / "bin") || !fs::exists(cur / "subos")) continue;
            // Ignore temp extract dir (quick_install) — install to ~/.xlings instead.
            if (!is_under_temp_dir(cur))
                return cur;
            break;
        }
    }
    return {};
}

static fs::path default_home() {
    return fs::path(platform::get_home_dir()) / ".xlings";
}

static void copy_directory_contents(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    for (auto& entry : fs::directory_iterator(src)) {
        auto target = dst / entry.path().filename();
        if (entry.is_directory()) {
            fs::copy(entry.path(), target,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        } else {
            fs::copy_file(entry.path(), target,
                          fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            std::println(stderr, "[xlings:self] copy failed: {} -> {} ({})",
                         entry.path().string(), target.string(), ec.message());
            ec.clear();
        }
    }
}

static void setup_shell_profiles(const fs::path& homeDir) {
#if defined(__linux__) || defined(__APPLE__)
    auto profileSh   = homeDir / "config" / "shell" / "xlings-profile.sh";
    auto profileFish = homeDir / "config" / "shell" / "xlings-profile.fish";

    auto sourceLine = "test -f \"" + profileSh.string() + "\" && source \"" + profileSh.string() + "\"";

    std::vector<fs::path> profiles;
#if defined(__APPLE__)
    profiles = {
        fs::path(platform::get_home_dir()) / ".zshrc",
        fs::path(platform::get_home_dir()) / ".bashrc",
        fs::path(platform::get_home_dir()) / ".zprofile",
    };
#else
    profiles = {
        fs::path(platform::get_home_dir()) / ".bashrc",
        fs::path(platform::get_home_dir()) / ".zshrc",
        fs::path(platform::get_home_dir()) / ".profile",
    };
#endif

    bool added = false;
    for (auto& prof : profiles) {
        if (!fs::exists(prof)) continue;
        auto content = platform::read_file_to_string(prof.string());
        if (content.find("xlings-profile") != std::string::npos) {
            std::println("[xlings:self] profile ok ({})", prof.filename().string());
            added = true;
            break;
        }
        std::string appendStr = "\n# xlings\n" + sourceLine + "\n";
        platform::write_string_to_file(prof.string(), content + appendStr);
        std::println("[xlings:self] added profile ({})", prof.filename().string());
        added = true;
        break;
    }

    auto fishConfig = fs::path(platform::get_home_dir()) / ".config" / "fish" / "config.fish";
    auto fishSourceLine = "test -f \"" + profileFish.string() + "\"; and source \"" + profileFish.string() + "\"";
    if (fs::exists(fishConfig) ||
        platform::run_command_capture("command -v fish 2>/dev/null").first == 0) {
        fs::create_directories(fishConfig.parent_path());
        std::string fishContent;
        if (fs::exists(fishConfig))
            fishContent = platform::read_file_to_string(fishConfig.string());
        if (fishContent.find("xlings-profile") == std::string::npos) {
            platform::write_string_to_file(fishConfig.string(),
                                           fishContent + "\n# xlings\n" + fishSourceLine + "\n");
            std::println("[xlings:self] added profile (config.fish)");
        } else {
            std::println("[xlings:self] profile ok (fish)");
        }
        added = true;
    }

    if (!added) {
        std::println("[xlings:self] no shell profile found, add manually:");
        std::println("  {}", sourceLine);
    }
#elif defined(_WIN32)
    auto xlingsBin = homeDir / "subos" / "current" / "bin";
    std::string checkCmd = "powershell -NoProfile -Command \""
        "$p=[System.Environment]::GetEnvironmentVariable('Path','User');"
        "if($p -notlike '*" + xlingsBin.string() + "*'){"
        "[System.Environment]::SetEnvironmentVariable('Path','"
        + xlingsBin.string() + ";'+$p,'User');"
        "Write-Host 'PATH updated'"
        "}else{Write-Host 'PATH already set'}\"";
    platform::exec(checkCmd);

    auto profilePs1 = homeDir / "config" / "shell" / "xlings-profile.ps1";
    std::string psCmd = "powershell -NoProfile -Command \""
        "$prof=$PROFILE;"
        "$dir=Split-Path $prof;"
        "if(!(Test-Path $dir)){New-Item -ItemType Directory -Force $dir|Out-Null};"
        "if(!(Test-Path $prof)){New-Item -ItemType File -Force $prof|Out-Null};"
        "$c=Get-Content $prof -Raw -ErrorAction SilentlyContinue;"
        "if(!$c -or $c -notlike '*xlings-profile*'){"
        "Add-Content $prof \\\"`n# xlings`nif(Test-Path '" + profilePs1.string() +
        "'){. '" + profilePs1.string() + "'}\\\""
        ";Write-Host 'PS profile added'"
        "}else{Write-Host 'PS profile already set'}\"";
    platform::exec(psCmd);
#endif
}

export int cmd_install() {
    auto srcDir = detect_source_dir();
    if (srcDir.empty()) {
        std::println(stderr, "[xlings:self] cannot detect source package directory.");
        std::println(stderr, "  run this command from inside a valid xlings release package");
        return 1;
    }

    auto pkgVersion = read_version_from_json(srcDir);
    if (pkgVersion.empty()) pkgVersion = std::string(Info::VERSION);
    auto existingHome = detect_existing_home();
    auto targetHome = existingHome.empty() ? default_home() : existingHome;
    auto installedVersion = read_version_from_json(targetHome);

    // When running from a temp extract (quick_install), install to ~/.xlings — temp dir will be deleted.
    std::error_code cmp_ec;
    bool sameDir = fs::equivalent(srcDir, targetHome, cmp_ec);
    bool fromTempExtract = false;
    fs::path existingPath = targetHome;  // path of existing install (may differ from target)
    if (!cmp_ec && sameDir && is_under_temp_dir(srcDir)) {
        targetHome = default_home();
        installedVersion = read_version_from_json(targetHome);
        existingPath = targetHome;
        fromTempExtract = true;  // no TTY in CI, skip prompts, always proceed
    }

    // Install header: show existing (if any) and install target; paths may differ
    std::println("\n[xlings:self] install");
    if (!installedVersion.empty()) {
        std::println("  existing: v{} -> {}", installedVersion, existingPath.string());
    }
    std::println("  install:  v{} -> {}", pkgVersion, targetHome.string());

    // Skip if source == target and not temp — "fix links" in place (e.g. dev from source).
    if (!cmp_ec && sameDir && !fromTempExtract) {
        std::println("\n[xlings:self] already in target dir, fixing links");
        auto currentLink = targetHome / "subos" / "current";
        auto defaultDir  = targetHome / "subos" / "default";
        platform::create_directory_link(currentLink, defaultDir);
        setup_shell_profiles(targetHome);
        std::println("[xlings:self] {} ({}) - ok\n", targetHome.string(), pkgVersion);
        return 0;
    }

    // Version / overwrite confirmation (skip when from temp extract — CI / quick_install)
    bool overwriteDataSubos = false;
    if (!fromTempExtract) {
        if (!pkgVersion.empty() && !installedVersion.empty() && pkgVersion == installedVersion) {
            if (!utils::ask_yes_no("\n[xlings:self] same version installed, reinstall? ", false)) {
                std::println("[xlings:self] cancelled\n");
                return 0;
            }
            if (fs::exists(targetHome / "data") || fs::exists(targetHome / "subos")) {
                overwriteDataSubos = utils::ask_yes_no("[xlings:self] overwrite data and subos? ", false);
            }
        } else if (fs::exists(targetHome / "bin") && fs::exists(targetHome / "subos")) {
            if (!utils::ask_yes_no("\n[xlings:self] overwrite existing installation? ", true)) {
                std::println("[xlings:self] cancelled\n");
                return 0;
            }
        }
    }

    // Selective install: preserve data/ and subos/ unless user chose to overwrite
    std::println("[xlings:self] copying binaries and config ...");
    fs::create_directories(targetHome);

    std::error_code ec;

    // 1. Remove non-user dirs; preserve data/subos unless user chose overwrite
    if (fs::exists(targetHome)) {
        for (auto& entry : fs::directory_iterator(targetHome)) {
            auto name = entry.path().filename().string();
            if (name == "data" || name == "subos") {
                if (!overwriteDataSubos) continue;
            }
            fs::remove_all(entry.path(), ec);
            ec.clear();
        }
    }

    // 2. Copy from release, skip data/subos when target already has them; merge subos static parts if exists
    for (auto& entry : fs::directory_iterator(srcDir)) {
        auto name = entry.path().filename().string();
        if (name == "data") {
            if (fs::exists(targetHome / "data") && fs::is_directory(targetHome / "data"))
                continue;
        }
        if (name == "subos") {
            if (fs::exists(targetHome / "subos") && fs::is_directory(targetHome / "subos")) {
                continue;  // 已有 subos 则完全保留，不合并（多 subos 且 bin 等无需覆盖）
            }
        }
        auto target = targetHome / name;
        if (entry.is_directory()) {
            fs::copy(entry.path(), target,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        } else {
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            std::println(stderr, "[xlings:self] copy failed: {} -> {} ({})",
                         entry.path().string(), target.string(), ec.message());
            ec.clear();
        }
    }

    // 3. First install: copy data and subos if target does not have them
    if (!fs::exists(targetHome / "data") && fs::exists(srcDir / "data")) {
        copy_directory_contents(srcDir / "data", targetHome / "data");
    }
    if (!fs::exists(targetHome / "subos") && fs::exists(srcDir / "subos")) {
        copy_directory_contents(srcDir / "subos", targetHome / "subos");
    }

    // 4. Fix permissions (platform-dispatched)
    platform::make_files_executable(targetHome / "bin");
    if (fs::exists(targetHome / "subos" / "default" / "bin"))
        platform::make_files_executable(targetHome / "subos" / "default" / "bin");

    // 5. Ensure subos/current link exists. Always recreate: Compress-Archive does not preserve
    //    NTFS junctions, so the zip may have subos/current as an empty dir instead of a junction.
    auto currentLink = targetHome / "subos" / "current";
    auto defaultDir  = targetHome / "subos" / "default";
    if (fs::exists(defaultDir) && platform::create_directory_link(currentLink, defaultDir))
        std::println("[xlings:self] subos/current -> default");

    setup_shell_profiles(targetHome);

    auto verifyBin = targetHome / "bin" /
#ifdef _WIN32
        "xlings.exe";
#else
        "xlings";
#endif
    if (fs::exists(verifyBin)) {
        auto [rc, out] = platform::run_command_capture(
            "\"" + verifyBin.string() + "\" -h");
        if (rc != 0)
            std::println(stderr, "[xlings:self] warning: verification failed");
    }

    std::println("\n[xlings:self] install: {} ({}) - ok", targetHome.string(), pkgVersion);
    std::println("");
    std::println("  run 'xlings -h' to get started");
#if defined(__linux__) || defined(__APPLE__)
    std::println("  restart shell or: source ~/.bashrc");
#else
    std::println("  restart terminal to refresh PATH");
#endif
    std::println("");
    return 0;
}

} // namespace xlings::xself
