#!/usr/bin/env bash
# Minimal xlings interface v1 client — bash + jq.
# Exercises: --version, --list, env, list_subos, plan_install.
#
# Usage:
#   XLINGS_HOME=/tmp/xlings-client-test bash examples/clients/bash_jq.sh
set -euo pipefail

# Locate xlings binary. Prefer ./build/.../xlings (dev) then $PATH.
xlings=$(command -v xlings || true)
for cand in build/linux/x86_64/release/xlings build/macosx/arm64/release/xlings build/macosx/x86_64/release/xlings; do
    [ -x "$cand" ] && xlings="$(realpath "$cand")" && break
done
if [ -z "$xlings" ]; then
    echo "ERROR: xlings binary not found" >&2
    exit 1
fi
echo "→ using $xlings"

# === 1. protocol version ===
v=$("$xlings" interface --version | jq -r .protocol_version)
echo "protocol_version: $v"
[ "$v" = "1.0" ] || { echo "ERROR: unexpected version $v" >&2; exit 1; }

# === 2. capability list ===
n=$("$xlings" interface --list | jq '.capabilities | length')
echo "capability count: $n"
"$xlings" interface --list | jq -r '.capabilities | map(.name) | sort | .[]' | sed 's/^/  - /'

# === 3. env capability ===
echo "→ env:"
"$xlings" interface env --args '{}' \
    | jq -r 'select(.kind=="data" and .dataKind=="env") | .payload | "  XLINGS_HOME = \(.xlingsHome)\n  activeSubos  = \(.activeSubos)\n  binDir       = \(.binDir)"'

# === 4. list_subos ===
echo "→ sub-OSs:"
"$xlings" interface list_subos --args '{}' \
    | jq -r 'select(.kind=="data" and .dataKind=="subos_list") | .payload.entries | map("  - \(.name) (\(if .active then "active" else "inactive" end), \(.pkgCount) pkg)") | .[]'

# === 5. plan_install (dry-run, doesn't actually install) ===
echo "→ plan_install xim:bun (dry-run):"
"$xlings" interface plan_install --args '{"targets":["xim:bun"]}' \
    | jq -r 'select(.kind=="data" and .dataKind=="install_plan") | .payload.packages | map("  + \(.[0])") | .[] // empty'

# === 6. terminal result line for the dry-run ===
exit_code=$("$xlings" interface plan_install --args '{"targets":["xim:bun"]}' \
    | jq -s 'map(select(.kind=="result"))[0].exitCode')
echo "plan_install exitCode: $exit_code"

echo "✓ bash_jq client done"
