#!/usr/bin/env bash
# E2E test: install linux-headers and verify pkginfo.install_dir() works
# by checking that header files are copied into the subos sysroot.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir pkginfo_installdir_home)"
SCODE_REPO="$ROOT_DIR/tests/fixtures/xim-pkgindex-scode"

cleanup() {
  rm -rf "$HOME_DIR" "$SCODE_REPO"
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
  # Restore fixture linux-headers.lua
  (cd "$FIXTURE_INDEX_DIR" && git checkout -- . 2>/dev/null || true)
}
trap cleanup EXIT
cleanup

# ── 1. Create mock scode sub-repo with linux-headers package ──
mkdir -p "$SCODE_REPO/pkgs/l"
cat > "$SCODE_REPO/pkgs/l/linux-headers.lua" <<'LUAEOF'
package = {
    name = "linux-headers",
    description = "Mock Linux Kernel Headers (scode)",
    authors = "test",
    license = "GPL",
    repo = "https://example.com/linux-headers",
    type = "package",
    xpm = {
        linux = { ["5.11.1"] = {} },
        macosx = { ["5.11.1"] = {} },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

function installed() return false end

function install()
    local dir = pkginfo.install_dir()
    os.mkdir(path.join(dir, "include", "linux"))
    io.writefile(path.join(dir, "include", "linux", "errno.h"),
        "/* mock linux/errno.h */")
    xvm.add("linux-headers")
    return true
end

function uninstall()
    xvm.remove("linux-headers")
    return true
end
LUAEOF
(cd "$SCODE_REPO" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Patch fixture linux-headers.lua to use pkginfo.install_dir ──
cat > "$FIXTURE_INDEX_DIR/pkgs/l/linux-headers.lua" <<'LUAEOF'
package = {
    spec = "1",
    name = "linux-headers",
    description = "Linux Kernel Header",
    licenses = {"GPL"},
    repo = "https://github.com/torvalds/linux",
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
    if not scodedir then
        error("pkginfo.install_dir('scode:linux-headers') returned nil")
    end

    log.info("Copying linux header files to subos rootfs ...")
    local sysroot_usrdir = path.join(system.subos_sysrootdir(), "usr")
    if not os.isdir(sysroot_usrdir) then os.mkdir(sysroot_usrdir) end
    os.cp(path.join(scodedir, "include"), sysroot_usrdir, {
        force = true, symlink = true
    })

    xvm.add("linux-headers")
    return true
end

function uninstall()
    xvm.remove("linux-headers")
    return true
end
LUAEOF
(cd "$FIXTURE_INDEX_DIR" && git add -A && git commit -q -m "patch linux-headers" 2>/dev/null || true)

# ── 3. Write xim-indexrepos.lua with scode sub-repo ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["scode"] = {
        ["GLOBAL"] = "file://$SCODE_REPO",
    }
}
LUAEOF

# ── 4. Set up XLINGS_HOME ──
write_home_config "$HOME_DIR" "GLOBAL"

# ── 5. Sync repos ──
log "Running xlings update..."
run_xlings "$HOME_DIR" "$ROOT_DIR" update

# ── 6. Install linux-headers ──
log "Installing linux-headers..."
INSTALL_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" install linux-headers -y 2>&1)" || true
echo "$INSTALL_OUT"

# ── 7. Verify scode:linux-headers was installed under namespace ──
SCODE_DIR="$HOME_DIR/data/xpkgs/scode-x-linux-headers/5.11.1"
[[ -d "$SCODE_DIR" ]] \
  || fail "scode-x-linux-headers not installed at $SCODE_DIR"

[[ -f "$SCODE_DIR/include/linux/errno.h" ]] \
  || fail "errno.h not found in scode-x-linux-headers install dir"

# ── 8. Verify config hook copied headers into subos sysroot ──
SYSROOT_ERRNO="$HOME_DIR/subos/default/usr/include/linux/errno.h"
[[ -f "$SYSROOT_ERRNO" ]] \
  || fail "linux/errno.h not found in subos sysroot ($SYSROOT_ERRNO)"

log "PASS: linux-headers install with pkginfo.install_dir"
