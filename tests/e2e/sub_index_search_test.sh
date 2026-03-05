#!/usr/bin/env bash
# E2E test: verify that packages from sub-index repos (discovered via
# xim-indexrepos.lua in the main repo) are visible to 'xlings search'.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir sub_index_search_home)"

cleanup() {
  rm -rf "$HOME_DIR"
  # Remove the xim-indexrepos.lua we added to the fixture
  rm -f "$FIXTURE_INDEX_DIR/xim-indexrepos.lua"
  # Remove the sub-fixture
  rm -rf "$ROOT_DIR/tests/fixtures/xim-pkgindex-testd2x"
}
trap cleanup EXIT
cleanup  # start fresh

# ── 1. Create a sub-index fixture with a test package ──
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

# Initialize as a git repo (required for sync_repo to recognize it)
(cd "$SUB_INDEX_DIR" && git init -q && git add -A && git commit -q -m "init")

# ── 2. Write xim-indexrepos.lua in the main fixture pointing to sub-index ──
cat > "$FIXTURE_INDEX_DIR/xim-indexrepos.lua" <<LUAEOF
xim_indexrepos = {
    ["testd2x"] = {
        ["GLOBAL"] = "file://$SUB_INDEX_DIR",
        ["CN"] = "file://$SUB_INDEX_DIR",
    }
}
LUAEOF

# ── 3. Set up XLINGS_HOME ──
write_home_config "$HOME_DIR" "GLOBAL"

# ── 4. Run update to sync repos (including sub-repos) ──
log "Running xlings update..."
run_xlings "$HOME_DIR" "$ROOT_DIR" update

# ── 5. Verify sub-repo was synced ──
SUB_REPO_SYNCED="$HOME_DIR/data/xim-index-repos/xim-pkgindex-testd2x"
[[ -d "$SUB_REPO_SYNCED/pkgs" ]] || fail "sub-index repo was not synced to $SUB_REPO_SYNCED"

# ── 6. Verify xim-indexrepos.json was written with the sub-repo ──
JSON_FILE="$HOME_DIR/data/xim-index-repos/xim-indexrepos.json"
[[ -f "$JSON_FILE" ]] || fail "xim-indexrepos.json was not created"
grep -q '"testd2x"' "$JSON_FILE" || fail "testd2x not found in xim-indexrepos.json"

# ── 7. Search for the sub-index package ──
log "Running xlings search testpkg-d2x..."
SEARCH_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search testpkg-d2x 2>&1)"
echo "$SEARCH_OUT"

echo "$SEARCH_OUT" | grep -q "testpkg-d2x" \
  || fail "search did not find testpkg-d2x from sub-index repo"
echo "$SEARCH_OUT" | grep -q "testd2x" \
  || fail "search result does not show testd2x namespace"

# ── 8. Search by namespace prefix ──
log "Running xlings search testd2x:testpkg..."
SEARCH_NS_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" search testpkg 2>&1)"
echo "$SEARCH_NS_OUT"
echo "$SEARCH_NS_OUT" | grep -q "testd2x:testpkg-d2x" \
  || fail "search with namespace did not find testd2x:testpkg-d2x"

log "PASS: sub-index repo search"
