#!/usr/bin/env bash
# E2E test for the bugfix in docs/bugfixes/2026-04-25-remove-wipes-version-db.md
#
# Scenarios (the bug: `xlings remove <pkg>` wiped the entire version DB entry
# whenever the package's uninstall hook emitted a versionless `xvm.remove(name)`):
#
#   1. Remove active (no version)   → delete only active payload; survivors keep;
#                                     active auto-switches to highest remaining semver
#   2. Remove non-active (with ver) → delete only that version; active untouched
#   3. Remove last remaining        → pkg entry fully cleared from DB + workspace
#
# The fixture package `rm-fixture` is injected directly into the shared
# xim-pkgindex fixture so the test needs no additional downloads — the same
# fixture is shared with the rest of the e2e suite via prepare_fixture_index.sh.

set -euo pipefail

# shellcheck source=./project_test_lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

RUNTIME_DIR="$ROOT_DIR/tests/e2e/runtime/remove_multi_version"
HOME_DIR="$RUNTIME_DIR/home"
# Private copy of the shared fixture index, lets us inject our rm-fixture
# xpkg and neutralise the sub-index-repos declaration without touching the
# fixture that other e2e tests share.
LOCAL_INDEX_DIR="$RUNTIME_DIR/xim-pkgindex"

FIXTURE_PKG="$LOCAL_INDEX_DIR/pkgs/r/rm-fixture.lua"

cleanup() {
  rm -rf "$RUNTIME_DIR"
}
trap cleanup EXIT
cleanup  # start fresh

XLINGS_BIN="$(find_xlings_bin)"

RUN() {
  env -i HOME="$HOME" PATH=/usr/bin:/bin XLINGS_HOME="$HOME_DIR" "$XLINGS_BIN" "$@"
}

mkdir -p "$HOME_DIR"

# Clone the shared xim-pkgindex fixture into a private directory (fast: it's a
# local copy on disk) and neutralise xim-indexrepos.lua so `xlings install`
# does not try to fetch github-hosted sub-index repos during this test.
cp -r "$FIXTURE_INDEX_DIR" "$LOCAL_INDEX_DIR"
printf 'xim_indexrepos = {}\n' > "$LOCAL_INDEX_DIR/xim-indexrepos.lua"
rm -f "$LOCAL_INDEX_DIR/.xlings-index-cache.json"
mkdir -p "$(dirname "$FIXTURE_PKG")"

# ── Inject the fixture xpkg ──────────────────────────────────────
# It declares three versions with no URL (no downloads), writes a marker
# file in install_dir, and registers itself with xvm.add *without* a binding
# so the uninstall hook's `xvm.remove` is emitted without a version — the exact
# shape that used to trigger the original bug.
cat > "$FIXTURE_PKG" <<'LUA'
package = {
    spec = "1",
    name = "rm-fixture",
    description = "Local fixture for tests/e2e/remove_multi_version_test.sh",
    authors = {"xlings-ci"},
    licenses = {"MIT"},
    type = "package",
    archs = {"x86_64"},
    status = "stable",
    categories = {"test-fixture"},

    xpm = {
        linux   = { ["1.0.0"] = {}, ["2.0.0"] = {}, ["3.0.0"] = {} },
        macosx  = { ["1.0.0"] = {}, ["2.0.0"] = {}, ["3.0.0"] = {} },
        windows = { ["1.0.0"] = {}, ["2.0.0"] = {}, ["3.0.0"] = {} },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

function install()
    local dir = pkginfo.install_dir()
    os.tryrm(dir)
    os.mkdir(dir)
    io.writefile(path.join(dir, "VERSION"), pkginfo.version())
    return true
end

function config()
    xvm.add("rm-fixture", { bindir = pkginfo.install_dir() })
    return true
end

function uninstall()
    -- Deliberately versionless: this is the exact call shape that the bug
    -- required to trigger — the fix must tolerate it without wiping siblings.
    xvm.remove("rm-fixture")
    return true
end
LUA

# ── Seed XLINGS_HOME pointing at our private (neutralised) index ────────
mkdir -p "$HOME_DIR/subos/default/bin"
cp "$XLINGS_BIN" "$HOME_DIR/xlings"
cat > "$HOME_DIR/.xlings.json" <<EOF
{
  "mirror": "GLOBAL",
  "index_repos": [
    {
      "name": "xim",
      "url": "$LOCAL_INDEX_DIR"
    }
  ]
}
EOF

log "Initializing sandbox XLINGS_HOME at $HOME_DIR"
RUN self init >/dev/null 2>&1 || fail "self init failed"

# Belt-and-braces: also empty the on-disk sub-index-repos JSON so even if a
# stale cache lingered we would not fetch anything.
mkdir -p "$HOME_DIR/data/xim-index-repos"
printf '{}\n' > "$HOME_DIR/data/xim-index-repos/xim-indexrepos.json"

# Derive the canonical store dir: xpkgs/<repo>-x-<name>/<version>
# The fixture repo is registered as name "xim" by write_home_config, so the
# canonical store prefix is "xim-x-".
STORE_DIR="$HOME_DIR/data/xpkgs/xim-x-rm-fixture"

# ── install all three versions ───────────────────────────────────
# `install` is no longer an implicit version-switch: once a version is active
# in the current sub-OS, subsequent `install <name>@<other>` adds the payload
# but preserves the active pointer. Explicitly `use` the highest after the
# install loop so the rest of the test reflects "active was 3.0.0".
for v in 1.0.0 2.0.0 3.0.0; do
  log "install rm-fixture@$v"
  RUN install "rm-fixture@$v" -y >/dev/null 2>&1 \
    || fail "install rm-fixture@$v failed"
  [[ -f "$STORE_DIR/$v/VERSION" ]] \
    || fail "install marker missing: $STORE_DIR/$v/VERSION"
done
RUN use rm-fixture 3.0.0 >/dev/null 2>&1 \
  || fail "use rm-fixture 3.0.0 failed"

python3 - "$HOME_DIR" 1.0.0 2.0.0 3.0.0 <<'PY' || fail "post-install DB state wrong"
import json, sys, pathlib
home, *want = sys.argv[1:]
data = json.loads(pathlib.Path(home, ".xlings.json").read_text())
got = sorted((data.get("versions") or {}).get("rm-fixture", {}).get("versions", {}))
assert got == sorted(want), f"expected versions {sorted(want)} in DB, got {got}"
PY

# ── Scenario 1: remove without version — only active 3.0.0 gone, active → 2.0.0 ──
log "Scenario 1: remove rm-fixture (no version; active was 3.0.0)"
RUN remove rm-fixture -y >/dev/null 2>&1 || fail "remove rm-fixture failed"

[[ ! -d "$STORE_DIR/3.0.0" ]] || fail "S1: 3.0.0 payload dir should be deleted"
[[ -f "$STORE_DIR/1.0.0/VERSION" ]] \
  || fail "S1: 1.0.0 payload dir must be preserved (bug would wipe it from DB)"
[[ -f "$STORE_DIR/2.0.0/VERSION" ]] \
  || fail "S1: 2.0.0 payload dir must be preserved"

python3 - "$HOME_DIR" <<'PY' || fail "S1 DB state wrong"
import json, sys, pathlib
home = sys.argv[1]
data = json.loads(pathlib.Path(home, ".xlings.json").read_text())
vers = sorted((data.get("versions") or {}).get("rm-fixture", {}).get("versions", {}))
assert vers == ["1.0.0", "2.0.0"], \
    f"S1: DB should have [1.0.0, 2.0.0] after removing active 3.0.0; got {vers}"

ws = json.loads(pathlib.Path(home, "subos/default/.xlings.json").read_text())
active = (ws.get("workspace") or {}).get("rm-fixture")
assert active == "2.0.0", \
    f"S1: active should auto-switch to highest remaining (2.0.0); got {active!r}"
PY

# ── Scenario 2: remove specific non-active 1.0.0 → active still 2.0.0 ──────
log "Scenario 2: remove rm-fixture@1.0.0 (non-active)"
RUN remove rm-fixture@1.0.0 -y >/dev/null 2>&1 || fail "remove rm-fixture@1.0.0 failed"

[[ ! -d "$STORE_DIR/1.0.0" ]] || fail "S2: 1.0.0 payload dir should be deleted"
[[ -f "$STORE_DIR/2.0.0/VERSION" ]] \
  || fail "S2: 2.0.0 payload dir must be preserved"

python3 - "$HOME_DIR" <<'PY' || fail "S2 DB state wrong"
import json, sys, pathlib
home = sys.argv[1]
data = json.loads(pathlib.Path(home, ".xlings.json").read_text())
vers = sorted((data.get("versions") or {}).get("rm-fixture", {}).get("versions", {}))
assert vers == ["2.0.0"], f"S2: DB should only contain [2.0.0]; got {vers}"

ws = json.loads(pathlib.Path(home, "subos/default/.xlings.json").read_text())
active = (ws.get("workspace") or {}).get("rm-fixture")
assert active == "2.0.0", \
    f"S2: active must remain 2.0.0 after removing non-active 1.0.0; got {active!r}"
PY

# ── Scenario 3: remove the last remaining version → package entry fully gone ──
log "Scenario 3: remove rm-fixture (last remaining version)"
RUN remove rm-fixture -y >/dev/null 2>&1 || fail "remove rm-fixture (last) failed"

[[ ! -d "$STORE_DIR/2.0.0" ]] || fail "S3: 2.0.0 payload dir should be deleted"

python3 - "$HOME_DIR" <<'PY' || fail "S3 DB state wrong"
import json, sys, pathlib
home = sys.argv[1]
data = json.loads(pathlib.Path(home, ".xlings.json").read_text())
assert "rm-fixture" not in (data.get("versions") or {}), \
    "S3: rm-fixture should be completely removed from versions DB"

ws = json.loads(pathlib.Path(home, "subos/default/.xlings.json").read_text())
assert "rm-fixture" not in (ws.get("workspace") or {}), \
    "S3: rm-fixture should be cleared from workspace"
PY

log "PASS: remove-multi-version scenarios 1, 2, 3"
