#!/usr/bin/env bash
# Build a self-contained xlings package for Linux, run verification with data in a temp dir,
# then pack the release directory into xlings-<version>-linux-x86_64.tar.gz.
# Output dir: build/xlings-<version>-linux-x86_64
# Usage: ./tools/linux_release.sh

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

# C++ binary as bin/.xlings.real (invoked by bin/xlings wrapper)
BIN_SRC="build/linux/x86_64/release/xlings"
if [[ ! -f "$BIN_SRC" ]]; then
  echo "[linux_release] Error: C++ binary not found at $BIN_SRC. Run xmake build first."
  exit 1
fi
cp "$BIN_SRC" "$OUT_DIR/bin/.xlings.real"
chmod +x "$OUT_DIR/bin/.xlings.real"

# bin/xlings wrapper: set XLINGS_HOME to package root; use bundled xmake if system has none
cat > "$OUT_DIR/bin/xlings" << 'WRAPPER'
#!/usr/bin/env bash
set -e
SELF="$(realpath "$0" 2>/dev/null || true)"
[[ -z "$SELF" ]] && SELF="$0"
XLINGS_ROOT="$(cd "$(dirname "$SELF")/.." && pwd)"
export XLINGS_HOME="$XLINGS_ROOT"
export XLINGS_DATA="${XLINGS_DATA:-$XLINGS_ROOT/data}"
export PATH="$XLINGS_DATA/bin:${PATH}"
# If system has no xmake, prefer bundled one (xim and C++ invoke xmake)
if ! command -v xmake &>/dev/null; then
  BUNDLED_XMAKE="$XLINGS_ROOT/tools/xmake/bin/xmake"
  if [[ -x "$BUNDLED_XMAKE" ]]; then
    export PATH="$XLINGS_ROOT/tools/xmake/bin:${PATH}"
  fi
fi
exec "$(dirname "$SELF")/.xlings.real" "$@"
WRAPPER
chmod +x "$OUT_DIR/bin/xlings"

# xvm and xvm-shim into data (from release-build.sh musl output)
XVM_RELEASE_DIR="core/xvm/target/x86_64-unknown-linux-musl/release"
if [[ -f "$XVM_RELEASE_DIR/xvm" ]]; then
  cp "$XVM_RELEASE_DIR/xvm" "$OUT_DIR/data/bin/"
  chmod +x "$OUT_DIR/data/bin/xvm"
fi
if [[ -f "$XVM_RELEASE_DIR/xvm-shim" ]]; then
  cp "$XVM_RELEASE_DIR/xvm-shim" "$OUT_DIR/data/bin/xvm-shim"
  chmod +x "$OUT_DIR/data/bin/xvm-shim"
fi

# xim tree (Lua)
cp -R core/xim "$OUT_DIR/"

# Bundled xmake (so package works without system xmake)
XMAKE_URL="https://github.com/xmake-io/xmake/releases/download/v3.0.7/xmake-bundle-v3.0.7.linux.x86_64"
XMAKE_BUNDLE_DIR="$OUT_DIR/tools/xmake"
XMAKE_DL="$OUT_DIR/tools/xmake.bin"
mkdir -p "$OUT_DIR/tools"
echo "[linux_release] Downloading bundled xmake..."
if curl -fsSL -o "$XMAKE_DL" "$XMAKE_URL"; then
  _xmake_extracted=
  # Try tar (auto-detect gzip/xz) - extracts into current dir
  if (cd "$OUT_DIR/tools" && tar -xf xmake.bin 2>/dev/null); then
    _xmake_extracted=1
  # Try makeself-style: script -d dest
  elif (cd "$OUT_DIR/tools" && sh ./xmake.bin -d "$XMAKE_BUNDLE_DIR" -y 2>/dev/null); then
    _xmake_extracted=1
  fi
  rm -f "$XMAKE_DL"
  if [[ -n "$_xmake_extracted" ]]; then
    # Tar extracts into OUT_DIR/tools; find the single top-level dir (e.g. xmake-v3.0.7) and move to tools/xmake
    if [[ -x "$XMAKE_BUNDLE_DIR/bin/xmake" ]]; then
      :
    elif compgen -G "$XMAKE_BUNDLE_DIR/xmake-*/bin/xmake" >/dev/null 2>&1; then
      _sub=$(ls -d "$XMAKE_BUNDLE_DIR"/xmake-* 2>/dev/null | head -1)
      _tmp="$OUT_DIR/tools/xmake.tmp"
      rm -rf "$_tmp"
      mv "$_sub" "$_tmp"
      rm -rf "$XMAKE_BUNDLE_DIR"
      mv "$_tmp" "$XMAKE_BUNDLE_DIR"
    elif compgen -G "$OUT_DIR/tools/xmake-*/bin/xmake" >/dev/null 2>&1; then
      _sub=$(ls -d "$OUT_DIR/tools"/xmake-* 2>/dev/null | head -1)
      rm -rf "$XMAKE_BUNDLE_DIR"
      mv "$_sub" "$XMAKE_BUNDLE_DIR"
    fi
  fi
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
TEST_DATA="$PROJECT_DIR/build/.release_verify_data.$$"
mkdir -p "$TEST_DATA"
trap cleanup_test_data EXIT

# So xim finds the package's xvm (not system xvm) when XLINGS_DATA=$TEST_DATA
mkdir -p "$TEST_DATA/bin"
for f in xvm xvm-shim; do
  [[ -f "$OUT_DIR/data/bin/$f" ]] && cp "$OUT_DIR/data/bin/$f" "$TEST_DATA/bin/" && chmod +x "$TEST_DATA/bin/$f"
done

export GIT_TERMINAL_PROMPT=0

echo "[linux_release] Verify: bin directory executables..."
if [[ ! -x "$OUT_DIR/bin/xlings" ]]; then
  echo "[linux_release] FAIL: bin/xlings is missing or not executable"
  exit 1
fi
if [[ ! -x "$OUT_DIR/bin/.xlings.real" ]]; then
  echo "[linux_release] FAIL: bin/.xlings.real is missing or not executable"
  exit 1
fi
echo "  OK: bin/xlings, bin/.xlings.real"

echo "[linux_release] Verify: xim --update index (into temp data)..."
(cd "$OUT_DIR" && export XLINGS_HOME="$OUT_DIR" XLINGS_DATA="$TEST_DATA" && xmake xim -P . -- --update index) || {
  echo "[linux_release] FAIL: xim --update index failed (network?)"
  exit 1
}

echo "[linux_release] Verify: ./bin/xlings install d2x -y (into temp data)..."
(cd "$OUT_DIR" && export XLINGS_DATA="$TEST_DATA" && ./bin/xlings install d2x -y) || {
  echo "[linux_release] FAIL: install d2x -y failed"
  exit 1
}

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
echo "  Unpack:   tar -xzf ${PKG_NAME}.tar.gz && cd $PKG_NAME && ./bin/xlings config"
echo ""
