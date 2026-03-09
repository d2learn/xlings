#!/usr/bin/env bash
# E2E test: verify that packages from sub-index repos can be installed
# both with explicit namespace (d2x:d2mcpp) and bare name (d2mcpp).
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir sub_index_install_home)"
D2X_REPO="$ROOT_DIR/tests/fixtures/xim-pkgindex-testd2x-install"

cleanup() {
  rm -rf "$HOME_DIR"
  rm -rf "$D2X_REPO"
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
}
trap cleanup EXIT
cleanup  # start fresh

# ── 1. Create a sub-index fixture with pkgindex-build.lua ──
mkdir -p "$D2X_REPO/pkgs/d"
cat > "$D2X_REPO/pkgs/d/d2testpkg.lua" <<'LUAEOF'
package = {
    name = "d2testpkg",
    description = "Test d2x package for install",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/d2testpkg",
}
LUAEOF
cat > "$D2X_REPO/template.lua" <<'LUAEOF'

package.type = "courses"
package.xpm = {
    linux = { ["latest"] = { url = package.repo .. ".git" } },
    macosx = { ["latest"] = { url = package.repo .. ".git" } },
    windows = { ["latest"] = { url = package.repo .. ".git" } },
}

import("platform")

function installed()
    return false
end

function install()
    return true
end

function uninstall()
    return true
end
LUAEOF
cat > "$D2X_REPO/pkgindex-build.lua" <<'LUAEOF'
package = {
    name = "pkgindex-update",
    description = "Test pkgindex build script",
    xpm = { linux = { ["latest"] = {} } },
}
local projectdir = os.scriptdir()
local pkgsdir = path.join(projectdir, "pkgs")
local template = path.join(projectdir, "template.lua")
function installed() return false end
function install()
    local files = os.files(path.join(pkgsdir, "**.lua"))
    local template_content = io.readfile(template)
    for _, file in ipairs(files) do
        if not file:endswith("pkgindex-update.lua") then
            io.writefile(file, io.readfile(file) .. template_content)
        end
    end
    return true
end
function uninstall() return true end
LUAEOF
(cd "$D2X_REPO" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Write xim-indexrepos.lua ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["testd2x"] = {
        ["GLOBAL"] = "file://$D2X_REPO",
    }
}
LUAEOF

# ── 3. Set up XLINGS_HOME ──
write_home_config "$HOME_DIR" "GLOBAL"

# ── 4. Sync repos ──
log "Running xlings update..."
run_xlings "$HOME_DIR" "$ROOT_DIR" update

# ── 5. Verify sub-repo was synced ──
SUB_REPO_SYNCED="$HOME_DIR/data/xim-index-repos/xim-pkgindex-testd2x-install"
[[ -d "$SUB_REPO_SYNCED/pkgs" ]] || fail "sub-index repo was not synced"

# ── 6. Search should find the package ──
log "Searching for d2testpkg..."
SEARCH_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search d2testpkg 2>&1)"
echo "$SEARCH_OUT"
echo "$SEARCH_OUT" | grep -q "d2testpkg" \
  || fail "search did not find d2testpkg"

# ── 7. Install with explicit namespace (testd2x:d2testpkg) ──
log "Installing testd2x:d2testpkg..."
INSTALL_NS_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" install testd2x:d2testpkg -y 2>&1)" || true
echo "$INSTALL_NS_OUT"
echo "$INSTALL_NS_OUT" | grep -q "d2testpkg" \
  || fail "install testd2x:d2testpkg did not resolve package"

# ── 8. Clean up installed package for next test ──
rm -rf "$HOME_DIR/data/xpkgs/testd2x-x-d2testpkg"

# ── 9. Install with bare name (d2testpkg) - should auto-resolve ──
log "Installing d2testpkg (bare name)..."
INSTALL_BARE_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" install d2testpkg -y 2>&1)" || true
echo "$INSTALL_BARE_OUT"
echo "$INSTALL_BARE_OUT" | grep -q "d2testpkg" \
  || fail "install d2testpkg (bare name) did not resolve package - sub-repo packages should be resolvable by bare name"

log "PASS: sub-index repo install (namespace + bare name)"
