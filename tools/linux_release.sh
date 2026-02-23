#!/usr/bin/env bash
# Build a self-contained xlings package for Linux, run verification with data in a temp dir,
# then pack the release directory into xlings-<version>-linux-x86_64.tar.gz.
# Output dir: build/xlings-<version>-linux-x86_64
# Usage: ./tools/linux_release.sh
# Env:   SKIP_NETWORK_VERIFY=1  skip xim --update and install d2x (avoids network hang).
#        GIT_CONNECT_TIMEOUT=N  git connect timeout in seconds (default 30).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Version: read from core (single source of truth)
VERSION=$(sed -n 's/.*VERSION = "\([^"]*\)".*/\1/p' "$PROJECT_DIR/core/config.cppm" | head -1)
[[ -z "$VERSION" ]] && VERSION="0.1.0"

PKG_NAME="xlings-${VERSION}-linux-x86_64"
OUT_DIR="$PROJECT_DIR/build/$PKG_NAME"
TEST_DATA=""
cleanup_test_data() {
  if [[ -n "$TEST_DATA" && -d "$TEST_DATA" ]]; then
    echo "[linux_release] Cleaning up test data: $TEST_DATA"
    rm -rf "$TEST_DATA"
  fi
}

cd "$PROJECT_DIR"

echo "[linux_release] Version: $VERSION"
echo "[linux_release] Building C++ binary..."
xmake clean
if ! xmake build -q 2>/dev/null; then
  xmake build 2>/dev/null || true
fi

echo "[linux_release] Building xvm (Rust) via release-build.sh..."
(cd core/xvm && cargo clean && chmod +x release-build.sh && ./release-build.sh linux)

echo "[linux_release] Assembling $OUT_DIR ..."
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/data/bin"
mkdir -p "$OUT_DIR/data/lib"

# bin/: real binaries only (no script wrapper). data/bin/: xvm-shim copies as shims; data/bin/xlings,xvm,xvm-shim run with package env via xvm.
BIN_SRC="build/linux/x86_64/release/xlings"
XVM_RELEASE_DIR="core/xvm/target/x86_64-unknown-linux-musl/release"
if [[ ! -f "$BIN_SRC" ]]; then
  echo "[linux_release] Error: C++ binary not found at $BIN_SRC. Run xmake build first."
  exit 1
fi
cp "$BIN_SRC" "$OUT_DIR/bin/xlings"
chmod +x "$OUT_DIR/bin/xlings"
if [[ -f "$XVM_RELEASE_DIR/xvm" ]]; then
  cp "$XVM_RELEASE_DIR/xvm" "$OUT_DIR/bin/xvm"
  chmod +x "$OUT_DIR/bin/xvm"
fi
if [[ -f "$XVM_RELEASE_DIR/xvm-shim" ]]; then
  cp "$XVM_RELEASE_DIR/xvm-shim" "$OUT_DIR/bin/xvm-shim"
  chmod +x "$OUT_DIR/bin/xvm-shim"
fi

# data/bin/: all shims (xvm-shim copies). xlings/xvm/xvm-shim run with package env via detect_package_data_bin() and bootstrap config.
for shim_name in xlings xvm xvm-shim; do
  if [[ -f "$XVM_RELEASE_DIR/xvm-shim" ]]; then
    cp "$XVM_RELEASE_DIR/xvm-shim" "$OUT_DIR/data/bin/$shim_name"
    chmod +x "$OUT_DIR/data/bin/$shim_name"
  fi
done

# data/xvm/: bootstrap config so "xvm run xlings|xvm|xvm-shim" resolve to ../bin (relative to XLINGS_DATA = data dir)
mkdir -p "$OUT_DIR/data/xvm"
cat > "$OUT_DIR/data/xvm/versions.xvm.yaml" << 'VERSIONS'
---
xlings:
  bootstrap:
    path: "../bin"
xvm:
  bootstrap:
    path: "../bin"
xvm-shim:
  bootstrap:
    path: "../bin"
VERSIONS
cat > "$OUT_DIR/data/xvm/.workspace.xvm.yaml" << 'WORKSPACE'
---
xvm-wmetadata:
  name: global
  active: true
  inherit: true
versions:
  xlings: bootstrap
  xvm: bootstrap
  xvm-shim: bootstrap
WORKSPACE

# xim tree (Lua)
cp -R core/xim "$OUT_DIR/"

# Bundled xmake (single executable, no extraction). Use timeouts to avoid hanging on slow/unreachable network.
XMAKE_URL="https://github.com/xmake-io/xmake/releases/download/v3.0.7/xmake-bundle-v3.0.7.linux.x86_64"
XMAKE_BUNDLE_DIR="$OUT_DIR/tools/xmake"
mkdir -p "$XMAKE_BUNDLE_DIR/bin"
echo "[linux_release] Downloading bundled xmake..."
if curl -fSsL --connect-timeout 15 --max-time 120 -o "$XMAKE_BUNDLE_DIR/bin/xmake" "$XMAKE_URL"; then
  chmod +x "$XMAKE_BUNDLE_DIR/bin/xmake"
fi
if [[ -x "$XMAKE_BUNDLE_DIR/bin/xmake" ]]; then
  echo "[linux_release] Bundled xmake: $XMAKE_BUNDLE_DIR/bin/xmake"
else
  echo "[linux_release] Warning: bundled xmake not available; package will require system xmake"
fi

# data/xim: xim will clone index repo on first run (xim --update index)
mkdir -p "$OUT_DIR/data/xim"
mkdir -p "$OUT_DIR/data/xim/xim-index-repos"
echo '{}' > "$OUT_DIR/data/xim/xim-index-repos/xim-indexrepos.json"

# config/ for i18n
mkdir -p "$OUT_DIR/config/i18n"
cp -R config/i18n/*.json "$OUT_DIR/config/i18n/" 2>/dev/null || true

# xmake.lua at package root
cat > "$OUT_DIR/xmake.lua" << 'XMAKE_PKG'
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
XMAKE_PKG

# .xlings.json (data path + version from config)
if command -v jq &>/dev/null && [[ -f config/xlings.json ]]; then
  jq -s '.[0] + .[1]' config/xlings.json <(echo '{"data":"data"}') > "$OUT_DIR/.xlings.json"
else
  cat > "$OUT_DIR/.xlings.json" << DOTJSON
{"data":"data","version":"$VERSION","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
DOTJSON
fi

echo "[linux_release] Package assembled: $OUT_DIR"
echo ""

# --- Verification: use dir under build/ (not /tmp) so package stays clean ---
# Set SKIP_NETWORK_VERIFY=1 to skip xim --update index and install d2x (avoids hanging on slow/firewalled network).
TEST_DATA="$PROJECT_DIR/build/.release_verify_data.$$"
mkdir -p "$TEST_DATA"
trap cleanup_test_data EXIT

# For install test with XLINGS_DATA=$TEST_DATA, xvm must be in TEST_DATA/bin (real xvm from bin/)
mkdir -p "$TEST_DATA/bin" "$TEST_DATA/xvm"
for f in xvm xvm-shim; do
  [[ -f "$OUT_DIR/bin/$f" ]] && cp "$OUT_DIR/bin/$f" "$TEST_DATA/bin/" && chmod +x "$TEST_DATA/bin/$f"
done
# Bootstrap so xvm run finds tools when using TEST_DATA
cp "$OUT_DIR/data/xvm/versions.xvm.yaml" "$TEST_DATA/xvm/" 2>/dev/null || true
cp "$OUT_DIR/data/xvm/.workspace.xvm.yaml" "$TEST_DATA/xvm/" 2>/dev/null || true

export GIT_TERMINAL_PROMPT=0
# Prefer shorter git TCP timeout to avoid long hangs (git 2.3+)
export GIT_CONNECT_TIMEOUT="${GIT_CONNECT_TIMEOUT:-30}"

echo "[linux_release] Verify: bin/ and data/bin/ executables..."
if [[ ! -x "$OUT_DIR/bin/xlings" ]]; then
  echo "[linux_release] FAIL: bin/xlings is missing or not executable"
  exit 1
fi
for f in xvm xvm-shim; do
  if [[ ! -x "$OUT_DIR/bin/$f" ]]; then
    echo "[linux_release] FAIL: bin/$f is missing or not executable"
    exit 1
  fi
done
if [[ ! -x "$OUT_DIR/data/bin/xlings" ]] || [[ ! -x "$OUT_DIR/data/bin/xvm-shim" ]]; then
  echo "[linux_release] FAIL: data/bin shims (xlings, xvm-shim) missing or not executable"
  exit 1
fi
echo "  OK: bin/xlings, bin/xvm, bin/xvm-shim; data/bin shims"

# Network-dependent verification: optional and time-bounded to avoid indefinite hang
if [[ "${SKIP_NETWORK_VERIFY}" = "1" ]]; then
  echo "[linux_release] Skip: xim --update index and install d2x (SKIP_NETWORK_VERIFY=1)"
else
  echo "[linux_release] Verify: xim --update index (into temp data, timeout 300s)..."
  run_with_timeout() {
    local t="$1"; shift
    if command -v timeout &>/dev/null; then timeout "$t" "$@"; else "$@"; fi
  }
  if ! run_with_timeout 300 bash -c 'cd "$1" && export XLINGS_HOME="$1" XLINGS_DATA="$2" PATH="$2/bin:$1/bin:$1/tools/xmake/bin:$PATH" && xmake xim -P . -- --update index' _ "$OUT_DIR" "$TEST_DATA"; then
    echo "[linux_release] FAIL: xim --update index failed (network/timeout?). Set SKIP_NETWORK_VERIFY=1 to skip."
    exit 1
  fi

  echo "[linux_release] Verify: ./bin/xlings install d2x -y (into temp data, timeout 300s)..."
  if ! run_with_timeout 300 bash -c 'cd "$1" && export XLINGS_HOME="$1" XLINGS_DATA="$2" PATH="$2/bin:$1/bin:$1/tools/xmake/bin:$PATH" && ./bin/xlings install d2x -y' _ "$OUT_DIR" "$TEST_DATA"; then
    echo "[linux_release] FAIL: install d2x -y failed (network/timeout?). Set SKIP_NETWORK_VERIFY=1 to skip."
    exit 1
  fi

  XPKG_D2X="$TEST_DATA/xim/xpkgs/d2x"
  if [[ ! -d "$XPKG_D2X" ]]; then
    echo "[linux_release] FAIL: data/xim/xpkgs/d2x not found or not a directory"
    exit 1
  fi
  if ! compgen -G "$XPKG_D2X/*" > /dev/null 2>&1; then
    echo "[linux_release] FAIL: data/xim/xpkgs/d2x has no version directory"
    exit 1
  fi
  echo "  OK: data/xim/xpkgs/d2x exists with version dir(s)"

  D2X_BIN="$TEST_DATA/bin/d2x"
  if [[ ! -x "$D2X_BIN" ]] && [[ ! -f "$D2X_BIN" ]]; then
    echo "[linux_release] FAIL: data/bin/d2x not found (xvm shim or binary)"
    exit 1
  fi
  echo "  OK: data/bin/d2x exists"
fi

echo "[linux_release] Verify: data/bin/xlings uses package env (isolation) when env is polluted..."
CONFIG_OUT=$(cd "$OUT_DIR" && export XLINGS_HOME=/home/xlings XLINGS_DATA=/home/xlings/data && ./data/bin/xlings config 2>&1) || true
if echo "$CONFIG_OUT" | grep -qF "XLINGS_DATA: $OUT_DIR/data"; then
  echo "  OK: data/bin/xlings uses package path under polluted env"
else
  echo "[linux_release] FAIL: expected XLINGS_DATA to be package path ($OUT_DIR/data), got:"
  echo "$CONFIG_OUT" | grep -E "XLINGS_HOME|XLINGS_DATA" || echo "$CONFIG_OUT"
  exit 1
fi

# Remove test data (trap will run on exit too; explicit for clarity)
cleanup_test_data
trap - EXIT

echo ""
echo "[linux_release] All checks passed. Creating release archive..."

ARCHIVE="$PROJECT_DIR/build/${PKG_NAME}.tar.gz"
tar -czvf "$ARCHIVE" -C "$PROJECT_DIR/build" "$PKG_NAME"

echo ""
echo "[linux_release] Done."
echo "  Package:  $OUT_DIR"
echo "  Archive:  $ARCHIVE"
echo "  Unpack:   tar -xzf ${PKG_NAME}.tar.gz && cd $PKG_NAME && ./bin/xlings config  (or ./data/bin/xlings for package-isolated env)"
echo ""
