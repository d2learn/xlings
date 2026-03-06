#!/usr/bin/env bash
# E2E test: install linux-headers via workspace config,
# verify headers are copied into anonymous subos sysroot.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/linux_headers"
HOME_DIR="$(runtime_home_dir linux_headers_home)"

CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
}
trap cleanup EXIT

# ── 1. Set up home and sync index (scode sub-index discovered automatically) ──
write_home_config "$HOME_DIR" "GLOBAL"

(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" install -y)

# ── 2. Verify anonymous subos sysroot has the headers ──
ANON_SUBOS="$SCENARIO_DIR/.xlings/subos/_"
[[ -d "$ANON_SUBOS" ]] \
  || fail "anonymous subos dir not created at $ANON_SUBOS"

[[ -f "$ANON_SUBOS/usr/include/linux/errno.h" ]] \
  || fail "linux/errno.h not found in anonymous subos sysroot"

log "PASS: linux-headers install (pkginfo.install_dir + anonymous subos)"
