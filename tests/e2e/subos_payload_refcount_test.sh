#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_test_lib.sh"

require_fixture_index

HOME_DIR="$(runtime_home_dir subos_refcount_home)"
rm -rf "$HOME_DIR"
write_home_config "$HOME_DIR" "GLOBAL"

run_xlings "$HOME_DIR" "$ROOT_DIR" self init >/dev/null
run_xlings "$HOME_DIR" "$ROOT_DIR" update >/dev/null

run_xlings "$HOME_DIR" "$ROOT_DIR" subos new s1 >/dev/null
run_xlings "$HOME_DIR" "$ROOT_DIR" subos new s2 >/dev/null

for subos_name in s1 s2; do
  SUBOS_DIR="$HOME_DIR/subos/$subos_name"
  [[ -d "$SUBOS_DIR/bin" ]] || fail "$subos_name bin dir missing after subos new"
  [[ -d "$SUBOS_DIR/lib" ]] || fail "$subos_name lib dir missing after subos new"
  [[ -d "$SUBOS_DIR/usr" ]] || fail "$subos_name usr dir missing after subos new"
  [[ -d "$SUBOS_DIR/generations" ]] || fail "$subos_name generations dir missing after subos new"
  [[ -f "$SUBOS_DIR/.xlings.json" ]] || fail "$subos_name .xlings.json missing after subos new"
  grep -q '"workspace"[[:space:]]*:[[:space:]]*{' "$SUBOS_DIR/.xlings.json" || \
    fail "$subos_name .xlings.json missing workspace object"
done

run_xlings "$HOME_DIR" "$ROOT_DIR" subos use s1 >/dev/null
INSTALL_S1="$(
  run_xlings "$HOME_DIR" "$ROOT_DIR" install node@22.17.1 -y 2>&1
)"
echo "$INSTALL_S1"
assert_contains "$INSTALL_S1" "version: 22.17.1" \
  "s1 install did not print resolved version"
assert_contains "$INSTALL_S1" "node@22.17.1 installed" \
  "s1 install did not confirm node installation"

PAYLOAD_DIR="$HOME_DIR/data/xpkgs/official-x-node/22.17.1"
DOWNLOAD_FILE="$HOME_DIR/data/runtimedir/downloads/official-x-node/22.17.1/$(node_archive_name 22.17.1)"
[[ -x "$PAYLOAD_DIR/bin/node" ]] || fail "node payload missing after s1 install"
[[ -f "$DOWNLOAD_FILE" ]] || fail "download cache missing after s1 install"
[[ -x "$HOME_DIR/subos/s1/bin/node" ]] || fail "s1 shim missing after install"

run_xlings "$HOME_DIR" "$ROOT_DIR" subos use s2 >/dev/null
INSTALL_S2="$(
  run_xlings "$HOME_DIR" "$ROOT_DIR" install node@22.17.1 -y 2>&1
)"
echo "$INSTALL_S2"
assert_contains "$INSTALL_S2" "all packages already installed" \
  "s2 install should reuse existing payload"
assert_contains "$INSTALL_S2" "node@22.17.1 already installed" \
  "s2 install did not confirm attach of reused payload"
[[ -x "$HOME_DIR/subos/s2/bin/node" ]] || fail "s2 shim missing after reused install"

NODE_VER_S2="$(
  env XLINGS_HOME="$HOME_DIR" "$HOME_DIR/subos/s2/bin/node" --version
)"
[[ "$NODE_VER_S2" == "v22.17.1" ]] || fail "s2 node shim did not resolve to v22.17.1"

run_xlings "$HOME_DIR" "$ROOT_DIR" subos use s1 >/dev/null
REMOVE_S1="$(
  run_xlings "$HOME_DIR" "$ROOT_DIR" remove node 2>&1
)"
echo "$REMOVE_S1"
[[ -x "$PAYLOAD_DIR/bin/node" ]] || fail "payload removed even though s2 still referenced it"
[[ ! -e "$HOME_DIR/subos/s1/bin/node" ]] || fail "s1 shim still present after detach"

run_xlings "$HOME_DIR" "$ROOT_DIR" subos use s2 >/dev/null
REMOVE_S2="$(
  run_xlings "$HOME_DIR" "$ROOT_DIR" remove node 2>&1
)"
echo "$REMOVE_S2"
[[ ! -e "$PAYLOAD_DIR" ]] || fail "payload still present after last subos reference was removed"
[[ ! -e "$HOME_DIR/subos/s2/bin/node" ]] || fail "s2 shim still present after final remove"

log "PASS: subos payload refcount scenario"
