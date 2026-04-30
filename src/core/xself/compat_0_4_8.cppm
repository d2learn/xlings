// COMPAT(0.4.8 → drop in 0.6.0)
//
// Migration code for "short-command aliases removed in 0.4.8".
//
// 0.4.8 collapsed the multicall surface to a single canonical entry point
// (`xlings`). The five legacy aliases — xim / xvm / xself / xsubos / xinstall
// — were created as symlinks-to-bootstrap by xlings ≤ 0.4.7 and are now
// migration artifacts. Everything below exists only to ease the upgrade
// path for users who still have those leftover shims on disk.
//
// This module is deliberately a single, isolated translation unit so the
// future removal is a one-shot operation:
//
//   1. Bump the codebase to >= 0.6.0
//   2. `git rm src/core/xself/compat_0_4_8.cppm`
//   3. Rebuild — every `import xlings.core.xself.compat_0_4_8;` and every
//      `xself::compat::` reference will surface as a hard build error.
//      Delete each, and any surrounding `COMPAT(0.4.8 → drop in 0.6.0)`
//      marker comments. No grep necessary.
//
// What lives here (all marked `export`, all temporary):
//   * LEGACY_ALIAS_NAMES — the 5 names, used by cleanup callers and
//     `xlings self doctor`.
//   * DEPRECATED_ALIASES — name + suggested-replacement message, used
//     only by main.cpp's argv[0] migration-error path.
//   * is_legacy_alias_symlink_to_bootstrap — safety predicate shared
//     between cleanup and doctor so they agree on what is "safe to remove".
//   * cleanup_legacy_alias_shims — silent removal of leftover shims, hooked
//     into every code path that re-pins the active bootstrap binary
//     (self init / self update / use xlings / install xlings --use).
//   * report_deprecated_alias_if_match — the user-facing migration error
//     printed when a removed alias is invoked directly.

export module xlings.core.xself.compat_0_4_8;

import std;
import xlings.core.log;

namespace xlings::xself::compat {

namespace fs = std::filesystem;

// Names that older xlings versions sprayed into ~/.xlings/bin as multicall
// symlinks → bootstrap. 0.4.8 collapses to a single canonical xlings entry,
// so any leftover entry of these names is a migration artifact.
export inline constexpr std::array<std::string_view, 5> LEGACY_ALIAS_NAMES = {
    "xim", "xvm", "xself", "xsubos", "xinstall"
};

// (alias name, suggested replacement command-line) — used only by main.cpp
// to print a helpful message when the user invokes a removed alias.
export struct DeprecatedAlias {
    std::string_view name;
    std::string_view replacement;
};
export inline constexpr std::array<DeprecatedAlias, 5> DEPRECATED_ALIASES = {{
    {"xim",      "xlings install/remove/search/list/info ..."},
    {"xvm",      "xlings use ..."},
    {"xself",    "xlings self ..."},
    {"xsubos",   "xlings subos ..."},
    {"xinstall", "xlings install ..."},
}};

// Safety predicate shared by cleanup_legacy_alias_shims and cmd_doctor:
// "is `path` a symlink whose target canonicalizes to `canonical_bootstrap`?".
// Both callers gate on this so they stay consistent on what counts as
// "safe to remove" — a user file that merely happens to share a name is
// never touched.
export bool is_legacy_alias_symlink_to_bootstrap(
    const fs::path& path,
    const fs::path& canonical_bootstrap);

// Remove leftover legacy alias symlinks under `bin_dir`. Hooked into every
// path that re-pins the active bootstrap binary so the cleanup happens
// opportunistically — the user never needs to run a separate migration
// step. Callers:
//   * `xlings self init`            (init.cppm)
//   * `xlings install xlings --use` (xim/installer.cppm self-replace path)
//   * `xlings use xlings <ver>`     (xvm/commands.cppm self-replace path)
//   * `xlings self doctor --fix`    (doctor.cppm)
export void cleanup_legacy_alias_shims(const fs::path& bin_dir,
                                       const fs::path& bootstrap_path);

// `main.cpp` calls this with the argv[0] basename. Returns true after
// printing the migration error to stderr; main.cpp then exits with code 2.
export bool report_deprecated_alias_if_match(std::string_view program_name);

bool is_legacy_alias_symlink_to_bootstrap(const fs::path& path,
                                          const fs::path& canonical_bootstrap)
{
    std::error_code ec;
    if (!fs::is_symlink(path, ec)) return false;
    ec.clear();
    auto target = fs::weakly_canonical(path, ec);
    if (ec) return false;
    return target == canonical_bootstrap;
}

void cleanup_legacy_alias_shims(const fs::path& bin_dir,
                                const fs::path& bootstrap_path) {
    std::error_code ec;
    auto canonical_bootstrap = fs::weakly_canonical(bootstrap_path, ec);
    if (ec) return;

    std::string ext = bootstrap_path.extension().string();
    for (auto name : LEGACY_ALIAS_NAMES) {
        auto path = bin_dir / (std::string(name) + ext);
        if (!is_legacy_alias_symlink_to_bootstrap(path, canonical_bootstrap))
            continue;
        ec.clear();
        fs::remove(path, ec);
        log::debug("[migrate] removed legacy alias shim: {}", path.string());
    }
}

bool report_deprecated_alias_if_match(std::string_view program_name) {
    for (auto& [alias, suggestion] : DEPRECATED_ALIASES) {
        if (alias == program_name) {
            std::println(std::cerr,
                "[error] `{}` was removed in 0.4.8. Use `{}` instead.",
                alias, suggestion);
            std::println(std::cerr,
                "        Run `xlings self doctor --fix` to clean up "
                "leftover shortcuts.");
            return true;
        }
    }
    return false;
}

} // namespace xlings::xself::compat
