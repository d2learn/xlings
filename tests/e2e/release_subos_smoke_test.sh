#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/release_test_lib.sh"

ARCHIVE_PATH="${1:-$ROOT_DIR/build/release.tar.gz}"
require_release_archive "$ARCHIVE_PATH"
require_fixture_index

PKG_DIR="$(extract_release_archive "$ARCHIVE_PATH" release_subos_smoke)"
write_fixture_release_config "$PKG_DIR"

export XLINGS_HOME="$PKG_DIR"
export PATH="$XLINGS_HOME/bin:$(minimal_system_path)"

xlings -h >/dev/null
xlings config >/dev/null
xlings --version >/dev/null

xlings self init
export PATH="$XLINGS_HOME/subos/current/bin:$XLINGS_HOME/bin:$(minimal_system_path)"
xlings update
xlings subos list >/dev/null

D2X_VERSION="${D2X_VERSION:-$(default_d2x_version)}"

xlings subos new s1
[[ -f "$XLINGS_HOME/subos/s1/.xlings.json" ]] || fail "s1 config missing"
xlings subos use s1
readlink "$XLINGS_HOME/subos/current" | grep -q "s1" || fail "subos/current not pointing to s1"

INSTALL_S1="$(xlings install "d2x@$D2X_VERSION" -y 2>&1)"
echo "$INSTALL_S1"
echo "$INSTALL_S1" | grep -Eq "d2x@$D2X_VERSION (installed|already installed)" || \
  fail "s1 install did not confirm d2x attach/install"
xlings use d2x "$D2X_VERSION" >/dev/null
[[ -x "$XLINGS_HOME/subos/s1/bin/d2x" ]] || fail "s1 d2x shim missing"
INFO_S1="$(xlings info d2x 2>&1)"
echo "$INFO_S1" | grep -q "installed:   yes" || fail "s1 d2x info did not report installed"

xlings subos new s2
[[ -f "$XLINGS_HOME/subos/s2/.xlings.json" ]] || fail "s2 config missing"
xlings subos use s2
readlink "$XLINGS_HOME/subos/current" | grep -q "s2" || fail "subos/current not pointing to s2"

INSTALL_S2="$(xlings install "d2x@$D2X_VERSION" -y 2>&1)"
echo "$INSTALL_S2"
echo "$INSTALL_S2" | grep -Eq "d2x@$D2X_VERSION (installed|already installed)" || \
  fail "s2 install did not confirm d2x attach/install"
xlings use d2x "$D2X_VERSION" >/dev/null
[[ -x "$XLINGS_HOME/subos/s2/bin/d2x" ]] || fail "s2 d2x shim missing"
INFO_S2="$(xlings info d2x 2>&1)"
echo "$INFO_S2" | grep -q "installed:   yes" || fail "s2 d2x info did not report installed"

xlings subos use s1
readlink "$XLINGS_HOME/subos/current" | grep -q "s1" || fail "failed to switch back to s1"
xlings subos use s2
readlink "$XLINGS_HOME/subos/current" | grep -q "s2" || fail "failed to switch back to s2"
xlings subos list >/dev/null

env XLINGS_HOME="$XLINGS_HOME" "$XLINGS_HOME/subos/s2/bin/d2x" --version >/dev/null

D2X_VERSION="$D2X_VERSION" XLINGS_HOME="$XLINGS_HOME" \
  bash "$ROOT_DIR/tests/e2e/rpath_verify_test.sh"

xlings subos use default
xlings subos remove s1
xlings subos remove s2

log "PASS: release subos smoke scenario"
