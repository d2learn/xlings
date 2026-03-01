#!/usr/bin/env bash
set -euo pipefail

# Regression tests for bugfixes:
#   Bug 3: elfpatch _get_tool relies on find_tool which may fail; needs direct exec fallback
#
# Note: Bug 1 (find_xim_project_dir) and Bug 2 (xvm add --path with spaces)
# were removed — they tested the old xmake-based xim and standalone Rust xvm
# binary, which no longer exist after the C++ integration.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

log() { echo "[bugfix-test] $*"; }
fail() { echo "[bugfix-test] FAIL: $*" >&2; exit 1; }

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

  log "  Bug 3: PASS"
}

# ── Main ───────────────────────────────────────────────────────────────
main() {
  log "=== Bugfix Regression Tests ==="

  test_bug3_elfpatch_tool_detection

  log "=== ALL BUGFIX REGRESSION TESTS PASSED ==="
}

main "$@"
