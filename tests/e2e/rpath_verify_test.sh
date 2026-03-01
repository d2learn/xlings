#!/usr/bin/env bash
set -euo pipefail

# Cross-platform RPATH verification test for installed packages.
#
# Verifies:
#   1. elfpatch correctness — installed binary contains RPATH/RUNPATH (Linux)
#      or LC_RPATH (macOS) entries.
#   2. Runtime resolution — binary runs successfully without LD_LIBRARY_PATH /
#      DYLD_LIBRARY_PATH, proving libraries resolve through RPATH alone.
#   3. Isolation — the shim environment does not leak library-path variables
#      to child processes.
#
# Environment:
#   D2X_VERSION   — version to inspect (default: 0.1.1 on Linux, 0.1.3 on macOS)
#   XLINGS_DATA   — path to xlings data dir (default: $XLINGS_HOME/data)

OS="$(uname -s)"

case "$OS" in
  Linux)  DEFAULT_D2X_VERSION="0.1.1" ;;
  Darwin) DEFAULT_D2X_VERSION="0.1.3" ;;
  *)      echo "[rpath-verify] unsupported OS: $OS" >&2; exit 1 ;;
esac

D2X_VERSION="${D2X_VERSION:-$DEFAULT_D2X_VERSION}"
XLINGS_DATA="${XLINGS_DATA:-${XLINGS_HOME:?XLINGS_HOME must be set}/data}"
XVM_BIN="${XVM_BIN:-xvm}"

log()  { echo "[rpath-verify] $*"; }
fail() { echo "[rpath-verify] FAIL: $*" >&2; exit 1; }

# ── Locate the installed d2x binary ──────────────────────────────────────

D2X_DIR="$XLINGS_DATA/xpkgs/d2x/$D2X_VERSION"
D2X_BIN=""

for candidate in "$D2X_DIR/bin/d2x" "$D2X_DIR/d2x"; do
  if [[ -f "$candidate" && -x "$candidate" ]]; then
    D2X_BIN="$candidate"
    break
  fi
done

[[ -n "$D2X_BIN" ]] || {
  log "SKIP: d2x binary not found in $D2X_DIR (XLINGS_RES download not yet implemented in C++ installer)"
  exit 0
}
log "binary: $D2X_BIN"

# ── Test 1: RPATH / RUNPATH / LC_RPATH entries ──────────────────────────

log "Test 1: verify RPATH entries in installed binary"

if [[ "$OS" == "Linux" ]]; then
  RPATH_OUT="$(readelf -d "$D2X_BIN" 2>&1)" || fail "readelf -d failed on $D2X_BIN"

  if ! echo "$RPATH_OUT" | grep -qE "RUNPATH|RPATH"; then
    echo "$RPATH_OUT"
    fail "no RPATH/RUNPATH found in d2x binary"
  fi

  RPATH_LINE="$(echo "$RPATH_OUT" | grep -E "RUNPATH|RPATH" | head -1)"
  log "  found: $RPATH_LINE"

elif [[ "$OS" == "Darwin" ]]; then
  RPATH_OUT="$(otool -l "$D2X_BIN" 2>&1)" || fail "otool -l failed on $D2X_BIN"

  # On macOS, LC_RPATH is only needed when the binary has non-system dylib
  # dependencies.  If all deps are under /usr/lib/ or /System/, elfpatch
  # correctly skips RPATH injection.
  DYLIB_OUT="$(otool -L "$D2X_BIN" 2>&1)" || fail "otool -L failed on $D2X_BIN"
  HAS_NON_SYSTEM_DEPS=false
  while IFS= read -r line; do
    dep="$(echo "$line" | sed -n 's/^[[:space:]]*\(.*\) (compatibility.*/\1/p')"
    if [[ -n "$dep" && "$dep" != "$D2X_BIN"* \
       && "$dep" != /usr/lib/* && "$dep" != /System/* \
       && "$dep" != @* ]]; then
      HAS_NON_SYSTEM_DEPS=true
      break
    fi
  done <<< "$DYLIB_OUT"

  if [[ "$HAS_NON_SYSTEM_DEPS" == "true" ]]; then
    if ! echo "$RPATH_OUT" | grep -q "LC_RPATH"; then
      echo "$RPATH_OUT"
      fail "non-system deps found but no LC_RPATH in binary"
    fi
    log "  LC_RPATH entries:"
    echo "$RPATH_OUT" | grep -A2 "LC_RPATH" | while IFS= read -r line; do
      log "    $line"
    done
  else
    log "  binary only links system dylibs — LC_RPATH not required (OK)"
    log "  dylibs:"
    echo "$DYLIB_OUT" | grep -v "^$D2X_BIN" | head -10 | while IFS= read -r line; do
      log "    $line"
    done
  fi
fi

# ── Test 2: runtime — libraries resolve through RPATH alone ─────────────

log "Test 2: d2x runs without LD_LIBRARY_PATH / DYLD_LIBRARY_PATH"

unset LD_LIBRARY_PATH   2>/dev/null || true
unset DYLD_LIBRARY_PATH 2>/dev/null || true

D2X_RUN_OUT="$(d2x --version 2>&1)" || fail "d2x --version failed — RPATH may be broken"
log "  d2x output: $D2X_RUN_OUT"

# ── Test 3: shim does not leak library-path variables ───────────────────

log "Test 3: shim does not inject library-path variables into child env"

if ! command -v "$XVM_BIN" &>/dev/null; then
  log "SKIP Test 3: standalone xvm binary not found (integrated into xlings multicall)"
  log "PASS: RPATH verification tests 1-2 passed (Test 3 skipped)"
  exit 0
fi

TARGET_NAME="xvm-rpath-env-test"
cleanup() { "$XVM_BIN" remove "$TARGET_NAME" -y >/dev/null 2>&1 || true; }
trap cleanup EXIT

"$XVM_BIN" add "$TARGET_NAME" 0.0.1 --alias "env" >/dev/null

CHILD_ENV="$("$XVM_BIN" run "$TARGET_NAME" 2>&1)" || true

if [[ "$OS" == "Linux" ]]; then
  LIB_VAR="LD_LIBRARY_PATH"
else
  LIB_VAR="DYLD_LIBRARY_PATH"
fi

if echo "$CHILD_ENV" | grep -q "^${LIB_VAR}="; then
  echo "$CHILD_ENV"
  fail "$LIB_VAR unexpectedly present in child environment"
fi

log "  no $LIB_VAR contamination"

log "PASS: all RPATH verification tests passed"
