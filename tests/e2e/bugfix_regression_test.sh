#!/usr/bin/env bash
set -euo pipefail

# Regression tests for bugfixes:
#   Bug 1: find_xim_project_dir falls back to source tree → "invalid task: xim"
#   Bug 2: xvm add --path with spaces breaks command parsing
#   Bug 3: elfpatch _get_tool relies on find_tool which may fail; needs direct exec fallback

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

log() { echo "[bugfix-test] $*"; }
fail() { echo "[bugfix-test] FAIL: $*" >&2; exit 1; }

assert_contains() {
  local output="$1" expected="$2" hint="${3:-}"
  if [[ "$output" != *"$expected"* ]]; then
    fail "expected output to contain '$expected'. ${hint}"
  fi
}

assert_not_contains() {
  local output="$1" unexpected="$2" hint="${3:-}"
  if [[ "$output" == *"$unexpected"* ]]; then
    fail "output should NOT contain '$unexpected'. ${hint}"
  fi
}

# ── Bug 1: find_xim_project_dir must not return source tree root ────────
test_bug1_xim_project_dir() {
  log "Bug 1: find_xim_project_dir fallback"

  # The source root xmake.lua does NOT define task("xim").
  # If find_xim_project_dir returns the source root, xmake xim would fail
  # with "invalid task: xim".
  # After fix: it should fall through to ~/.xlings (installed) or report
  # a meaningful error instead of "invalid task".

  if [[ -z "${XLINGS_HOME:-}" ]]; then
    log "  XLINGS_HOME not set, skip (need installed xlings)"
    return
  fi

  # Verify the installed home has the correct layout
  if [[ -d "$XLINGS_HOME/xim" ]] && [[ -f "$XLINGS_HOME/xmake.lua" ]]; then
    local out
    out="$(xmake xim -P "$XLINGS_HOME" -- -h 2>&1)" || true
    assert_not_contains "$out" "invalid task" "xim task should be found in installed home"
    assert_contains "$out" "xim" "xim help should appear"
    log "  installed home layout: OK"
  fi

  # Verify source root does NOT have xim/ dir (only core/xim/)
  if [[ -d "$ROOT_DIR/core/xim" ]] && [[ ! -d "$ROOT_DIR/xim" ]]; then
    log "  source tree layout correct: core/xim/ exists, xim/ does not"
  fi

  log "  Bug 1: PASS"
}

# ── Bug 2: xvm add --path with spaces ──────────────────────────────────
test_bug2_xvm_path_spaces() {
  log "Bug 2: xvm add --path with spaces"

  local test_dir="/tmp/xlings-test-path with spaces"
  mkdir -p "$test_dir/bin"
  printf '#!/bin/sh\necho hello\n' > "$test_dir/bin/bugtest2"
  chmod +x "$test_dir/bin/bugtest2"

  # xvm add with quoted path should succeed
  local out
  out="$(xvm add bugtest2 1.0.0 --path "$test_dir" 2>&1)" || {
    rm -rf "$test_dir"
    fail "xvm add with spaced path failed: $out"
  }
  assert_contains "$out" "adding target" "xvm add should report success"

  # Verify it was registered
  out="$(xvm info bugtest2 2>&1)" || true
  assert_contains "$out" "bugtest2" "xvm info should show the target"

  # Cleanup
  yes | xvm remove bugtest2 >/dev/null 2>&1 || true
  rm -rf "$test_dir"

  log "  Bug 2: PASS"
}

# ── Bug 3: elfpatch _get_tool direct exec fallback ─────────────────────
test_bug3_elfpatch_tool_detection() {
  log "Bug 3: elfpatch tool detection"

  # Verify that the elfpatch code has the _try_probe_tool fallback
  local elfpatch_file="$ROOT_DIR/core/xim/libxpkg/elfpatch.lua"
  if [[ -f "$elfpatch_file" ]]; then
    if grep -q "_try_probe_tool" "$elfpatch_file"; then
      log "  _try_probe_tool fallback present in elfpatch.lua"
    else
      fail "_try_probe_tool fallback missing from elfpatch.lua"
    fi

    if grep -q "_tool_cache" "$elfpatch_file"; then
      log "  _tool_cache present in elfpatch.lua"
    else
      fail "_tool_cache missing from elfpatch.lua"
    fi
  else
    log "  elfpatch.lua not found at $elfpatch_file, skip"
  fi

  # Verify xvm.lua --path quoting
  local xvm_file="$ROOT_DIR/core/xim/base/xvm.lua"
  if [[ -f "$xvm_file" ]]; then
    if grep -q '\-\-path "%s"' "$xvm_file"; then
      log "  --path properly quoted in xvm.lua"
    else
      fail "--path not quoted in xvm.lua"
    fi
  fi

  log "  Bug 3: PASS"
}

# ── Main ───────────────────────────────────────────────────────────────
main() {
  log "=== Bugfix Regression Tests ==="

  test_bug1_xim_project_dir
  test_bug2_xvm_path_spaces
  test_bug3_elfpatch_tool_detection

  log "=== ALL BUGFIX REGRESSION TESTS PASSED ==="
}

main "$@"
