#!/usr/bin/env bash
# E2E test: runtime/build deps split.
#
# Validates that:
#   1. The new schema `deps = { runtime = ..., build = ... }` is parsed
#      and propagated through resolve → install correctly.
#   2. Runtime deps activate in the workspace (PATH shim, xvm version).
#   3. Build deps land in xpkgs but are NOT activated (no shim, no
#      workspace mutation) — they exist only to support the consumer's
#      install hook.
#   4. The consumer's install hook sees XLINGS_BUILDDEP_<NAME>_PATH env
#      vars pointing at the build-dep payload directories, and the
#      build dep's bin/ is on PATH for the hook subprocess.
#   5. pkginfo.build_dep() returns the same install_dir / bin / version
#      from inside the consumer's install hook.
#
# Fixtures: tests/fixtures/xim-pkgindex/pkgs/{b,r}/{bdtool,rttool,bdconsumer}.lua
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

# Use a private LOCAL_INDEX_DIR (not the shared $FIXTURE_INDEX_DIR) because
# `xlings install` internally calls sync_all_repos when the catalog isn't
# loaded yet — that runs `git fetch + reset --hard` against the index repo
# and would clobber any fixture .lua we drop in. Following the same pattern
# as remove_multi_version_test.sh / xlings_self_replace_test.sh: clone-copy
# the shared fixture into a runtime-private directory, neutralise its
# sub-index repos to skip GitHub fetches, and inject the test-only fixtures
# there. The .xlings.json points "xim" at LOCAL_INDEX_DIR — sync sees it
# as a non-git path and leaves it alone.
RUNTIME_DIR="$ROOT_DIR/tests/e2e/runtime/build_deps_split"
HOME_DIR="$RUNTIME_DIR/home"
LOCAL_INDEX_DIR="$RUNTIME_DIR/xim-pkgindex"
SCENARIO_FIXTURES="$ROOT_DIR/tests/e2e/fixtures/build_deps_split"

cleanup() { rm -rf "$RUNTIME_DIR"; }
trap cleanup EXIT
cleanup

mkdir -p "$HOME_DIR/subos/default/bin"

# Private copy of the shared fixture index, neutralise sub-index repos.
cp -r "$FIXTURE_INDEX_DIR" "$LOCAL_INDEX_DIR"
printf 'xim_indexrepos = {}\n' > "$LOCAL_INDEX_DIR/xim-indexrepos.lua"
rm -f "$LOCAL_INDEX_DIR/.xlings-index-cache.json"

# Inject the test fixtures into the private index.
mkdir -p "$LOCAL_INDEX_DIR/pkgs/b" "$LOCAL_INDEX_DIR/pkgs/r"
cp "$SCENARIO_FIXTURES/bdtool.lua"     "$LOCAL_INDEX_DIR/pkgs/b/bdtool.lua"
cp "$SCENARIO_FIXTURES/bdconsumer.lua" "$LOCAL_INDEX_DIR/pkgs/b/bdconsumer.lua"
cp "$SCENARIO_FIXTURES/rttool.lua"     "$LOCAL_INDEX_DIR/pkgs/r/rttool.lua"

# Seed XLINGS_HOME pointing at the private (neutralised + injected) index.
cp "$(find_xlings_bin)" "$HOME_DIR/xlings"
cat > "$HOME_DIR/.xlings.json" <<EOF
{
  "mirror": "GLOBAL",
  "index_repos": [
    { "name": "xim", "url": "$LOCAL_INDEX_DIR" }
  ]
}
EOF

run_xlings "$HOME_DIR" "$ROOT_DIR" self init >/dev/null 2>&1 || fail "self init failed"
mkdir -p "$HOME_DIR/data/xim-index-repos"
printf '{}\n' > "$HOME_DIR/data/xim-index-repos/xim-indexrepos.json"

log "Installing bdconsumer (runtime_dep=rttool, build_dep=bdtool) ..."
INSTALL_OUT="$(run_xlings "$HOME_DIR" "$ROOT_DIR" install bdconsumer -y 2>&1)" || {
    echo "$INSTALL_OUT"
    fail "xlings install bdconsumer failed"
}
echo "$INSTALL_OUT"

CONSUMER_DIR="$HOME_DIR/data/xpkgs/xim-x-bdconsumer/1.0.0"
BDTOOL_DIR="$HOME_DIR/data/xpkgs/xim-x-bdtool/1.0.0"
RTTOOL_DIR="$HOME_DIR/data/xpkgs/xim-x-rttool/1.0.0"
BIN_DIR="$HOME_DIR/subos/default/bin"

# 1. All three payloads laid down on disk.
[[ -d "$CONSUMER_DIR" ]] || fail "bdconsumer payload missing at $CONSUMER_DIR"
[[ -d "$BDTOOL_DIR"   ]] || fail "bdtool payload missing at $BDTOOL_DIR"
[[ -d "$RTTOOL_DIR"   ]] || fail "rttool payload missing at $RTTOOL_DIR"

# 2. Runtime dep is activated: shim exists.
[[ -e "$BIN_DIR/rttool" ]] \
    || fail "rttool shim missing — runtime dep must activate in workspace"

# 3. Build dep is NOT activated: no shim, no workspace entry.
if [[ -e "$BIN_DIR/bdtool" ]]; then
    fail "bdtool shim should NOT exist — build deps must not pollute workspace PATH"
fi
if grep -q '"bdtool"' "$HOME_DIR/.xlings.json" 2>/dev/null; then
    log ".xlings.json contains 'bdtool' — checking it's only payload metadata, not workspace"
    if python3 -c "
import json, sys
d = json.load(open('$HOME_DIR/.xlings.json'))
ws = d.get('workspace', {})
sys.exit(0 if 'bdtool' not in ws else 1)
"; then
        log "  ok: bdtool is referenced but not in workspace map"
    else
        fail "bdtool should NOT appear in workspace map"
    fi
fi

# 4. Consumer's install hook captured the env var.
CAPTURED="$CONSUMER_DIR/captured_env.txt"
[[ -f "$CAPTURED" ]] || fail "captured_env.txt missing — install hook didn't run?"
log "captured env:"; sed 's/^/  /' "$CAPTURED"
EXPECTED="BDTOOL_PATH=$BDTOOL_DIR"
grep -qxF "$EXPECTED" "$CAPTURED" \
    || fail "expected '$EXPECTED' in captured_env.txt; got: $(cat "$CAPTURED")"

# RTTOOL env should be empty: runtime deps are activated via workspace,
# not via the build-dep env-var pathway.
if grep -q "^RTTOOL_ENV=$" "$CAPTURED"; then
    log "  ok: RTTOOL env var was empty (runtime deps don't use build-dep injection)"
else
    fail "RTTOOL env should be empty — runtime deps must not use XLINGS_BUILDDEP_*"
fi

# 5. bdtool was reachable on PATH during the install hook.
BDTOOL_OUT="$CONSUMER_DIR/bdtool_output.txt"
[[ -f "$BDTOOL_OUT" ]] || fail "bdtool_output.txt missing"
log "bdtool output during install: $(cat "$BDTOOL_OUT")"
grep -q "bdtool-1.0.0" "$BDTOOL_OUT" \
    || fail "bdtool was not reachable on PATH during install hook"

# 6. pkginfo.build_dep() returns expected fields.
API="$CONSUMER_DIR/build_dep_api.txt"
[[ -f "$API" ]] || fail "build_dep_api.txt missing"
log "pkginfo.build_dep('bdtool') ->"; sed 's/^/  /' "$API"
grep -qxF "path=$BDTOOL_DIR" "$API" \
    || fail "pkginfo.build_dep().path mismatch — got: $(grep '^path=' "$API")"
grep -qxF "bin=$BDTOOL_DIR/bin" "$API" \
    || fail "pkginfo.build_dep().bin mismatch — got: $(grep '^bin=' "$API")"
grep -qxF "version=1.0.0" "$API" \
    || fail "pkginfo.build_dep().version should be 1.0.0"

log "PASS: build_deps split (runtime activated, build is install-time only, env vars + Lua API both work)"
