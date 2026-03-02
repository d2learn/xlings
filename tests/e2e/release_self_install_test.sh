#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/release_test_lib.sh"

ARCHIVE_PATH="${1:-$ROOT_DIR/build/release.tar.gz}"
require_release_archive "$ARCHIVE_PATH"

PKG_DIR="$(extract_release_archive "$ARCHIVE_PATH" release_self_install)"
INSTALL_USER_DIR="$RUNTIME_ROOT/release_self_install_user"
rm -rf "$INSTALL_USER_DIR"
mkdir -p "$INSTALL_USER_DIR"

[[ -f "$PKG_DIR/.xlings.json" ]] || fail "bootstrap config missing in release package"
[[ -x "$PKG_DIR/bin/xlings" ]] || fail "bootstrap binary missing in release package"

HOME="$INSTALL_USER_DIR" \
PATH="$(minimal_system_path)" \
env -u XLINGS_HOME "$PKG_DIR/bin/xlings" self install

INSTALLED_HOME="$INSTALL_USER_DIR/.xlings"
[[ -x "$INSTALLED_HOME/bin/xlings" ]] || fail "installed home missing bin/xlings"
[[ -L "$INSTALLED_HOME/subos/current" ]] || fail "installed home missing subos/current link"
[[ -f "$INSTALLED_HOME/config/shell/xlings-profile.sh" ]] || fail "installed home missing shell profile"

for shim in xlings xim xsubos xself; do
  [[ -x "$INSTALLED_HOME/subos/default/bin/$shim" ]] || fail "shim $shim missing after self install"
done

INSTALLED_PATH="$INSTALLED_HOME/subos/current/bin:$INSTALLED_HOME/bin:$(minimal_system_path)"
HOME="$INSTALL_USER_DIR" PATH="$INSTALLED_PATH" env -u XLINGS_HOME "$INSTALLED_HOME/bin/xlings" -h >/dev/null
CONFIG_OUT="$(
  HOME="$INSTALL_USER_DIR" PATH="$INSTALLED_PATH" env -u XLINGS_HOME \
    "$INSTALLED_HOME/bin/xlings" config
)"
echo "$CONFIG_OUT" | grep -q "XLINGS_HOME:     $INSTALLED_HOME" || \
  fail "installed home config output mismatch"

log "PASS: release self install scenario"
