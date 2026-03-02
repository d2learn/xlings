#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/release_test_lib.sh"

QUICK_HOME="$RUNTIME_ROOT/release_quick_install_home"
rm -rf "$QUICK_HOME"
mkdir -p "$QUICK_HOME"

HOME="$QUICK_HOME" \
PATH="$(minimal_system_path)" \
XLINGS_NON_INTERACTIVE=1 \
env -u XLINGS_HOME \
  bash -c 'curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash'

INSTALLED_HOME="$QUICK_HOME/.xlings"
[[ -x "$INSTALLED_HOME/subos/current/bin/xlings" ]] || fail "quick install did not create current xlings shim"

QUICK_PATH="$INSTALLED_HOME/subos/current/bin:$INSTALLED_HOME/bin:$(minimal_system_path)"
HOME="$QUICK_HOME" PATH="$QUICK_PATH" env -u XLINGS_HOME xlings -h >/dev/null
HOME="$QUICK_HOME" PATH="$QUICK_PATH" env -u XLINGS_HOME xlings config >/dev/null
HOME="$QUICK_HOME" PATH="$QUICK_PATH" env -u XLINGS_HOME xlings --version >/dev/null

log "PASS: quick install scenario"
