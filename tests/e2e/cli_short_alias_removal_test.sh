#!/usr/bin/env bash
# E2E: short-command aliases (xim/xvm/xself/xsubos/xinstall) were
# removed in 0.4.8.
#
# Verifies:
#   1. self init creates ONLY the `xlings` shim — not xim/xvm/xself/...
#   2. self init also auto-cleans pre-existing legacy alias symlinks
#      (covers users upgrading from 0.4.7 or earlier).
#   3. Invoking xlings binary via a legacy alias name (argv[0] = xim
#      etc.) prints a clear migration error and exits 2.
#   4. self doctor detects leftover legacy alias shims as findings.
#   5. self doctor --fix removes leftover legacy alias shims.
#   6. A user's real (non-symlink) program named `xim` is NOT touched.

set -euo pipefail

# shellcheck source=./project_test_lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

RUNTIME_DIR="$ROOT_DIR/tests/e2e/runtime/cli_short_alias_removal"
HOME_DIR="$RUNTIME_DIR/home"

cleanup() { rm -rf "$RUNTIME_DIR"; }
trap cleanup EXIT
cleanup

XLINGS_BIN="$(find_xlings_bin)"

# Plain RUN — invoke as canonical `xlings`.
RUN() {
  ( cd /tmp && env -i HOME="$HOME" PATH=/usr/bin:/bin XLINGS_HOME="$HOME_DIR" "$XLINGS_BIN" "$@" )
}

# RUN_AS — invoke the binary via a temporary symlink whose basename is
# exactly `<alias_name>` so argv[0] basename matches what the multicall
# dispatcher inspects. Used to simulate a user typing `xim foo`.
RUN_AS() {
  local alias_name="$1"; shift
  local link_dir="$RUNTIME_DIR/.alias-link"
  local link="$link_dir/$alias_name"
  mkdir -p "$link_dir"
  ln -sf "$XLINGS_BIN" "$link"
  env -i HOME="$HOME" PATH=/usr/bin:/bin XLINGS_HOME="$HOME_DIR" "$link" "$@"
}

mkdir -p "$HOME_DIR/bin"
cp "$XLINGS_BIN" "$HOME_DIR/bin/xlings"

# ── S1: self init only creates `xlings` shim ───────────────────────
log "S1: self init produces only `xlings` shim under subos/default/bin"
RUN self init >/dev/null 2>&1 || fail "S1: self init failed"
shims=$(ls "$HOME_DIR/subos/default/bin" | sort | tr '\n' ' ')
[[ "$shims" == "xlings " ]] \
  || fail "S1: only `xlings` should be present in bin/, got: $shims"

# ── S2: pre-existing legacy alias symlinks are cleaned by self init ─
log "S2: legacy alias symlinks (from older xlings) are auto-cleaned"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xim"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xvm"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xself"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xsubos"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xinstall"
RUN self init >/dev/null 2>&1 || fail "S2: self init failed"
shims=$(ls "$HOME_DIR/subos/default/bin" | sort | tr '\n' ' ')
[[ "$shims" == "xlings " ]] \
  || fail "S2: legacy alias symlinks should be removed, but found: $shims"

# ── S3: invoking via legacy alias prints migration error + exits 2 ──
log "S3: invoking via legacy alias names → clear error + exit 2"
for alias in xim xvm xself xsubos xinstall; do
  rc=0
  out=$(RUN_AS "$alias" install foo 2>&1) || rc=$?
  [[ $rc -eq 2 ]] || fail "S3[$alias]: expected exit 2, got $rc"
  echo "$out" | grep -q "was removed in 0.4.8" \
    || fail "S3[$alias]: should mention 'was removed in 0.4.8'; got:\n$out"
  echo "$out" | grep -q "Use \`xlings " \
    || fail "S3[$alias]: should suggest 'xlings' replacement"
done

# ── S4: self doctor reports legacy alias shim ──────────────────────
log "S4: self doctor reports legacy alias as a finding"
ln -s "$HOME_DIR/bin/xlings" "$HOME_DIR/subos/default/bin/xvm"
out=$(RUN self doctor 2>&1) || true
echo "$out" | grep -q "legacy alias shim" \
  || fail "S4: doctor should report 'legacy alias shim'; got:\n$out"

# ── S5: self doctor --fix removes legacy alias shim ────────────────
log "S5: self doctor --fix cleans leftover legacy alias shim"
RUN self doctor --fix >/dev/null 2>&1 || true
[[ ! -e "$HOME_DIR/subos/default/bin/xvm" ]] \
  || fail "S5: legacy alias shim should be removed by --fix"

# ── S6: user's real (non-symlink) file named `xim` is preserved ────
log "S6: user's real file named `xim` (not a symlink) must NOT be touched"
echo '#!/bin/sh' > "$HOME_DIR/subos/default/bin/xim"
chmod +x "$HOME_DIR/subos/default/bin/xim"
RUN self init >/dev/null 2>&1
[[ -f "$HOME_DIR/subos/default/bin/xim" && ! -L "$HOME_DIR/subos/default/bin/xim" ]] \
  || fail "S6: user's real `xim` file was unexpectedly removed"

# Equally for doctor --fix
RUN self doctor --fix >/dev/null 2>&1
[[ -f "$HOME_DIR/subos/default/bin/xim" && ! -L "$HOME_DIR/subos/default/bin/xim" ]] \
  || fail "S6: user's real `xim` file removed by doctor --fix"

log "PASS: cli short-alias removal — scenarios 1-6"
