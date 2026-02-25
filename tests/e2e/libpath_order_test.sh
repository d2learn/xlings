#!/usr/bin/env bash
set -euo pipefail

TARGET_NEW="xvm-libpath-order-test"
TARGET_LEGACY="xvm-libpath-legacy-test"
XVM_BIN="${XVM_BIN:-xvm}"

xvm_cmd() {
  "$XVM_BIN" "$@"
}

cleanup() {
  xvm_cmd remove "$TARGET_NEW" -y >/dev/null 2>&1 || true
  xvm_cmd remove "$TARGET_LEGACY" -y >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[libpath-test] add target with new closure fields"
xvm_cmd add "$TARGET_NEW" 0.0.1 --alias "env" \
  --env "XLINGS_PROGRAM_LIBPATH=/tmp/a:/tmp/b" \
  --env "XLINGS_EXTRA_LIBPATH=/tmp/b:/tmp/c" >/dev/null

INFO_NEW="$(xvm_cmd info "$TARGET_NEW" 0.0.1 2>&1)"
echo "$INFO_NEW"

if [[ "$INFO_NEW" != *"XLINGS_PROGRAM_LIBPATH=/tmp/a:/tmp/b"* ]]; then
  echo "[libpath-test] FAIL: missing XLINGS_PROGRAM_LIBPATH"
  exit 1
fi

if [[ "$INFO_NEW" != *"XLINGS_EXTRA_LIBPATH=/tmp/b:/tmp/c"* ]]; then
  echo "[libpath-test] FAIL: missing XLINGS_EXTRA_LIBPATH"
  exit 1
fi

# precedence + de-dup check: /tmp/b should appear once
if [[ "$INFO_NEW" != *"LD_LIBRARY_PATH=/tmp/a:/tmp/b:/tmp/c:"* ]]; then
  echo "[libpath-test] FAIL: unexpected LD_LIBRARY_PATH order"
  exit 1
fi
if [[ "$INFO_NEW" == *"/tmp/b:/tmp/b"* ]]; then
  echo "[libpath-test] FAIL: /tmp/b duplicated in LD_LIBRARY_PATH"
  exit 1
fi

echo "[libpath-test] add target with legacy LD_LIBRARY_PATH"
xvm_cmd add "$TARGET_LEGACY" 0.0.1 --alias "env" \
  --env "LD_LIBRARY_PATH=/legacy/path" >/dev/null

INFO_LEGACY="$(xvm_cmd info "$TARGET_LEGACY" 0.0.1 2>&1)"
echo "$INFO_LEGACY"

if [[ "$INFO_LEGACY" != *"XLINGS_EXTRA_LIBPATH=/legacy/path"* ]]; then
  echo "[libpath-test] FAIL: legacy LD_LIBRARY_PATH not migrated to EXTRA"
  exit 1
fi

echo "[libpath-test] PASS"

