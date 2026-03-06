#!/usr/bin/env bash
# E2E test: install linux-headers via scode sub-index,
# verify pkginfo.install_dir() copies headers into subos sysroot.
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
  (cd "$FIXTURE_INDEX_DIR" && git checkout -- . 2>/dev/null || true)
}
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

# ── 1. Create mock scode sub-repo (linux-headers creates include/linux/errno.h) ──
mkdir -p "$SCODE_REPO/pkgs/l"
cat > "$SCODE_REPO/pkgs/l/linux-headers.lua" <<'LUA'
package = {
    name = "linux-headers",
    description = "Mock scode linux-headers",
    authors = "test", license = "GPL",
    repo = "https://example.com/linux-headers",
    type = "package",
    xpm = {
        linux  = { ["5.11.1"] = {} },
        macosx = { ["5.11.1"] = {} },
    },
}
import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
function installed() return false end
function install()
    local dir = pkginfo.install_dir()
    os.mkdir(path.join(dir, "include", "linux"))
    io.writefile(path.join(dir, "include", "linux", "errno.h"), "/* mock */")
    xvm.add("linux-headers")
    return true
end
function uninstall() xvm.remove("linux-headers"); return true end
LUA
(cd "$SCODE_REPO" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Patch fixture: add scode sub-index + update linux-headers.lua ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<CONF
xim_indexrepos = {
    ["scode"] = { ["GLOBAL"] = "file://$SCODE_REPO" }
}
CONF

cat > "$FIXTURE_INDEX_DIR/pkgs/l/linux-headers.lua" <<'LUA'
package = {
    spec = "1",
    name = "linux-headers",
    description = "Linux Kernel Header",
    licenses = {"GPL"},
    repo = "https://github.com/torvalds/linux",
    type = "package", archs = {"x86_64"}, status = "stable",
    xvm_enable = true,
    xpm = {
        linux = { ["latest"] = { ref = "5.11.1" }, ["5.11.1"] = {} },
    },
}
import("xim.libxpkg.pkginfo")
import("xim.libxpkg.system")
import("xim.libxpkg.pkgmanager")
import("xim.libxpkg.xvm")
import("xim.libxpkg.log")
function install()
    pkgmanager.install("scode:linux-headers@" .. pkginfo.version())
    return true
end
function config()
    local scodedir = pkginfo.install_dir("scode:linux-headers", pkginfo.version())
    if not scodedir then error("pkginfo.install_dir('scode:linux-headers') returned nil") end
    log.info("Copying linux header files to subos rootfs ...")
    local sysroot_usrdir = path.join(system.subos_sysrootdir(), "usr")
    if not os.isdir(sysroot_usrdir) then os.mkdir(sysroot_usrdir) end
    os.cp(path.join(scodedir, "include"), sysroot_usrdir, { force = true, symlink = true })
    xvm.add("linux-headers")
    return true
end
function uninstall() xvm.remove("linux-headers"); return true end
LUA
(cd "$FIXTURE_INDEX_DIR" && git add -A && git commit -q -m "add scode sub-index" 2>/dev/null || true)

# ── 3. Update + Install ──
(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" update)

log "Installing linux-headers..."
INSTALL_OUT="$(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" install linux-headers -y 2>&1)" || true
echo "$INSTALL_OUT"

# ── 4. Verify ──
[[ -f "$HOME_DIR/data/xpkgs/scode-x-linux-headers/5.11.1/include/linux/errno.h" ]] \
  || fail "scode-x-linux-headers not installed correctly"

[[ -f "$HOME_DIR/subos/default/usr/include/linux/errno.h" ]] \
  || fail "linux/errno.h not found in subos sysroot"

log "PASS: linux-headers install (pkginfo.install_dir)"
