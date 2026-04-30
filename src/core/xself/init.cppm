export module xlings.core.xself.init;

import std;

import xlings.core.config;
import xlings.libs.json;
import xlings.core.log;
import xlings.platform;

namespace xlings::xself {

namespace fs = std::filesystem;

// Base shim names (always created)
inline constexpr std::array<std::string_view, 5> SHIM_NAMES_BASE = {
    "xlings", "xim", "xinstall", "xsubos", "xself"
};

// Optional shims (created only when pkg_root/bin/<name> exists)
inline constexpr std::array<std::string_view, 0> SHIM_NAMES_OPTIONAL = {};

export enum class LinkResult { Symlink, Hardlink, Copy, Failed };

export bool is_builtin_shim(std::string_view name);
export bool is_bootstrap_home_root(const fs::path& root);
export fs::path xlings_binary_in_home(const fs::path& home_dir);
export LinkResult create_shim(const fs::path& source, const fs::path& target);
export void ensure_subos_shims(const fs::path& target_bin_dir,
                               const fs::path& shim_src,
                               const fs::path& pkg_root);
export bool ensure_home_layout(const fs::path& home_dir);

bool is_builtin_shim(std::string_view name) {
    for (auto n : SHIM_NAMES_BASE)
        if (n == name) return true;
    for (auto n : SHIM_NAMES_OPTIONAL)
        if (n == name) return true;
    return false;
}

bool is_bootstrap_home_root(const fs::path& root) {
    std::error_code ec;
    if (root.empty() || !fs::exists(root / ".xlings.json", ec)) return false;
    if (!fs::exists(root / "bin", ec) || !fs::is_directory(root / "bin", ec)) return false;
#ifdef _WIN32
    return fs::exists(root / "bin" / "xlings.exe", ec);
#else
    return fs::exists(root / "bin" / "xlings", ec);
#endif
}

fs::path xlings_binary_in_home(const fs::path& home_dir) {
#ifdef _WIN32
    auto bin = home_dir / "bin" / "xlings.exe";
#else
    auto bin = home_dir / "bin" / "xlings";
#endif
    if (fs::exists(bin)) return bin;
    return {};
}

// Unified shim creation: symlink (Unix) > hardlink > copy
LinkResult create_shim(const fs::path& source, const fs::path& target) {
    std::error_code ec;

    if (!fs::exists(source, ec)) return LinkResult::Failed;

    // Remove existing target (file or symlink)
    if (fs::exists(target, ec) || fs::is_symlink(target, ec)) {
        ec.clear();
        fs::remove(target, ec);
        ec.clear();
    }

#if !defined(_WIN32)
    // Unix: prefer relative symlink
    auto rel = fs::relative(source, target.parent_path(), ec);
    if (!ec && !rel.empty()) {
        fs::create_symlink(rel, target, ec);
        if (!ec) return LinkResult::Symlink;
    }
    ec.clear();
    // Fallback: absolute symlink
    fs::create_symlink(source, target, ec);
    if (!ec) return LinkResult::Symlink;
    ec.clear();
#endif

    // Hardlink (Unix fallback / Windows primary)
    fs::create_hard_link(source, target, ec);
    if (!ec) return LinkResult::Hardlink;
    ec.clear();

    // Final fallback: copy
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (!ec) return LinkResult::Copy;

    log::error("[xlings:self]: failed to create shim {} - {}",
        target.string(), ec.message());
    return LinkResult::Failed;
}

void ensure_subos_shims(const fs::path& target_bin_dir,
                        const fs::path& shim_src,
                        const fs::path& pkg_root) {
    if (!fs::exists(shim_src)) return;

    std::string ext = shim_src.extension().string();

    for (auto name : SHIM_NAMES_BASE) {
        auto dst = target_bin_dir / (std::string(name) + ext);
        create_shim(shim_src, dst);
    }

    if (!pkg_root.empty()) {
        auto bin_dir = pkg_root / "bin";
        for (auto name : SHIM_NAMES_OPTIONAL) {
            auto opt_bin = bin_dir / (std::string(name) + ext);
            if (fs::exists(opt_bin)) {
                auto dst = target_bin_dir / (std::string(name) + ext);
                create_shim(shim_src, dst);
            }
        }
    }

    platform::make_files_executable(target_bin_dir);
}

static void ensure_parent_dirs_(const fs::path& file) {
    std::error_code ec;
    if (!file.parent_path().empty()) fs::create_directories(file.parent_path(), ec);
}

static void write_if_missing_(const fs::path& path, std::string_view content) {
    if (fs::exists(path)) return;
    ensure_parent_dirs_(path);
    platform::write_string_to_file(path.string(), std::string(content));
}

static void ensure_home_config_defaults_(const fs::path& home_dir) {
    auto config_path = home_dir / ".xlings.json";
    nlohmann::json json = nlohmann::json::object();

    if (fs::exists(config_path)) {
        try {
            auto content = platform::read_file_to_string(config_path.string());
            auto parsed = nlohmann::json::parse(content, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) json = std::move(parsed);
        } catch (...) {}
    }

    if (!json.contains("activeSubos") || !json["activeSubos"].is_string() ||
        json["activeSubos"].get<std::string>().empty()) {
        json["activeSubos"] = "default";
    }
    if (!json.contains("subos") || !json["subos"].is_object()) {
        json["subos"] = nlohmann::json::object();
    }
    if (!json["subos"].contains("default") || !json["subos"]["default"].is_object()) {
        json["subos"]["default"] = {{"dir", ""}};
    }

    ensure_parent_dirs_(config_path);
    platform::write_string_to_file(config_path.string(), json.dump(2));
}

bool ensure_home_layout(const fs::path& home_dir) {
    std::error_code ec;
    if (home_dir.empty()) return false;

    auto default_subos = home_dir / "subos" / "default";
    auto dirs = {
        home_dir,
        home_dir / "bin",
        home_dir / "config" / "shell",
        home_dir / "data" / "xpkgs",
        home_dir / "data" / "runtimedir",
        home_dir / "data" / "xim-index-repos",
        home_dir / "data" / "local-indexrepo",
        default_subos / "bin",
        default_subos / "lib",
        default_subos / "usr",
        default_subos / "generations",
    };

    for (auto& dir : dirs) {
        fs::create_directories(dir, ec);
        if (ec) {
            log::error("[xlings:self]: failed to create {} - {}", dir.string(), ec.message());
            return false;
        }
    }

    auto current_link = home_dir / "subos" / "current";
    platform::create_directory_link(current_link, default_subos);

    write_if_missing_(default_subos / ".xlings.json", "{\"workspace\":{}}");
    write_if_missing_(home_dir / "data" / "xim-index-repos" / "xim-indexrepos.json", "{}");
    write_if_missing_(home_dir / "config" / "shell" / "xlings-profile.sh",
R"XLINGSSH(# Xlings Shell Profile (bash/zsh)

_xlings_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." 2>/dev/null && pwd)"
if [ -n "$_xlings_dir" ]; then
    export XLINGS_HOME="$_xlings_dir"
fi
unset _xlings_dir

export XLINGS_BIN="$XLINGS_HOME/subos/current/bin"

case ":$PATH:" in
    *":$XLINGS_BIN:"*) ;;
    *) export PATH="$XLINGS_BIN:$XLINGS_HOME/bin:$PATH" ;;
esac
)XLINGSSH");
    write_if_missing_(home_dir / "config" / "shell" / "xlings-profile.fish",
R"XLINGSFISH(# Xlings Shell Profile (fish)

set -l _script_dir (dirname (status filename))
set -gx XLINGS_HOME (dirname (dirname "$_script_dir"))

set -gx XLINGS_BIN "$XLINGS_HOME/subos/current/bin"

if not contains "$XLINGS_BIN" $PATH
    set -gx PATH "$XLINGS_BIN" "$XLINGS_HOME/bin" $PATH
end
)XLINGSFISH");
    write_if_missing_(home_dir / "config" / "shell" / "xlings-profile.ps1",
R"XLINGSPS(# Xlings Shell Profile (PowerShell)

$env:XLINGS_HOME = (Resolve-Path "$PSScriptRoot\..\..").Path

$env:XLINGS_BIN = "$env:XLINGS_HOME\subos\current\bin"

if ($env:Path -notlike "*$env:XLINGS_BIN*") {
    $env:Path = "$env:XLINGS_BIN;$env:XLINGS_HOME\bin;$env:Path"
}
)XLINGSPS");

    ensure_home_config_defaults_(home_dir);

    auto xlings_bin = xlings_binary_in_home(home_dir);
    if (!xlings_bin.empty()) ensure_subos_shims(default_subos / "bin", xlings_bin, home_dir);

    return true;
}

// `xlings self init` — bootstrap (or repair) the home directory layout.
// Thin wrapper over ensure_home_layout; kept here so the dispatcher in
// xself.cppm can route by command name without pulling Config into a
// separate translation unit.
export int cmd_init() {
    auto& p = Config::paths();
    if (!ensure_home_layout(p.homeDir)) return 1;
    log::info("init ok");
    return 0;
}

} // namespace xlings::xself
