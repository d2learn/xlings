#!/usr/bin/env bash
# E2E test: install linux-headers via workspace config,
# verify headers are copied into subos sysroot via pkginfo.install_dir.
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/linux_headers"
HOME_DIR="$(runtime_home_dir linux_headers_home)"

CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() { restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"; }
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

(cd "$SCENARIO_DIR" && run_xlings "$HOME_DIR" "$SCENARIO_DIR" install -y 2>&1) | tee /dev/stderr

[[ -f "$HOME_DIR/subos/default/usr/include/linux/errno.h" ]] \
  || fail "linux/errno.h not found in subos sysroot"

log "PASS: linux-headers install (pkginfo.install_dir)"
