#!/usr/bin/env bash
# E2E test: verify pkginfo.install_dir() can find packages across xpkgs root
# with namespace-aware matching (scode:linux-headers -> scode-x-linux-headers)
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir pkginfo_installdir_home)"
SUB_REPO="$ROOT_DIR/tests/fixtures/xim-pkgindex-testsub-installdir"

cleanup() {
  rm -rf "$HOME_DIR" "$SUB_REPO"
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
}
trap cleanup EXIT
cleanup

# ── 1. Create a sub-index with "dep-lib" package (simulates scode:linux-headers) ──
mkdir -p "$SUB_REPO/pkgs/d"
cat > "$SUB_REPO/pkgs/d/dep-lib.lua" <<'LUAEOF'
package = {
    name = "dep-lib",
    description = "Test dependency library",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/dep-lib",
    type = "package",
    xpm = {
        linux = { ["1.0.0"] = {} },
        macosx = { ["1.0.0"] = {} },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

function installed() return false end

function install()
    local dir = pkginfo.install_dir()
    os.mkdir(path.join(dir, "include"))
    io.writefile(path.join(dir, "include", "dep.h"), "// dep header")
    xvm.add("dep-lib")
    return true
end

function uninstall()
    xvm.remove("dep-lib")
    return true
end
LUAEOF
(cd "$SUB_REPO" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Create main-index "consumer" package that uses pkginfo.install_dir ──
mkdir -p "$FIXTURE_INDEX_DIR/pkgs/c"
cat > "$FIXTURE_INDEX_DIR/pkgs/c/consumer-pkg.lua" <<'LUAEOF'
package = {
    name = "consumer-pkg",
    description = "Test package that uses pkginfo.install_dir to find dep-lib",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/consumer-pkg",
    type = "package",
    xpm = {
        linux = { ["1.0.0"] = {} },
        macosx = { ["1.0.0"] = {} },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.pkgmanager")
import("xim.libxpkg.xvm")
import("xim.libxpkg.log")

function installed() return false end

function install()
    pkgmanager.install("testsub:dep-lib@1.0.0")
    return true
end

function config()
    -- Use namespace-aware lookup: "testsub:dep-lib" -> matches "testsub-x-dep-lib"
    local depdir = pkginfo.install_dir("testsub:dep-lib", "1.0.0")
    if not depdir then
        error("pkginfo.install_dir('testsub:dep-lib', '1.0.0') returned nil")
    end
    log.info("Found dep-lib at: " .. depdir)

    -- Verify the include dir exists
    local incdir = path.join(depdir, "include", "dep.h")
    if not os.isfile(incdir) then
        error("dep.h not found at " .. incdir)
    end
    log.info("dep.h verified at: " .. incdir)

    xvm.add("consumer-pkg")
    return true
end

function uninstall()
    xvm.remove("consumer-pkg")
    return true
end
LUAEOF
(cd "$FIXTURE_INDEX_DIR" && git add -A && git commit -q -m "add consumer-pkg" 2>/dev/null || true)

# ── 3. Write xim-indexrepos.lua ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["testsub"] = {
        ["GLOBAL"] = "file://$SUB_REPO",
    }
}
LUAEOF

# ── 4. Set up XLINGS_HOME ──
write_home_config "$HOME_DIR" "GLOBAL"

# ── 5. Sync repos ──
log "Running xlings update..."
run_xlings "$HOME_DIR" "$ROOT_DIR" update

# ── 6. Install consumer-pkg (triggers dep-lib install via pkgmanager, then config) ──
log "Installing consumer-pkg..."
INSTALL_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" install consumer-pkg -y 2>&1)" || true
echo "$INSTALL_OUT"

# ── 7. Verify dep-lib was installed under testsub namespace ──
DEP_DIR="$HOME_DIR/data/xpkgs/testsub-x-dep-lib/1.0.0"
[[ -d "$DEP_DIR" ]] \
  || fail "testsub-x-dep-lib was not installed at $DEP_DIR"

[[ -f "$DEP_DIR/include/dep.h" ]] \
  || fail "dep.h not found in installed dep-lib"

# ── 8. Verify consumer-pkg config hook succeeded (found dep-lib via pkginfo.install_dir) ──
echo "$INSTALL_OUT" | grep -q "Found dep-lib at:" \
  || fail "config hook did not find dep-lib via pkginfo.install_dir"

echo "$INSTALL_OUT" | grep -q "dep.h verified" \
  || fail "config hook did not verify dep.h"

# ── 9. Cleanup fixture index changes ──
rm -f "$FIXTURE_INDEX_DIR/pkgs/c/consumer-pkg.lua"
(cd "$FIXTURE_INDEX_DIR" && git checkout -- . 2>/dev/null || true)

log "PASS: pkginfo.install_dir namespace-aware lookup"
