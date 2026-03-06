#!/usr/bin/env bash
# E2E test: install linux-headers via workspace config,
# verify headers are copied into anonymous subos sysroot via pkginfo.install_dir.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/linux_headers"
HOME_DIR="$(runtime_home_dir linux_headers_home)"
SCODE_REPO="$ROOT_DIR/tests/fixtures/xim-pkgindex-scode"

CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
  rm -rf "$SCODE_REPO"
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
}
trap cleanup EXIT

# ── 1. Create a scode sub-index with a mock linux-headers package ──
mkdir -p "$SCODE_REPO/pkgs/l"
cat > "$SCODE_REPO/pkgs/l/linux-headers.lua" <<'LUAEOF'
package = {
    spec = "1",
    name = "linux-headers",
    description = "Mock Linux Kernel Headers (source code)",
    type = "package",
    archs = {"x86_64"},
    status = "stable",
    xvm_enable = true,
    xpm = {
        linux = {
            ["latest"] = { ref = "5.11.1" },
            ["5.11.1"] = { },
        },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
import("xim.libxpkg.system")
import("xim.libxpkg.log")

function install()
    -- Create mock header files in the install directory
    local idir = pkginfo.install_dir()
    local header_dir = path.join(idir, "include", "linux")
    os.mkdir(header_dir)
    io.writefile(path.join(header_dir, "errno.h"), "/* mock linux/errno.h */\n")
    return true
end

function config()
    local scodedir = pkginfo.install_dir("scode:linux-headers", pkginfo.version())

    log.info("Copying linux header files to subos rootfs ...")
    local sysroot_usrdir = path.join(system.subos_sysrootdir(), "usr")
    if not os.isdir(sysroot_usrdir) then os.mkdir(sysroot_usrdir) end
    os.cp(path.join(scodedir, "include"), sysroot_usrdir, {
        force = true, symlink = true
    })

    xvm.add("scode-linux-headers")
    return true
end

function uninstall()
    xvm.remove("scode-linux-headers")
    return true
end
LUAEOF
(cd "$SCODE_REPO" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Register scode as a sub-index of xim ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["scode"] = {
        ["GLOBAL"] = "file://$SCODE_REPO",
    }
}
LUAEOF

# ── 3. Set up home and run ──
write_home_config "$HOME_DIR" "GLOBAL"

(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" update)
(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" install -y 2>&1) | tee /dev/stderr

# Anonymous subos should be created at project/.xlings/subos/_/
ANON_SUBOS="$SCENARIO_DIR/.xlings/subos/_"
[[ -d "$ANON_SUBOS" ]] \
  || fail "anonymous subos dir not created at $ANON_SUBOS"

[[ -f "$ANON_SUBOS/usr/include/linux/errno.h" ]] \
  || fail "linux/errno.h not found in anonymous subos sysroot"

log "PASS: linux-headers install (pkginfo.install_dir + anonymous subos)"
