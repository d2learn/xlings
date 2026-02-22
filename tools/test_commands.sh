#!/usr/bin/env bash
# Run tests in the packaged xlings directory (build/xlings) so XLINGS_HOME is the package root, not /tmp.
# Exit immediately on first test failure or Ctrl+C.
# Usage: ./tools/test_commands.sh
#   Ensure package exists: ./tools/linux_release.sh  (or script will try to create it)
#   XLINGS_SDK=/path/to/gcc15  to build with SDK before testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Ctrl+C: exit immediately (TEST_DATA cleanup done in main after it is set)
trap 'echo ""; echo "Interrupted."; exit 130' INT

# Build so the test script uses the same binary as "xmake build" (no stale binary).
# - If XLINGS_SDK set: reconfigure with SDK, clean, build.
# - Else: just run xmake build (use your existing config, same as building in terminal).
if [[ -n "${XLINGS_SDK:-}" ]]; then
  echo "Building with SDK=$XLINGS_SDK ..."
  (cd "$PROJECT_DIR" && XLINGS_NOLINKSTATIC="${XLINGS_NOLINKSTATIC:-}" xmake f --sdk="$XLINGS_SDK" && xmake clean && xmake build) || {
    echo "Error: xmake f --sdk=$XLINGS_SDK && xmake clean && xmake build failed"
    exit 1
  }
else
  echo "Building (using current xmake config)..."
  (cd "$PROJECT_DIR" && xmake build) || {
    echo "Warning: xmake build failed, will use existing binary if any."
  }
fi
# Resolve raw binary and optionally refresh build/xlings/bin/.xlings.real
RAW_BIN="$PROJECT_DIR/build/linux/x86_64/release/xlings"
TARGETFILE="$(cd "$PROJECT_DIR" && xmake show --target=xlings targetfile 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | tr -d '\r' | tail -1)"
if [[ -n "$TARGETFILE" ]]; then
  [[ "$TARGETFILE" != /* ]] && TARGETFILE="$PROJECT_DIR/$TARGETFILE"
  [[ -x "$TARGETFILE" ]] && RAW_BIN="$TARGETFILE"
fi
PKG_REAL="$PROJECT_DIR/build/xlings/bin/.xlings.real"
if [[ -x "$RAW_BIN" && -d "$PROJECT_DIR/build/xlings/bin" ]]; then
  cp -f "$RAW_BIN" "$PKG_REAL" 2>/dev/null && chmod +x "$PKG_REAL" && echo "Updated .xlings.real from build."
fi

# Prefer packaged dir so XLINGS_HOME is build/xlings (wrapper sets it)
PKG_DIR="$PROJECT_DIR/build/xlings"
PKG_BIN="$PKG_DIR/bin/xlings"

# Run one test; on failure exit immediately (and on INT we already trap)
run_test() {
  local name="$1"
  shift
  if "$@"; then
    echo "  OK: $name"
  else
    echo "  FAIL: $name (exit $?)"
    exit 1
  fi
}

# --- main: run in packaged directory (XLINGS_HOME = build/xlings) ---
if [[ ! -x "$PKG_BIN" ]]; then
  echo "Package not found at $PKG_DIR. Creating it..."
  if [[ -x "$PROJECT_DIR/tools/linux_release.sh" ]]; then
    "$PROJECT_DIR/tools/linux_release.sh" || true
  fi
  if [[ ! -x "$PKG_BIN" ]]; then
    echo "Error: Run ./tools/linux_release.sh first to create build/xlings, then run this script."
    exit 1
  fi
fi

trap 'echo ""; echo "Interrupted."; exit 130' INT

# Avoid git hanging when no TTY (e.g. in script/CI): never prompt, fail fast instead
export GIT_TERMINAL_PROMPT=0

# Use packaged wrapper: XLINGS_HOME and XLINGS_DATA are set by wrapper to $PKG_DIR and $PKG_DIR/data (no /tmp)
unset XLINGS_DATA
XLINGS_RUN=("$PKG_BIN")

echo "xlings: $PKG_BIN"
echo "XLINGS_HOME: $PKG_DIR"
echo "XLINGS_DATA: $PKG_DIR/data (package)"
echo ""

# --- C++ commands (help, config) ---
run_test "help / version" "${XLINGS_RUN[@]}" help
run_test "config (print paths)" "${XLINGS_RUN[@]}" config

# --- self subcommands ---
run_test "self help" "${XLINGS_RUN[@]}" self help
run_test "self init" "${XLINGS_RUN[@]}" self init
run_test "self config" "${XLINGS_RUN[@]}" self config
run_test "self update (no-op if not git)" "${XLINGS_RUN[@]}" self update
run_test "self clean" "${XLINGS_RUN[@]}" self clean

# --- xim: update index (network, timeout 90s), then search / list ---
echo ""
echo "Updating index (network, max 90s)..."
timeout 90 "${XLINGS_RUN[@]}" update --update index 2>/dev/null || true
run_test "search (keyword)" "${XLINGS_RUN[@]}" search d2x
run_test "list (-l)" "${XLINGS_RUN[@]}" install -l 2>/dev/null

# --- optional: install/remove (can be slow, requires network) ---
if [[ -n "${XLINGS_TEST_INSTALL:-}" ]]; then
  echo ""
  run_test "install d2x -y" "${XLINGS_RUN[@]}" install d2x -y
  run_test "remove d2x -y" "${XLINGS_RUN[@]}" remove d2x -y
fi

echo ""
echo "All tests passed."
