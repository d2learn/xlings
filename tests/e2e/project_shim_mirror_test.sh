#!/usr/bin/env bash
# E2E test: verify that project-context `use` mirrors shims to global subos bin
# so that tools are reachable via PATH (~/.xlings/subos/current/bin).
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/local_repo"
HOME_DIR="$(runtime_home_dir shim_mirror_home)"
CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
}
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

# --- Pre-check: global subos bin should NOT have a node shim ---
GLOBAL_BIN="$HOME_DIR/subos/default/bin"
[[ ! -e "$GLOBAL_BIN/node" ]] || fail "global node shim already exists before test"

# --- Install & use node in project context ---
(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" update
)
(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" -y install projectrepo:node@22.17.1
)
(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" use node 22.17.1 >/dev/null
)

# --- Verify: project subos bin has the shim ---
PROJECT_BIN="$SCENARIO_DIR/.xlings/subos/_/bin"
[[ -e "$PROJECT_BIN/node" ]] || fail "project node shim missing after use"

# --- Verify: global subos bin now also has the shim ---
[[ -e "$GLOBAL_BIN/node" ]] || fail "global node shim was NOT mirrored after project use"
[[ -L "$GLOBAL_BIN/node" ]] || fail "global node shim is not a symlink"

# --- Verify: global shim resolves to xlings binary ---
XLINGS_BIN_PATH="$(find_xlings_bin)"
RESOLVED="$(realpath "$GLOBAL_BIN/node")"
EXPECTED="$(realpath "$HOME_DIR/xlings")"
[[ "$RESOLVED" = "$EXPECTED" ]] || \
  fail "global node shim resolves to '$RESOLVED', expected '$EXPECTED'"

# --- Verify: pre-existing global shim is not overwritten ---
# Create a marker shim, then run use again — it should remain untouched.
echo "marker" > "$GLOBAL_BIN/node"
(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" use node 22.17.1 >/dev/null
)
MARKER="$(cat "$GLOBAL_BIN/node")"
[[ "$MARKER" = "marker" ]] || fail "global node shim was overwritten (should preserve existing)"

# --- Verify: non-project use does NOT attempt global mirror ---
# (This is implicitly covered: write_home_config doesn't set project config,
#  and outside a project dir, has_project_config() is false.)

log "PASS: project shim mirror to global subos bin"
