#!/usr/bin/env bash
set -euo pipefail

TARGET_DEPRECATED="xvm-libpath-deprecated-test"
TARGET_DIRECT="xvm-libpath-direct-test"
XVM_BIN="${XVM_BIN:-xvm}"

xvm_cmd() {
  "$XVM_BIN" "$@"
}

cleanup() {
  xvm_cmd remove "$TARGET_DEPRECATED" -y >/dev/null 2>&1 || true
  xvm_cmd remove "$TARGET_DIRECT" -y >/dev/null 2>&1 || true
}
trap cleanup EXIT

# --- Test 1: deprecated fields are silently ignored ---
echo "[libpath-test] Test 1: deprecated XLINGS_PROGRAM_LIBPATH / XLINGS_EXTRA_LIBPATH are ignored"
xvm_cmd add "$TARGET_DEPRECATED" 0.0.1 --alias "env" \
  --env "XLINGS_PROGRAM_LIBPATH=/tmp/a:/tmp/b" \
  --env "XLINGS_EXTRA_LIBPATH=/tmp/b:/tmp/c" >/dev/null

INFO_DEP="$(xvm_cmd info "$TARGET_DEPRECATED" 0.0.1 2>&1)"
echo "$INFO_DEP"

if [[ "$INFO_DEP" == *"XLINGS_PROGRAM_LIBPATH"* ]]; then
  echo "[libpath-test] FAIL: XLINGS_PROGRAM_LIBPATH should be silently ignored"
  exit 1
fi

if [[ "$INFO_DEP" == *"XLINGS_EXTRA_LIBPATH"* ]]; then
  echo "[libpath-test] FAIL: XLINGS_EXTRA_LIBPATH should be silently ignored"
  exit 1
fi

# --- Test 2: shim does not compose LD_LIBRARY_PATH ---
echo "[libpath-test] Test 2: shim does not inject LD_LIBRARY_PATH"

if [[ "$INFO_DEP" == *"LD_LIBRARY_PATH="* ]]; then
  echo "[libpath-test] FAIL: shim should not set LD_LIBRARY_PATH (RPATH-only)"
  exit 1
fi

# --- Test 3: direct LD_LIBRARY_PATH in envs flows through ---
echo "[libpath-test] Test 3: direct LD_LIBRARY_PATH flows through envs (exception path)"
xvm_cmd add "$TARGET_DIRECT" 0.0.1 --alias "env" \
  --env "LD_LIBRARY_PATH=/exception/path" >/dev/null

INFO_DIRECT="$(xvm_cmd info "$TARGET_DIRECT" 0.0.1 2>&1)"
echo "$INFO_DIRECT"

if [[ "$INFO_DIRECT" != *"LD_LIBRARY_PATH=/exception/path"* ]]; then
  echo "[libpath-test] FAIL: direct LD_LIBRARY_PATH should appear as regular env"
  exit 1
fi

echo "[libpath-test] PASS (RPATH-only policy verified)"
