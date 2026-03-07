#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/project_home"
HOME_DIR="$(runtime_home_dir project_home_home)"
CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
}
trap cleanup EXIT
write_home_config "$HOME_DIR" "GLOBAL"

CONFIG_OUT="$({
  cd "$SCENARIO_DIR"
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" config
})"
echo "$CONFIG_OUT"
assert_contains "$CONFIG_OUT" "XLINGS_DATA_PROJECT: $SCENARIO_DIR/.xlings/data" \
  "project home scenario did not expose project-local data dir"
assert_contains "$CONFIG_OUT" "XLINGS_SUBOS:    $SCENARIO_DIR/.xlings/subos/_" \
  "project home scenario did not resolve anonymous project subos"

INSTALL_OUT="$({
  cd "$SCENARIO_DIR"
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" -y install
})"
echo "$INSTALL_OUT"
assert_contains "$INSTALL_OUT" "packages to install (1):" \
  "project home manifest install did not plan one package"
assert_contains "$INSTALL_OUT" "projectrepo:node @22.17.1" \
  "project home manifest install did not resolve node from project workspace"

PROJECT_STATE="$SCENARIO_DIR/.xlings/.xlings.json"
[[ -f "$PROJECT_STATE" ]] || fail "project home runtime state file missing"
grep -q '"workspace"' "$PROJECT_STATE" \
  || fail "project home runtime state missing workspace"
grep -q '"versions"' "$PROJECT_STATE" \
  || fail "project home runtime state missing versions"
grep -q '22.17.1' "$PROJECT_STATE" \
  || fail "project home runtime state missing installed node version"
if grep -q '"index_repos"' "$PROJECT_STATE"; then
  fail "project home runtime state should not duplicate declarative index_repos"
fi

cmp -s "$CONFIG_BACKUP" "$SCENARIO_DIR/.xlings.json" \
  || fail "project manifest .xlings.json should remain unchanged"
if grep -q '"versions"' "$SCENARIO_DIR/.xlings.json"; then
  fail "project manifest .xlings.json should remain declarative and not gain versions"
fi

[[ -x "$SCENARIO_DIR/.xlings/subos/_/bin/node" ]] \
  || fail "project home anonymous subos node shim missing"
NODE_VER="$({
  cd "$SCENARIO_DIR"
  env XLINGS_HOME="$HOME_DIR" "$SCENARIO_DIR/.xlings/subos/_/bin/node" --version
})"
[[ "$NODE_VER" == "v22.17.1" ]] || fail "project home node shim did not resolve expected version"

log "PASS: project_home scenario"
