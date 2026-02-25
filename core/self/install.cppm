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

static fs::path detect_source_dir() {
    auto exe = platform::get_executable_path();
    if (exe.empty()) return {};
    auto binDir = exe.parent_path();
    auto candidate = binDir.parent_path();
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
    auto binPath = utils::trim_string(out);
    if (!binPath.empty() && fs::exists(binPath)) {
        auto p = fs::weakly_canonical(fs::path(binPath));
        auto candidate = p.parent_path().parent_path().parent_path().parent_path();
        if (fs::exists(candidate / "bin") && fs::exists(candidate / "subos"))
            return candidate;
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
            std::println("[xlings:self] profile already configured in {}", prof.string());
            added = true;
            break;
        }
        std::string appendStr = "\n# xlings\n" + sourceLine + "\n";
        platform::write_string_to_file(prof.string(), content + appendStr);
        std::println("[xlings:self] added profile to {}", prof.string());
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
            std::println("[xlings:self] added profile to {}", fishConfig.string());
        } else {
            std::println("[xlings:self] fish profile already configured");
        }
        added = true;
    }

    if (!added) {
        std::println("[xlings:self] no shell profile found. Manually add:");
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
        std::println(stderr, "  Run this command from inside a valid xlings release package.");
        return 1;
    }

    auto pkgVersion = read_version_from_json(srcDir);
    if (pkgVersion.empty()) pkgVersion = std::string(Info::VERSION);
    std::println("[xlings:self] package version: v{}", pkgVersion);
    std::println("[xlings:self] package source:  {}", srcDir.string());

    auto existingHome = detect_existing_home();
    auto targetHome = existingHome.empty() ? default_home() : existingHome;

    std::println("[xlings:self] install target:  {}", targetHome.string());

    auto installedVersion = read_version_from_json(targetHome);
    if (!installedVersion.empty())
        std::println("[xlings:self] installed version at {}: v{}", targetHome.string(), installedVersion);

    // Skip if source == target (equivalent returns unspecified when paths don't exist)
    std::error_code cmp_ec;
    bool sameDir = fs::equivalent(srcDir, targetHome, cmp_ec);
    if (!cmp_ec && sameDir) {
        std::println("[xlings:self] already running from target directory, nothing to copy.");
        auto currentLink = targetHome / "subos" / "current";
        auto defaultDir  = targetHome / "subos" / "default";
        platform::create_directory_link(currentLink, defaultDir);
        setup_shell_profiles(targetHome);
        std::println("[xlings:self] install ok");
        return 0;
    }

    // Version / overwrite confirmation
    if (!pkgVersion.empty() && !installedVersion.empty() && pkgVersion == installedVersion) {
        if (!utils::ask_yes_no("[xlings:self] same version (v" + pkgVersion +
                               ") is already installed. Continue reinstall? ", false)) {
            std::println("[xlings:self] installation cancelled.");
            return 0;
        }
    } else if (fs::exists(targetHome / "bin") && fs::exists(targetHome / "subos")) {
        if (!utils::ask_yes_no("[xlings:self] overwrite existing installation at " +
                               targetHome.string() + "? ", true)) {
            std::println("[xlings:self] installation cancelled.");
            return 0;
        }
    }

    // Preserve cached data (data/ and subos/)
    fs::path dataBackup, subosBackup;
    auto dataPath  = targetHome / "data";
    auto subosPath = targetHome / "subos";

    bool preserveCache = false;
    bool hasExistingCache = (fs::exists(dataPath)  && fs::is_directory(dataPath)) ||
                            (fs::exists(subosPath) && fs::is_directory(subosPath));

    if (hasExistingCache) {
        preserveCache = utils::ask_yes_no(
            "[xlings:self] keep existing cached data? ", true);
    }

    auto tmpDir = fs::temp_directory_path() / ("xlings-install-backup-" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(tmpDir);

    if (preserveCache && fs::exists(dataPath) && fs::is_directory(dataPath)) {
        dataBackup = tmpDir / "data";
        std::println("[xlings:self] backing up data ...");
        copy_directory_contents(dataPath, dataBackup);
    }
    if (preserveCache && fs::exists(subosPath) && fs::is_directory(subosPath)) {
        subosBackup = tmpDir / "subos";
        std::println("[xlings:self] backing up subos ...");
        copy_directory_contents(subosPath, subosBackup);
    }

    // Copy package to target
    std::println("[xlings:self] installing to {} ...", targetHome.string());
    fs::create_directories(targetHome);

    if (fs::exists(targetHome)) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(targetHome)) {
            fs::remove_all(entry.path(), ec);
        }
    }
    copy_directory_contents(srcDir, targetHome);

    // Fix permissions (platform-dispatched)
    platform::make_files_executable(targetHome / "bin");
    platform::make_files_executable(targetHome / "subos" / "default" / "bin");

    // Create subos/current link (platform-dispatched)
    auto currentLink = targetHome / "subos" / "current";
    auto defaultDir  = targetHome / "subos" / "default";
    if (platform::create_directory_link(currentLink, defaultDir))
        std::println("[xlings:self] created subos/current -> default");

    // Restore preserved cached data
    if (!dataBackup.empty() && fs::exists(dataBackup)) {
        std::println("[xlings:self] restoring data ...");
        std::error_code ec;
        if (fs::exists(targetHome / "data"))
            fs::remove_all(targetHome / "data", ec);
        copy_directory_contents(dataBackup, targetHome / "data");
        std::println("[xlings:self] data preserved.");
    }
    if (!subosBackup.empty() && fs::exists(subosBackup)) {
        std::println("[xlings:self] restoring subos ...");
        std::error_code ec;
        if (fs::exists(targetHome / "subos"))
            fs::remove_all(targetHome / "subos", ec);
        copy_directory_contents(subosBackup, targetHome / "subos");
        platform::create_directory_link(currentLink, defaultDir);
        std::println("[xlings:self] subos preserved.");
    }

    // Cleanup backup
    {
        std::error_code ec;
        fs::remove_all(tmpDir, ec);
    }

    // Setup shell profiles / PATH
    setup_shell_profiles(targetHome);

    // Verify
    auto verifyBin = targetHome / "bin" /
#ifdef _WIN32
        "xlings.exe";
#else
        "xlings";
#endif
    if (fs::exists(verifyBin)) {
        auto [rc, out] = platform::run_command_capture(
            "\"" + verifyBin.string() + "\" -h");
        if (rc == 0)
            std::println("[xlings:self] verification passed.");
        else
            std::println("[xlings:self] warning: binary test returned non-zero.");
    }

    std::println("\n[xlings:self] xlings installed successfully!");
    std::println("    Run 'xlings -h' to get started.");
#if defined(__linux__) || defined(__APPLE__)
    std::println("    Restart your shell or run: source ~/.bashrc");
#else
    std::println("    Restart your terminal to refresh PATH.");
#endif
    std::println("");
    return 0;
}

} // namespace xlings::xself
