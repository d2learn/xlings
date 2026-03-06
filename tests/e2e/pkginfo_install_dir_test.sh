#!/usr/bin/env bash
# E2E test: install linux-headers from real xim-pkgindex,
# verify pkginfo.install_dir() copies headers into subos sysroot.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/linux_headers"
HOME_DIR="$(runtime_home_dir linux_headers_home)"

CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() { restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"; }
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

# ── Update + Install ──
(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" update)

log "Installing linux-headers..."
INSTALL_OUT="$(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" install linux-headers -y 2>&1)" || true
echo "$INSTALL_OUT"

# ── Verify ──
[[ -f "$HOME_DIR/data/xpkgs/scode-x-linux-headers/5.11.1/include/linux/errno.h" ]] \
  || fail "scode-x-linux-headers not installed correctly"

[[ -f "$HOME_DIR/subos/default/usr/include/linux/errno.h" ]] \
  || fail "linux/errno.h not found in subos sysroot"

log "PASS: linux-headers install (pkginfo.install_dir)"
