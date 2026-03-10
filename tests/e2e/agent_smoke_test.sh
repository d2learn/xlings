#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$ROOT_DIR/build/linux/x86_64/release/xlings"

log() { echo "[agent_smoke] $*"; }
fail() { echo "[agent_smoke] FAIL: $*" >&2; exit 1; }

[[ -f "$BIN" ]] || fail "binary not found at $BIN, run xmake build first"

# Test 1: agent without API key should fail gracefully
set +e
OUT=$("$BIN" agent 2>&1 <<< "hello")
RC=$?
set -e
echo "$OUT" | grep -qi "api key\|API key\|not set" || fail "agent should report missing API key"
log "PASS: agent reports missing API key"

# Test 2: agent mcp placeholder should not crash
set +e
OUT=$("$BIN" agent mcp 2>&1)
set -e
echo "$OUT" | grep -qi "not yet implemented\|mcp" || fail "agent mcp should report not implemented"
log "PASS: agent mcp placeholder works"

log "All agent smoke tests passed"
