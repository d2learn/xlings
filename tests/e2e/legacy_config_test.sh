#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/legacy_config"
HOME_DIR="$(runtime_home_dir legacy_config_home)"

# Clean up any previous run
rm -rf "$HOME_DIR" "$SCENARIO_DIR/.xlings" "$SCENARIO_DIR/.xlings.json"

cleanup() {
  rm -rf "$HOME_DIR" "$SCENARIO_DIR/.xlings" "$SCENARIO_DIR/.xlings.json"
}
trap cleanup EXIT

write_home_config "$HOME_DIR" "GLOBAL"

# Verify no .xlings.json exists before test
[[ ! -f "$SCENARIO_DIR/.xlings.json" ]] \
  || fail ".xlings.json should not exist before legacy config test"

# Verify config.xlings exists
[[ -f "$SCENARIO_DIR/config.xlings" ]] \
  || fail "config.xlings fixture missing"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" update
)

# Run xlings install (no arguments) — should detect config.xlings
INSTALL_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" install 2>&1
)"
echo "$INSTALL_OUT"

# Verify .xlings.json was generated from legacy config
[[ -f "$SCENARIO_DIR/.xlings.json" ]] \
  || fail ".xlings.json was not generated from config.xlings"

# Verify generated .xlings.json contains workspace with node
grep -q '"node"' "$SCENARIO_DIR/.xlings.json" \
  || fail "generated .xlings.json should contain node in workspace"
grep -q '"22.17.1"' "$SCENARIO_DIR/.xlings.json" \
  || fail "generated .xlings.json should contain version 22.17.1"

# Verify node was actually installed
NODE_ARCHIVE="$(node_archive_name 22.17.1)"
[[ -f "$HOME_DIR/data/runtimedir/$NODE_ARCHIVE" ]] \
  || fail "node download cache missing after legacy config install"

log "PASS: legacy_config scenario"
