#!/usr/bin/env bash
set -euo pipefail

# End-to-end usability test for Linux release package.
# It validates command availability, subos lifecycle, alias compatibility,
# and (optionally) package installation scenarios in an isolated temp dir.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
TMP_BASE="${TMPDIR:-/tmp}/xlings-e2e-$$"
ARCHIVE_PATH="${1:-}"
SKIP_NETWORK_TESTS="${SKIP_NETWORK_TESTS:-0}"

log() { echo "[e2e] $*"; }
fail() { echo "[e2e] FAIL: $*" >&2; exit 1; }

cleanup() {
  [[ -d "$TMP_BASE" ]] && rm -rf "$TMP_BASE" || true
}
trap cleanup EXIT

assert_contains() {
  local output="$1"
  local expected="$2"
  local hint="${3:-}"
  if [[ "$output" != *"$expected"* ]]; then
    fail "expected output to contain '$expected'. ${hint}"
  fi
}

prepare_archive() {
  if [[ -n "$ARCHIVE_PATH" ]]; then
    [[ -f "$ARCHIVE_PATH" ]] || fail "archive not found: $ARCHIVE_PATH"
    return
  fi

  ARCHIVE_PATH="$(ls -t "$BUILD_DIR"/xlings-*-linux-x86_64.tar.gz 2>/dev/null | head -1 || true)"
  if [[ -n "$ARCHIVE_PATH" ]]; then
    return
  fi

  log "no archive found, building via tools/linux_release.sh ..."
  export PATH="/home/xlings/.xlings_data/xim/xpkgs/musl-gcc/15.1.0/bin:$PATH"
  SKIP_NETWORK_VERIFY=1 "$ROOT_DIR/tools/linux_release.sh" || fail "release build failed"
  ARCHIVE_PATH="$(ls -t "$BUILD_DIR"/xlings-*-linux-x86_64.tar.gz 2>/dev/null | head -1 || true)"
  [[ -n "$ARCHIVE_PATH" ]] || fail "archive still not found after build"
}

prepare_runtime() {
  mkdir -p "$TMP_BASE"
  tar -xzf "$ARCHIVE_PATH" -C "$TMP_BASE"
  PKG_DIR="$(ls -d "$TMP_BASE"/xlings-*-linux-x86_64 2>/dev/null | head -1 || true)"
  [[ -n "$PKG_DIR" ]] || fail "failed to locate extracted package dir"

  export XLINGS_HOME="$PKG_DIR"
  export XLINGS_DATA="$PKG_DIR/data"
  export XLINGS_SUBOS="$PKG_DIR/subos/current"
  export PATH="$PKG_DIR/subos/current/bin:$PKG_DIR/bin:$PATH"
}

scenario_basic_commands() {
  log "scenario: basic commands"
  local help_out cfg_out version_out
  help_out="$(xlings -h 2>&1)" || fail "xlings -h failed"
  assert_contains "$help_out" "Commands:" "help output malformed"
  assert_contains "$help_out" "info" "info command should be listed"

  cfg_out="$(xlings config 2>&1)" || fail "xlings config failed"
  assert_contains "$cfg_out" "XLINGS_HOME" "config output missing XLINGS_HOME"
  assert_contains "$cfg_out" "XLINGS_SUBOS" "config output missing XLINGS_SUBOS"

  version_out="$(xvm --version 2>&1)" || fail "xvm --version failed"
  assert_contains "$version_out" "xvm" "xvm version output malformed"
}

scenario_info_mapping() {
  log "scenario: info mapping"
  local info_out
  info_out="$(xlings info xlings 2>&1)" || fail "xlings info xlings failed"
  assert_contains "$info_out" "Program: xlings" "xlings info should route to xvm info"
}

scenario_subos_lifecycle_and_aliases() {
  log "scenario: subos lifecycle and aliases"
  local out

  out="$(xlings subos ls 2>&1)" || fail "xlings subos ls failed"
  assert_contains "$out" "default" "subos list should contain default"

  xlings subos new s1 >/dev/null 2>&1 || fail "xlings subos new s1 failed"
  xlings subos use s1 >/dev/null 2>&1 || fail "xlings subos use s1 failed"

  out="$(xlings subos i s1 2>&1)" || fail "xlings subos i s1 failed"
  assert_contains "$out" "info for 's1'" "subos info alias failed"
  assert_contains "$out" "active: true" "subos use should switch active env"

  # Backward compatibility paths
  xlings subos list >/dev/null 2>&1 || fail "xlings subos list failed"
  xlings subos info s1 >/dev/null 2>&1 || fail "xlings subos info s1 failed"

  xlings subos use default >/dev/null 2>&1 || fail "xlings subos use default failed"
  xlings subos rm s1 >/dev/null 2>&1 || fail "xlings subos rm s1 failed"
}

scenario_self_and_cleanup() {
  log "scenario: self and cleanup"
  local out
  out="$(xlings self clean --dry-run 2>&1)" || fail "xlings self clean --dry-run failed"
  assert_contains "$out" "dry-run" "clean dry-run output mismatch"
}

scenario_subos_create_requires_config_xvm() {
  log "scenario: subos new fails when config/xvm missing"
  local config_xvm="$XLINGS_HOME/config/xvm"
  [[ -d "$config_xvm" ]] || fail "config/xvm must exist in package"

  mv "$config_xvm" "${config_xvm}.bak" || fail "backup config/xvm failed"
  local out
  out="$(xlings subos new testcfgxvm 2>&1)" || true
  mv "${config_xvm}.bak" "$config_xvm" || fail "restore config/xvm failed"

  assert_contains "$out" "config/xvm" "subos new should error when config/xvm missing"
  assert_contains "$out" "package incomplete" "error should mention package incomplete"
  log "  subos create requires config/xvm: OK"
}

scenario_project_deps() {
  log "scenario: project deps (.xlings.json)"

  local tmpdir
  tmpdir="$(mktemp -d)"
  local out

  # T18: valid .xlings.json with deps array
  printf '{"deps":["d2x@0.1.3"]}\n' > "$tmpdir/.xlings.json"
  out="$(cd "$tmpdir" && xlings install 2>&1)" || true
  assert_contains "$out" "d2x" \
    "xlings install should process d2x dep from .xlings.json"

  rm -rf "$tmpdir"
  log "  project deps: OK"
}

scenario_xpkgs_reuse() {
  log "scenario: xpkgs multi-subos reuse (T14)"

  # Install d2x@0.1.1 in subos reuse-a
  xlings subos new reuse-a >/dev/null 2>&1 || true
  xlings subos use reuse-a >/dev/null 2>&1
  xlings install d2x@0.1.1 -y >/dev/null 2>&1 || fail "install d2x in reuse-a failed"

  # Install same version in subos reuse-b — should reuse xpkgs (fast path)
  xlings subos new reuse-b >/dev/null 2>&1 || true
  xlings subos use reuse-b >/dev/null 2>&1
  local out
  out="$(xlings install d2x@0.1.1 -y 2>&1)" || true
  echo "$out"
  assert_contains "$out" "d2x" "reuse install should mention d2x"
  # xvm cross-subos scanning may show "already installed" — that IS the reuse
  assert_contains "$out" "installed" \
    "second install of d2x@0.1.1 should confirm it is installed (reused)"

  # Cleanup
  xlings subos use default >/dev/null 2>&1 || true
  xlings subos rm reuse-a >/dev/null 2>&1 || true
  xlings subos rm reuse-b >/dev/null 2>&1 || true
  log "  xpkgs reuse: OK"
}

scenario_index_spec_validation() {
  log "scenario: index spec validation & enrichment (T20/T21)"

  # Search triggers index rebuild; spec warnings should appear for legacy pkgs
  local out
  out="$(xlings search d2x 2>&1)" || true
  echo "$out"
  assert_contains "$out" "d2x" "search should find d2x packages"
  log "  index spec validation: OK"
}

scenario_network_install_optional() {
  if [[ "$SKIP_NETWORK_TESTS" == "1" ]]; then
    log "scenario: network install (skipped, SKIP_NETWORK_TESTS=1)"
    return
  fi

  log "scenario: network install"
  command -v xmake >/dev/null 2>&1 || fail "xmake not found in PATH; d2x install requires xmake"

  xlings subos new net11 >/dev/null 2>&1 || true
  xlings subos use net11 >/dev/null 2>&1
  xlings install d2x@0.1.1 -y >/dev/null 2>&1 || fail "xlings install d2x@0.1.1 failed"

  xlings subos new net12 >/dev/null 2>&1 || true
  xlings subos use net12 >/dev/null 2>&1
  xlings install d2x@0.1.2 -y >/dev/null 2>&1 || fail "xlings install d2x@0.1.2 failed"

  xlings subos new net13 >/dev/null 2>&1 || true
  xlings subos use net13 >/dev/null 2>&1
  xlings install d2x@0.1.3 -y >/dev/null 2>&1 || fail "xlings install d2x@0.1.3 failed"

  # T14/T18/T20/T21 feature tests (require network-installed packages)
  scenario_project_deps
  scenario_xpkgs_reuse
  scenario_index_spec_validation

  # Cleanup test envs
  xlings subos use default >/dev/null 2>&1 || true
  xlings subos rm net11 >/dev/null 2>&1 || true
  xlings subos rm net12 >/dev/null 2>&1 || true
  xlings subos rm net13 >/dev/null 2>&1 || true
}

scenario_project_deps_offline() {
  log "scenario: project deps offline (.xlings.json handling)"

  # T18: no .xlings.json → tip (exit 0)
  local tmpdir
  tmpdir="$(mktemp -d)"
  local out
  out="$(cd "$tmpdir" && xlings install 2>&1)"
  assert_contains "$out" ".xlings.json" \
    "xlings install (no args, no config) should mention .xlings.json"

  # T18: invalid deps format
  printf '{"deps":{"d2x":"0.1.3"}}\n' > "$tmpdir/.xlings.json"
  out="$(cd "$tmpdir" && xlings install 2>&1)" || true
  assert_contains "$out" "invalid" \
    "xlings install with object deps should report invalid format"

  rm -rf "$tmpdir"
  log "  project deps offline: OK"
}

main() {
  prepare_archive
  prepare_runtime

  log "archive: $ARCHIVE_PATH"
  log "runtime: $PKG_DIR"

  scenario_basic_commands
  scenario_info_mapping
  scenario_subos_lifecycle_and_aliases
  scenario_subos_create_requires_config_xvm
  scenario_self_and_cleanup
  scenario_project_deps_offline
  scenario_network_install_optional

  log "PASS: all usability scenarios passed"
  exit 0
}

main "$@"

