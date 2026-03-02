#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

SCENARIO_NAME="${SCENARIO_NAME:-xlings_res_cn}"
EXPECTED_RES_SERVER="${EXPECTED_RES_SERVER:-https://gitcode.com/xlings-res}"
HOME_NAME="${HOME_NAME:-${SCENARIO_NAME}_home}"
SCENARIO_DIR="$ROOT_DIR/tests/e2e/scenarios/$SCENARIO_NAME"
HOME_DIR="$(runtime_home_dir "$HOME_NAME")"
CONFIG_BACKUP="$(prepare_scenario "$SCENARIO_DIR" "$HOME_DIR")"
cleanup() {
  restore_scenario "$SCENARIO_DIR" "$HOME_DIR" "$CONFIG_BACKUP"
}
trap cleanup EXIT
write_home_config "$HOME_DIR" "CN"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" update
)

INSTALL_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" -y install projectrepo:mdbook@0.4.43
)"
echo "$INSTALL_OUT"
assert_contains "$INSTALL_OUT" "$EXPECTED_RES_SERVER/mdbook/releases/download/0.4.43" \
  "XLINGS_RES mirror rewrite did not use the expected resource server"

MDBOOK_ARCHIVE="$(mdbook_archive_name 0.4.43)"
[[ -f "$SCENARIO_DIR/.xlings/data/runtimedir/downloads/projectrepo-x-mdbook/0.4.43/$MDBOOK_ARCHIVE" ]] \
  || fail "mdbook XLINGS_RES archive missing from project-local runtimedir"

if [[ "$(platform_name)" == "macosx" ]]; then
  [[ -x "$SCENARIO_DIR/.xlings/data/xpkgs/projectrepo-x-mdbook/0.4.43/mdbook" ]] \
    || fail "mdbook payload missing from project-local xpkgs"
else
  [[ -x "$SCENARIO_DIR/.xlings/data/xpkgs/projectrepo-x-mdbook/0.4.43/mdbook" ]] \
    || fail "mdbook payload missing from project-local xpkgs"
fi

INFO_OUT="$(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" info projectrepo:mdbook
)"
echo "$INFO_OUT"
assert_contains "$INFO_OUT" "installed:   yes" \
  "project-local mdbook should be reported as installed"

(
  cd "$SCENARIO_DIR" &&
  run_xlings "$HOME_DIR" "$SCENARIO_DIR" use mdbook 0.4.43 >/dev/null
)
MDBOOK_VER="$(
  cd "$SCENARIO_DIR" &&
  env XLINGS_HOME="$HOME_DIR" "$HOME_DIR/subos/default/bin/mdbook" --version
)"
assert_contains "$MDBOOK_VER" "mdbook v0.4.43" \
  "mdbook shim did not execute the installed XLINGS_RES payload"

log "PASS: $SCENARIO_NAME scenario"
