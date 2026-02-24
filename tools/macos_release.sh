#!/usr/bin/env bash
# Build a self-contained xlings package for macOS arm64.
#
# Directory layout matches linux_release.sh (see that file for details).
#
# Output:  build/xlings-<ver>-macosx-arm64.tar.gz
# Usage:   ./tools/macos_release.sh
# Env:     SKIP_NETWORK_VERIFY=1   skip network-dependent tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION=$(sed -n 's/.*VERSION = "\([^"]*\)".*/\1/p' "$PROJECT_DIR/core/config.cppm" | head -1)
[[ -z "$VERSION" ]] && VERSION="0.2.0"

ARCH="arm64"
PKG_NAME="xlings-${VERSION}-macosx-${ARCH}"
OUT_DIR="$PROJECT_DIR/build/$PKG_NAME"

TEST_DATA=""
cleanup() {
  [[ -n "$TEST_DATA" && -d "$TEST_DATA" ]] && rm -rf "$TEST_DATA"
}
trap cleanup EXIT

info()  { echo "[release] $*"; }
fail()  { echo "[release] FAIL: $*" >&2; exit 1; }

cd "$PROJECT_DIR"

# ── 1. Build C++ ─────────────────────────────────────────────────
info "Version: $VERSION  |  Arch: $ARCH"
info "Building C++ binary..."
xmake clean -q 2>/dev/null || true
xmake build xlings 2>&1 || fail "xmake build failed"

BIN_SRC="build/macosx/${ARCH}/release/xlings"
[[ -f "$BIN_SRC" ]] || fail "C++ binary not found at $BIN_SRC"

info "Verifying no LLVM toolchain dependency..."
if otool -L "$BIN_SRC" | grep -q "llvm"; then
    otool -L "$BIN_SRC"
    fail "Binary still links against LLVM dylib"
fi
info "OK: binary has no LLVM runtime dependency"

# ── 2. Build xvm (Rust) ─────────────────────────────────────────
info "Building xvm (Rust)..."
(cd core/xvm && cargo build --release)
XVM_DIR="core/xvm/target/release"
[[ -f "$XVM_DIR/xvm" ]]      || fail "xvm binary not found"
[[ -f "$XVM_DIR/xvm-shim" ]] || fail "xvm-shim binary not found"

# ── 3. Assemble package ─────────────────────────────────────────
info "Assembling $OUT_DIR ..."
rm -rf "$OUT_DIR"

mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/subos/default/bin"
mkdir -p "$OUT_DIR/subos/default/lib"
mkdir -p "$OUT_DIR/subos/default/usr"
mkdir -p "$OUT_DIR/subos/default/xvm"
mkdir -p "$OUT_DIR/subos/default/generations"
ln -sfn default "$OUT_DIR/subos/current"
mkdir -p "$OUT_DIR/xim"
mkdir -p "$OUT_DIR/data/xpkgs"
mkdir -p "$OUT_DIR/data/runtimedir"
mkdir -p "$OUT_DIR/data/xim-index-repos"
mkdir -p "$OUT_DIR/data/local-indexrepo"
mkdir -p "$OUT_DIR/tools"

cp "$BIN_SRC"          "$OUT_DIR/bin/xlings"
cp "$XVM_DIR/xvm"      "$OUT_DIR/bin/xvm"
cp "$XVM_DIR/xvm-shim" "$OUT_DIR/bin/xvm-shim"
chmod +x "$OUT_DIR/bin/"*

for shim in xlings xvm xvm-shim; do
  cp "$OUT_DIR/bin/xvm-shim" "$OUT_DIR/subos/default/bin/$shim"
  chmod +x "$OUT_DIR/subos/default/bin/$shim"
done

cat > "$OUT_DIR/subos/default/xvm/versions.xvm.yaml" << 'YAML'
---
xlings:
  bootstrap:
    path: "../../bin"
xvm:
  bootstrap:
    path: "../../bin"
xvm-shim:
  bootstrap:
    path: "../../bin"
YAML

cat > "$OUT_DIR/subos/default/xvm/.workspace.xvm.yaml" << 'YAML'
---
xvm-wmetadata:
  name: global
  active: true
  inherit: true
versions:
  xlings: bootstrap
  xvm: bootstrap
  xvm-shim: bootstrap
YAML

cp -R core/xim/* "$OUT_DIR/xim/" 2>/dev/null || true

# Install script (bundled in package root)
cp "$PROJECT_DIR/tools/install-from-release.sh" "$OUT_DIR/install.sh"
chmod +x "$OUT_DIR/install.sh"

# i18n
mkdir -p "$OUT_DIR/config/i18n"
cp -R config/i18n/*.json "$OUT_DIR/config/i18n/" 2>/dev/null || true

# shell profiles
mkdir -p "$OUT_DIR/config/shell"
cp -R config/shell/* "$OUT_DIR/config/shell/" 2>/dev/null || true

# xmake.lua (package root)
cat > "$OUT_DIR/xmake.lua" << 'LUA'
add_moduledirs("xim")
add_moduledirs(".")
task("xim")
    on_run(function ()
        import("core.base.option")
        local xim_dir = path.join(os.projectdir(), "xim")
        local xim_entry = import("xim", {rootdir = xim_dir, anonymous = true})
        local args = option.get("arguments") or { "-h" }
        xim_entry.main(table.unpack(args))
    end)
    set_menu{
        usage = "xmake xim [arguments]",
        description = "xim package manager",
        options = {
            {nil, "arguments", "vs", nil, "xim arguments"},
        }
    }
LUA

# .xlings.json
if command -v jq &>/dev/null && [[ -f config/xlings.json ]]; then
  jq '. + {"activeSubos":"default","subos":{"default":{"dir":""}}}' \
    config/xlings.json > "$OUT_DIR/.xlings.json"
else
  cat > "$OUT_DIR/.xlings.json" << DOTJSON
{"activeSubos":"default","subos":{"default":{"dir":""}},"version":"$VERSION","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
DOTJSON
fi

# xim index-repos placeholder
echo '{}' > "$OUT_DIR/data/xim-index-repos/xim-indexrepos.json"

info "Package assembled: $OUT_DIR"

# ── 4. Verification ─────────────────────────────────────────────
info "=== Verification ==="

for f in bin/xlings bin/xvm bin/xvm-shim \
         subos/default/bin/xlings subos/default/bin/xvm subos/default/bin/xvm-shim; do
  [[ -x "$OUT_DIR/$f" ]] || fail "$f is missing or not executable"
done
info "OK: all binaries present and executable"

for d in subos/default/lib subos/default/usr subos/default/xvm subos/default/generations xim data/xpkgs config/i18n config/shell; do
  [[ -d "$OUT_DIR/$d" ]] || fail "directory $d missing"
done
[[ -L "$OUT_DIR/subos/current" ]] || fail "subos/current symlink missing"
[[ "$(readlink "$OUT_DIR/subos/current")" == "default" ]] || fail "subos/current does not point to default"
info "OK: directory structure valid (incl. subos/current symlink)"

[[ -f "$OUT_DIR/.xlings.json" ]] || fail ".xlings.json missing"
info "OK: .xlings.json present"

TEST_DATA="$PROJECT_DIR/build/.release_verify_$$"
mkdir -p "$TEST_DATA"

export XLINGS_HOME="$OUT_DIR"
export XLINGS_DATA="$OUT_DIR/data"
export XLINGS_SUBOS="$OUT_DIR/subos/current"
export PATH="$OUT_DIR/subos/current/bin:$OUT_DIR/bin:$PATH"

HELP_OUT=$("$OUT_DIR/bin/xlings" -h 2>&1) || fail "xlings -h failed"
echo "$HELP_OUT" | grep -q "subos" || fail "xlings -h missing 'subos' command"
info "OK: xlings -h shows subos/self commands"

CONFIG_OUT=$("$OUT_DIR/bin/xlings" config 2>&1) || fail "xlings config failed"
echo "$CONFIG_OUT" | grep -q "XLINGS_HOME" || fail "config output missing XLINGS_HOME"
info "OK: xlings config prints correct paths"

SUBOS_OUT=$("$OUT_DIR/bin/xlings" subos list 2>&1) || fail "xlings subos list failed"
echo "$SUBOS_OUT" | grep -q "default" || fail "subos list missing 'default'"
info "OK: xlings subos list shows default"

XVM_OUT=$("$OUT_DIR/bin/xvm" --version 2>&1) || fail "xvm --version failed"
info "OK: xvm --version = $XVM_OUT"

if [[ "${SKIP_NETWORK_VERIFY:-}" == "1" ]]; then
  info "Skip: network-dependent tests (SKIP_NETWORK_VERIFY=1)"
else
  export GIT_TERMINAL_PROMPT=0
  export GIT_CONNECT_TIMEOUT="${GIT_CONNECT_TIMEOUT:-30}"

  run_with_timeout() {
    local t="$1"; shift
    if command -v timeout &>/dev/null; then timeout "$t" "$@"; else "$@"; fi
  }

  info "Verify: xim --update index (timeout 300s)..."
  if ! run_with_timeout 300 bash -c \
    'cd "$1" && xmake xim -P . -- --update index' _ "$OUT_DIR"; then
    fail "xim --update index failed (network?). Set SKIP_NETWORK_VERIFY=1 to skip."
  fi

  info "Verify: xlings install d2x -y (timeout 300s)..."
  if ! run_with_timeout 300 bash -c \
    'cd "$1" && ./bin/xlings install d2x -y' _ "$OUT_DIR"; then
    fail "install d2x failed. Set SKIP_NETWORK_VERIFY=1 to skip."
  fi

  XPKG_D2X="$XLINGS_DATA/xpkgs/d2x"
  if [[ -d "$XPKG_D2X" ]] && compgen -G "$XPKG_D2X/*" > /dev/null 2>&1; then
    info "OK: data/xpkgs/d2x installed successfully"
  else
    fail "data/xpkgs/d2x not found after install"
  fi
fi

cleanup
trap - EXIT

# ── 5. Create archive ───────────────────────────────────────────
info ""
info "All checks passed. Creating release archive..."

ARCHIVE="$PROJECT_DIR/build/${PKG_NAME}.tar.gz"
tar -czf "$ARCHIVE" -C "$PROJECT_DIR/build" "$PKG_NAME"

info ""
info "Done."
info "  Package:  $OUT_DIR"
info "  Archive:  $ARCHIVE"
info ""
info "  Unpack & install:"
info "    tar -xzf ${PKG_NAME}.tar.gz"
info "    cd $PKG_NAME"
info "    ./install.sh"
info ""
