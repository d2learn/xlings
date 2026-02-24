#!/usr/bin/env bash
set -euo pipefail

# End-to-end usability test for macOS release package.
# Scenarios mirror Linux E2E where platform behavior is equivalent.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
TMP_BASE="${TMPDIR:-/tmp}/xlings-e2e-macos-$$"
ARCHIVE_PATH="${1:-}"
SKIP_NETWORK_TESTS="${SKIP_NETWORK_TESTS:-0}"
D2X_VERSION="${D2X_VERSION:-0.1.3}"

log() { echo "[e2e-macos] $*"; }
fail() { echo "[e2e-macos] FAIL: $*" >&2; exit 1; }

cleanup() {
  [[ -d "$TMP_BASE" ]] && rm -rf "$TMP_BASE" || true
}
# Ensure EXIT trap does not override script exit code (some shells use trap's exit status)
trap 'cleanup; true' EXIT

assert_contains() {
  local output="$1"
  local expected="$2"
  local hint="${3:-}"
  if [[ "$output" != *"$expected"* ]]; then
    fail "expected output to contain '$expected'. ${hint}"
  fi
}

prepare_runtime_from_archive() {
  mkdir -p "$TMP_BASE"
  tar -xzf "$ARCHIVE_PATH" -C "$TMP_BASE"
  PKG_DIR="$(ls -d "$TMP_BASE"/xlings-*-macosx-arm64 2>/dev/null | head -1 || true)"
  [[ -n "$PKG_DIR" ]] || fail "failed to locate extracted package dir"
}

prepare_runtime() {
  if [[ -n "${XLINGS_HOME:-}" && -d "${XLINGS_HOME:-}" ]]; then
    PKG_DIR="$XLINGS_HOME"
    return
  fi

  if [[ -z "$ARCHIVE_PATH" ]]; then
    ARCHIVE_PATH="$(ls -t "$BUILD_DIR"/xlings-*-macosx-arm64.tar.gz 2>/dev/null | head -1 || true)"
  fi
  [[ -n "$ARCHIVE_PATH" ]] || fail "macOS release archive not found"
  [[ -f "$ARCHIVE_PATH" ]] || fail "archive not found: $ARCHIVE_PATH"

  prepare_runtime_from_archive
}

setup_env() {
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

scenario_network_install_optional() {
  if [[ "$SKIP_NETWORK_TESTS" == "1" ]]; then
    log "scenario: network install (skipped, SKIP_NETWORK_TESTS=1)"
    return
  fi

  log "scenario: network install (xlings install d2x)"
  command -v xmake >/dev/null 2>&1 || fail "xmake not found in PATH; d2x install requires xmake"

  xlings subos new netmac >/dev/null 2>&1 || true
  xlings subos use netmac >/dev/null 2>&1
  local out
  out="$(xlings install "d2x@${D2X_VERSION}" -y 2>&1)" || fail "xlings install d2x@${D2X_VERSION} failed"
  assert_contains "$out" "0.1" "install output should show d2x version"
  assert_contains "$out" "installed" "install output should confirm installed"

  xlings subos use default >/dev/null 2>&1 || true
  xlings subos rm netmac >/dev/null 2>&1 || true
}

main() {
  prepare_runtime
  setup_env

  log "runtime: $PKG_DIR"
  scenario_basic_commands
  scenario_info_mapping
  scenario_subos_lifecycle_and_aliases
  scenario_self_and_cleanup
  scenario_network_install_optional

  log "PASS: all usability scenarios passed"
  exit 0
}

main "$@"

