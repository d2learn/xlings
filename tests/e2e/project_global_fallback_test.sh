#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/global_fallback"
HOME_DIR="$(runtime_home_dir global_fallback_home)"
CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
}
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

CONFIG_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" config
)"
echo "$CONFIG_OUT"
if echo "$CONFIG_OUT" | grep -F "XLINGS_DATA_PROJECT:" >/dev/null; then
  fail "global fallback scenario unexpectedly exposed project-local data dir"
fi

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" update
)

INSTALL_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" -y install
)"
echo "$INSTALL_OUT"

NODE_ARCHIVE="$(node_archive_name 22.17.1)"
[[ -f "$HOME_DIR/data/xpkgs/official-x-node/22.17.1/bin/node" ]] \
  || fail "global fallback install did not land in isolated global xpkgs"
[[ -f "$HOME_DIR/data/runtimedir/downloads/official-x-node/22.17.1/$NODE_ARCHIVE" ]] \
  || fail "global fallback download cache missing from isolated global home"

if [[ -d "$SCENARIO_DIR/.xlings/data" ]]; then
  fail "global fallback scenario should not create project-local data dir"
fi

INFO_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" info official:node
)"
echo "$INFO_OUT"
assert_contains "$INFO_OUT" "installed:   yes" \
  "global fallback node should be reported as installed"

log "PASS: global_fallback scenario"
