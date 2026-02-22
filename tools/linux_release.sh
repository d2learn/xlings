#!/usr/bin/env bash
# Build a runnable xlings directory for Linux, then verify with install d2x -y.
# Output: build/xlings/ (or $1 if given)
# Usage: ./tools/linux_release.sh [output_dir]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${1:-$PROJECT_DIR/build/xlings}"

cd "$PROJECT_DIR"

echo "[linux_release] Building C++ binary..."
if ! xmake build -q 2>/dev/null; then
  xmake clean
  xmake build 2>/dev/null || true
fi

echo "[linux_release] Building xvm (Rust)..."
(cd core/xvm && cargo clean && cargo  build --release -q 2>/dev/null || cargo build --release)

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

# bin/xlings wrapper: set XLINGS_HOME to package root so unpack-and-run works
cat > "$OUT_DIR/bin/xlings" << 'WRAPPER'
#!/usr/bin/env bash
set -e
SELF="$(realpath "$0" 2>/dev/null || true)"
[[ -z "$SELF" ]] && SELF="$0"
XLINGS_ROOT="$(cd "$(dirname "$SELF")/.." && pwd)"
export XLINGS_HOME="$XLINGS_ROOT"
export XLINGS_DATA="${XLINGS_DATA:-$XLINGS_ROOT/data}"
export PATH="$XLINGS_DATA/bin:${PATH}"
exec "$(dirname "$SELF")/.xlings.real" "$@"
WRAPPER
chmod +x "$OUT_DIR/bin/xlings"

# xvm and xvm-shim into data (xvm expects xvm-shim in data dir for creating shims)
if [[ -f "core/xvm/target/release/xvm" ]]; then
  cp core/xvm/target/release/xvm "$OUT_DIR/data/bin/"
  chmod +x "$OUT_DIR/data/bin/xvm"
fi
if [[ -f "core/xvm/target/release/xvm-shim" ]]; then
  cp core/xvm/target/release/xvm-shim "$OUT_DIR/data/bin/xvm-shim"
  chmod +x "$OUT_DIR/data/bin/xvm-shim"
fi

# xim tree (Lua; third-party, not from root xmake.lua)
cp -R core/xim "$OUT_DIR/"

# data/xim: xim will clone index repo (xim-pkgindex) and sub-repos via git on first run (xim --update index)
mkdir -p "$OUT_DIR/data/xim"
# Empty sub-indexrepos list so xim does not warn "file not found" (xim fills this on --update index)
mkdir -p "$OUT_DIR/data/xim/xim-index-repos"
echo '{}' > "$OUT_DIR/data/xim/xim-index-repos/xim-indexrepos.json"

# config/ for i18n only; single config is .xlings.json (merged from former config/xlings.json)
mkdir -p "$OUT_DIR/config/i18n"
cp -R config/i18n/*.json "$OUT_DIR/config/i18n/" 2>/dev/null || true

# xmake.lua at package root: xim (packages) + xself (xlings self management, independent of xim)
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

# Single config file: .xlings.json (data path + mirror/xim/repo; data dir relative to package)
if command -v jq &>/dev/null; then
  jq -s '.[0] + .[1]' config/xlings.json <(echo '{"data":"data"}') > "$OUT_DIR/.xlings.json"
else
  # fallback: full default merged config
  cat > "$OUT_DIR/.xlings.json" << 'DOTJSON'
{"data":"data","version":"0.0.5","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
DOTJSON
fi

echo "[linux_release] Package done: $OUT_DIR"
echo ""

# --- Verification: bin executables ---
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

# --- Verification: xim pulls index via git into package data dir, then install d2x -y ---
echo "[linux_release] Verify: xim --update index (git pull index repo into package)..."
(cd "$OUT_DIR" && export XLINGS_HOME="$OUT_DIR" XLINGS_DATA="$OUT_DIR/data" && xmake xim -P . -- --update index) || {
  echo "[linux_release] FAIL: xim --update index failed (network?)"
  exit 1
}
echo "[linux_release] Verify: ./bin/xlings install d2x -y ..."
(cd "$OUT_DIR" && ./bin/xlings install d2x -y) || {
  echo "[linux_release] FAIL: install d2x -y failed"
  exit 1
}

# --- Verification: data/xim/xpkgs/... has d2x ---
XPKG_D2X="$OUT_DIR/data/xim/xpkgs/d2x"
if [[ ! -d "$XPKG_D2X" ]]; then
  echo "[linux_release] FAIL: data/xim/xpkgs/d2x not found or not a directory"
  exit 1
fi
# expect at least one version dir (e.g. 0.1.2)
if ! compgen -G "$XPKG_D2X/*" > /dev/null 2>&1; then
  echo "[linux_release] FAIL: data/xim/xpkgs/d2x has no version directory"
  exit 1
fi
echo "  OK: data/xim/xpkgs/d2x exists with version dir(s)"

# --- Verification: data/bin has d2x executable ---
D2X_BIN="$OUT_DIR/data/bin/d2x"
if [[ ! -x "$D2X_BIN" ]] && [[ ! -f "$D2X_BIN" ]]; then
  echo "[linux_release] FAIL: data/bin/d2x not found (xvm shim or binary)"
  exit 1
fi
echo "  OK: data/bin/d2x exists"

echo ""
echo "[linux_release] All checks passed."
echo "  cd $OUT_DIR"
echo "  ./bin/xlings config"
echo "  ./bin/xlings install d2x -y   # already verified"
echo ""
