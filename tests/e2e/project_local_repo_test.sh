#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/local_repo"
HOME_DIR="$(runtime_home_dir local_repo_home)"
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
assert_contains "$CONFIG_OUT" "XLINGS_DATA_PROJECT: $SCENARIO_DIR/.xlings/data" \
  "local-repo scenario did not expose project-local data dir"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" update
)

[[ -L "$SCENARIO_DIR/.xlings/data/projectrepo" || -d "$SCENARIO_DIR/.xlings/data/projectrepo/pkgs" ]] \
  || fail "projectrepo index was not materialized in project-local data dir"

INSTALL_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" -y install projectrepo:node@22.17.1 projectrepo:node@20.19.0
)"
echo "$INSTALL_OUT"
assert_contains "$INSTALL_OUT" "packages to install (2):" \
  "single-command multi-version install plan did not contain both node versions"

NODE_ARCHIVE_22="$(node_archive_name 22.17.1)"
NODE_ARCHIVE_20="$(node_archive_name 20.19.0)"
[[ -f "$SCENARIO_DIR/.xlings/data/runtimedir/downloads/projectrepo-x-node/22.17.1/$NODE_ARCHIVE_22" ]] \
  || fail "node 22.17.1 download cache missing from project-local runtimedir"
[[ -f "$SCENARIO_DIR/.xlings/data/runtimedir/downloads/projectrepo-x-node/20.19.0/$NODE_ARCHIVE_20" ]] \
  || fail "node 20.19.0 download cache missing from project-local runtimedir"

[[ -x "$SCENARIO_DIR/.xlings/data/xpkgs/projectrepo-x-node/22.17.1/bin/node" ]] \
  || fail "node 22.17.1 payload missing from project-local xpkgs"
[[ -x "$SCENARIO_DIR/.xlings/data/xpkgs/projectrepo-x-node/20.19.0/bin/node" ]] \
  || fail "node 20.19.0 payload missing from project-local xpkgs"

INFO_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" info projectrepo:node
)"
echo "$INFO_OUT"
assert_contains "$INFO_OUT" "installed:   yes" \
  "project-local node should be reported as installed"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" use node 20.19.0 >/dev/null
)
NODE_VER_20="$(
  cd "$SCENARIO_DIR" &&
  env XLINGS_HOME="$HOME_DIR" "$HOME_DIR/subos/default/bin/node" --version
)"
[[ "$NODE_VER_20" == "v20.19.0" ]] || fail "node shim did not switch to 20.19.0"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" use node 22.17.1 >/dev/null
)
NODE_VER_22="$(
  cd "$SCENARIO_DIR" &&
  env XLINGS_HOME="$HOME_DIR" "$HOME_DIR/subos/default/bin/node" --version
)"
[[ "$NODE_VER_22" == "v22.17.1" ]] || fail "node shim did not switch back to 22.17.1"

log "PASS: local_repo scenario"
