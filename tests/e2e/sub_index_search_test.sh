#!/usr/bin/env bash
# E2E test: verify that packages from sub-index repos (discovered via
# xim-indexrepos.lua in the main repo) are visible to 'xlings search'.
# Also tests pkgindex-build.lua support (template appending).
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir sub_index_search_home)"

cleanup() {
  rm -rf "$HOME_DIR"
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
  rm -rf "$ROOT_DIR/tests/fixtures/xim-pkgindex-testd2x"
  rm -rf "$ROOT_DIR/tests/fixtures/xim-pkgindex-testbuild"
}
trap cleanup EXIT
cleanup  # start fresh

# ── 1. Create a sub-index fixture with a metadata-only package ──
SUB_INDEX_DIR="$ROOT_DIR/tests/fixtures/xim-pkgindex-testd2x"
mkdir -p "$SUB_INDEX_DIR/pkgs/t"
cat > "$SUB_INDEX_DIR/pkgs/t/testpkg-d2x.lua" <<'LUAEOF'
package = {
    name = "testpkg-d2x",
    description = "Test package from sub-index repo",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/test",
}
LUAEOF
(cd "$SUB_INDEX_DIR" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Create a sub-index with pkgindex-build.lua (template appending) ──
BUILD_INDEX_DIR="$ROOT_DIR/tests/fixtures/xim-pkgindex-testbuild"
mkdir -p "$BUILD_INDEX_DIR/pkgs/b"
cat > "$BUILD_INDEX_DIR/pkgs/b/buildpkg.lua" <<'LUAEOF'
package = {
    name = "buildpkg",
    description = "Test package requiring pkgindex-build",
    authors = "test",
    license = "MIT",
    repo = "https://example.com/buildpkg",
}
LUAEOF
cat > "$BUILD_INDEX_DIR/template.lua" <<'LUAEOF'

package.type = "courses"
package.xpm = {
    linux = { ["latest"] = { url = package.repo .. ".git" } },
    macosx = { ["latest"] = { url = package.repo .. ".git" } },
    windows = { ["latest"] = { url = package.repo .. ".git" } },
}
LUAEOF
cat > "$BUILD_INDEX_DIR/pkgindex-build.lua" <<'LUAEOF'
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
(cd "$BUILD_INDEX_DIR" && git init -q && git add -A && git commit -q -m "init")

# ── 3. Write xim-indexrepos.lua (must match upstream multi-line format) ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["testd2x"] = {
        ["GLOBAL"] = "file://$SUB_INDEX_DIR",
    },
    ["testbuild"] = {
        ["GLOBAL"] = "file://$BUILD_INDEX_DIR",
    }
}
LUAEOF

# ── 4. Set up XLINGS_HOME ──
write_home_config "$HOME_DIR" "GLOBAL"

# ── 5. Run update to sync repos ──
log "Running xlings update..."
run_xlings "$HOME_DIR" "$ROOT_DIR" update

# ── 6. Verify sub-repos were synced ──
SUB_REPO_SYNCED="$HOME_DIR/data/xim-index-repos/xim-pkgindex-testd2x"
[[ -d "$SUB_REPO_SYNCED/pkgs" ]] || fail "sub-index repo testd2x was not synced"

BUILD_REPO_SYNCED="$HOME_DIR/data/xim-index-repos/xim-pkgindex-testbuild"
[[ -d "$BUILD_REPO_SYNCED/pkgs" ]] || fail "sub-index repo testbuild was not synced"

# ── 7. Verify xim-indexrepos.json ──
JSON_FILE="$HOME_DIR/data/xim-index-repos/xim-indexrepos.json"
[[ -f "$JSON_FILE" ]] || fail "xim-indexrepos.json was not created"
grep -q '"testd2x"' "$JSON_FILE" || fail "testd2x not in xim-indexrepos.json"
grep -q '"testbuild"' "$JSON_FILE" || fail "testbuild not in xim-indexrepos.json"

# ── 8. Search metadata-only package ──
log "Running xlings search testpkg-d2x..."
SEARCH_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search testpkg-d2x 2>&1)"
echo "$SEARCH_OUT"
assert_contains "$SEARCH_OUT" "testpkg-d2x" "search did not find testpkg-d2x"
assert_contains "$SEARCH_OUT" "testd2x" "search result missing testd2x namespace"

# ── 9. Search by namespace prefix ──
log "Running xlings search testpkg..."
SEARCH_NS_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search testpkg 2>&1)"
echo "$SEARCH_NS_OUT"
assert_contains "$SEARCH_NS_OUT" "testd2x:testpkg-d2x" "namespace search did not find testd2x:testpkg-d2x"

# ── 10. Search for pkgindex-build package ──
log "Running xlings search buildpkg..."
SEARCH_BUILD_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search buildpkg 2>&1)"
echo "$SEARCH_BUILD_OUT"
assert_contains "$SEARCH_BUILD_OUT" "testbuild:buildpkg" "search did not find testbuild:buildpkg"

# ── 11. Verify built package has xpm versions (info shows "latest") ──
log "Running xlings info testbuild:buildpkg..."
INFO_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" info testbuild:buildpkg 2>&1)" || true
echo "$INFO_OUT"
assert_contains "$INFO_OUT" "latest" "info for testbuild:buildpkg should show 'latest' version"

log "PASS: sub-index repo search + pkgindex-build"
