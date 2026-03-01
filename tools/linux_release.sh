#!/usr/bin/env bash
# Build a self-contained xlings package for Linux x86_64.
#
# Directory layout (v0.2.0+):
#   xlings-<ver>-linux-x86_64/
#   ├── bin/                   # real binaries (xlings)
#   ├── xim/                   # xim Lua source code
#   ├── data/                  # global shared data (XLINGS_DATA)
#   │   ├── xpkgs/             # package store (populated at runtime)
#   │   ├── runtimedir/        # download cache
#   │   ├── xim-index-repos/   # git-cloned index repos
#   │   └── local-indexrepo/   # user local index
#   ├── subos/
#   │   ├── current -> default # symlink to active subos
#   │   └── default/           # default sub-os (XLINGS_SUBOS = sysroot)
#   │       ├── bin/            # shim hardlinks (xlings, xim, etc.)
#   │       ├── lib/            # library symlinks (populated at runtime)
#   │       ├── usr/            # headers (populated at runtime)
#   │       └── generations/    # profile generations
#   ├── tools/xmake/bin/xmake  # bundled xmake
#   ├── bin/patchelf            # bundled patchelf (for elfpatch RPATH)
#   ├── config/i18n/           # i18n json
#   ├── xmake.lua              # package-root xim task
#   └── .xlings.json           # config
#
# Output:  build/xlings-<ver>-linux-x86_64.tar.gz
# Usage:   ./tools/linux_release.sh
# Env:     SKIP_NETWORK_VERIFY=1   skip network-dependent tests
#          SKIP_XMAKE_BUNDLE=1     skip downloading bundled xmake
#          SKIP_PATCHELF_BUNDLE=1  skip downloading bundled patchelf
#          GIT_CONNECT_TIMEOUT=N   git TCP timeout seconds (default 30)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION=$(sed -n 's/.*VERSION = "\([^"]*\)".*/\1/p' "$PROJECT_DIR/core/config.cppm" | head -1)
[[ -z "$VERSION" ]] && VERSION="0.2.0"

ARCH="x86_64"
PKG_NAME="xlings-${VERSION}-linux-${ARCH}"
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
# Ensure xmake is configured with a toolchain that supports `import std`.
MUSL_SDK_DEFAULT="${XLINGS_HOME:-$HOME/.xlings}/data/xpkgs/musl-gcc/15.1.0"
MUSL_SDK="${MUSL_SDK:-$MUSL_SDK_DEFAULT}"
if [[ -f "$MUSL_SDK/x86_64-linux-musl/include/c++/15.1.0/bits/std.cc" ]]; then
  # Some musl-gcc toolchains are built with a fixed interpreter path
  # baked into helper binaries (cc1, as, collect2).  Create the loader
  # symlink at that path so they can run during the build.
  LOADER_DIR="/home/xlings/.xlings_data/lib"
  if ! mkdir -p "$LOADER_DIR" 2>/dev/null; then
    sudo mkdir -p "$LOADER_DIR" && sudo chown "$(id -u):$(id -g)" "$LOADER_DIR"
  fi
  ln -sfn "$MUSL_SDK/x86_64-linux-musl/lib/libc.so" "$LOADER_DIR/ld-musl-x86_64.so.1"
  export CC="${CC:-x86_64-linux-musl-gcc}"
  export CXX="${CXX:-x86_64-linux-musl-g++}"
  export PATH="$MUSL_SDK/bin:$PATH"
  xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y \
    || fail "xmake configure with musl-gcc failed"
else
  info "Warning: musl std module file not found at $MUSL_SDK"
  info "Warning: fallback to existing xmake config/toolchain"
fi
xmake clean -q 2>/dev/null || true
xmake build xlings 2>&1 || fail "xmake build failed"

BIN_SRC="build/linux/${ARCH}/release/xlings"
[[ -f "$BIN_SRC" ]] || fail "C++ binary not found at $BIN_SRC"

# ── 2. Assemble package ─────────────────────────────────────────
info "Assembling $OUT_DIR ..."
rm -rf "$OUT_DIR"

mkdir -p "$OUT_DIR/bin"
mkdir -p "$OUT_DIR/subos/default/bin"
mkdir -p "$OUT_DIR/subos/default/lib"
mkdir -p "$OUT_DIR/subos/default/usr"
mkdir -p "$OUT_DIR/subos/default/generations"
ln -sfn default "$OUT_DIR/subos/current"
mkdir -p "$OUT_DIR/xim"
mkdir -p "$OUT_DIR/data/xpkgs"
mkdir -p "$OUT_DIR/data/runtimedir"
mkdir -p "$OUT_DIR/data/xim-index-repos"
mkdir -p "$OUT_DIR/data/local-indexrepo"

cp "$BIN_SRC"         "$OUT_DIR/bin/xlings"
chmod +x "$OUT_DIR/bin/"*

cp -R core/xim/* "$OUT_DIR/xim/" 2>/dev/null || true

# Bundled xmake (baseline tool in bin/)
XMAKE_READY=0
if [[ "${SKIP_XMAKE_BUNDLE:-}" != "1" ]]; then
  XMAKE_URL="https://github.com/xmake-io/xmake/releases/download/v3.0.7/xmake-bundle-v3.0.7.linux.x86_64"
  XMAKE_BIN="$OUT_DIR/bin/xmake"
  info "Downloading bundled xmake..."
  if curl -fSsL --connect-timeout 15 --max-time 120 -o "$XMAKE_BIN" "$XMAKE_URL"; then
    chmod +x "$XMAKE_BIN"
    info "Bundled xmake OK"
    XMAKE_READY=1
  else
    info "curl failed, trying wget..."
    if command -v wget &>/dev/null && wget -q --timeout=120 -O "$XMAKE_BIN" "$XMAKE_URL"; then
      chmod +x "$XMAKE_BIN"
      info "Bundled xmake OK (wget fallback)"
      XMAKE_READY=1
    else
      fail "bundled xmake download failed (curl + wget)"
    fi
  fi
fi

# Bundled patchelf (for elfpatch RPATH patching at package install time)
PATCHELF_READY=0
if [[ "${SKIP_PATCHELF_BUNDLE:-}" != "1" ]]; then
  PATCHELF_VER="0.18.0"
  PATCHELF_URL="https://github.com/NixOS/patchelf/releases/download/${PATCHELF_VER}/patchelf-${PATCHELF_VER}-x86_64.tar.gz"
  PATCHELF_TMP="$PROJECT_DIR/build/.patchelf_tmp_$$"
  info "Downloading bundled patchelf ${PATCHELF_VER}..."
  mkdir -p "$PATCHELF_TMP"
  if curl -fSsL --connect-timeout 15 --max-time 60 -o "$PATCHELF_TMP/patchelf.tar.gz" "$PATCHELF_URL"; then
    tar -xzf "$PATCHELF_TMP/patchelf.tar.gz" -C "$PATCHELF_TMP"
    cp "$PATCHELF_TMP/bin/patchelf" "$OUT_DIR/bin/patchelf"
    chmod +x "$OUT_DIR/bin/patchelf"
    info "Bundled patchelf OK"
    PATCHELF_READY=1
  else
    info "Warning: patchelf download failed — elfpatch will require system patchelf"
  fi
  rm -rf "$PATCHELF_TMP"
fi

# i18n
mkdir -p "$OUT_DIR/config/i18n"
cp -R config/i18n/*.json "$OUT_DIR/config/i18n/" 2>/dev/null || true

# shell profiles
mkdir -p "$OUT_DIR/config/shell"
cp -R config/shell/* "$OUT_DIR/config/shell/" 2>/dev/null || true

# xmake.lua (package root) — reuse core/xim/xmake.lua which auto-detects layout
cp core/xim/xmake.lua "$OUT_DIR/xmake.lua"

# .xlings.json
if command -v jq &>/dev/null && [[ -f config/xlings.json ]]; then
  jq --arg version "$VERSION" '. + {"version":$version,"activeSubos":"default","subos":{"default":{"dir":""}}}' \
    config/xlings.json > "$OUT_DIR/.xlings.json"
else
  cat > "$OUT_DIR/.xlings.json" << DOTJSON
{"activeSubos":"default","subos":{"default":{"dir":""}},"version":"$VERSION","need_update":false,"mirror":"CN","xim":{"mirrors":{"index-repo":{"GLOBAL":"https://github.com/d2learn/xim-pkgindex.git","CN":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"res-server":{"GLOBAL":"https://github.com/xlings-res","CN":"https://gitcode.com/xlings-res"}},"res-server":"https://gitcode.com/xlings-res","index-repo":"https://gitee.com/sunrisepeak/xim-pkgindex.git"},"repo":"https://gitee.com/sunrisepeak/xlings.git"}
DOTJSON
fi

# xim index-repos placeholder (global shared)
echo '{}' > "$OUT_DIR/data/xim-index-repos/xim-indexrepos.json"

# Shims are created at install time (not in package) to reduce archive size

info "Package assembled: $OUT_DIR"

# ── 4. Verification ─────────────────────────────────────────────
info "=== Verification ==="

# 4a. Check binaries exist (shims created at install time)
for f in bin/xlings; do
  [[ -x "$OUT_DIR/$f" ]] || fail "$f is missing or not executable"
done
if [[ "${SKIP_XMAKE_BUNDLE:-}" != "1" ]]; then
  [[ -x "$OUT_DIR/bin/xmake" ]] || fail "bin/xmake is missing or not executable"
fi
if [[ "${SKIP_PATCHELF_BUNDLE:-}" != "1" && "$PATCHELF_READY" == "1" ]]; then
  [[ -x "$OUT_DIR/bin/patchelf" ]] || fail "bin/patchelf is missing or not executable"
fi
info "OK: all binaries present and executable"

# 4b. Check directory structure (subos/default/bin empty; shims created at install)
for d in subos/default/bin subos/default/lib subos/default/usr subos/default/generations xim data/xpkgs config/i18n config/shell; do
  [[ -d "$OUT_DIR/$d" ]] || fail "directory $d missing"
done
[[ -L "$OUT_DIR/subos/current" ]] || fail "subos/current symlink missing"
[[ "$(readlink "$OUT_DIR/subos/current")" == "default" ]] || fail "subos/current does not point to default"
info "OK: directory structure valid (incl. subos/current symlink)"

# 4c. Check .xlings.json
[[ -f "$OUT_DIR/.xlings.json" ]] || fail ".xlings.json missing"
if command -v jq &>/dev/null; then
  AS=$(jq -r '.activeSubos' "$OUT_DIR/.xlings.json" 2>/dev/null)
  [[ "$AS" == "default" ]] || fail ".xlings.json activeSubos != 'default' (got '$AS')"
fi
info "OK: .xlings.json present and valid"

# 4d. Functional tests (self-contained detection)
info "Testing self-contained execution..."
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
echo "$CONFIG_OUT" | grep -q "XLINGS_SUBOS" || fail "config output missing XLINGS_SUBOS"
info "OK: xlings config prints correct paths"

SUBOS_OUT=$("$OUT_DIR/bin/xlings" subos list 2>&1) || fail "xlings subos list failed"
echo "$SUBOS_OUT" | grep -q "default" || fail "subos list missing 'default'"
info "OK: xlings subos list shows default"

GC_OUT=$("$OUT_DIR/bin/xlings" self clean --dry-run 2>&1) || fail "xlings self clean --dry-run failed"
info "OK: xlings self clean --dry-run works"

# 4e. Network-dependent tests
if [[ "${SKIP_NETWORK_VERIFY:-}" == "1" ]]; then
  info "Skip: network-dependent tests (SKIP_NETWORK_VERIFY=1)"
else
  export GIT_TERMINAL_PROMPT=0
  export GIT_CONNECT_TIMEOUT="${GIT_CONNECT_TIMEOUT:-30}"

  run_with_timeout() {
    local t="$1"; shift
    if command -v timeout &>/dev/null; then timeout "$t" "$@"; else "$@"; fi
  }

  info "Verify: xlings update (timeout 300s)..."
  if ! run_with_timeout 300 bash -c \
    'PATH="$1/subos/current/bin:$1/bin:/usr/local/bin:/usr/bin:/bin" "$1/bin/xlings" update' _ "$OUT_DIR"; then
    fail "xlings update failed (network?). Set SKIP_NETWORK_VERIFY=1 to skip."
  fi

  info "Verify: xlings install d2x@0.1.3 -y (timeout 300s)..."
  if ! run_with_timeout 300 bash -c \
    'PATH="$1/subos/current/bin:$1/bin:/usr/local/bin:/usr/bin:/bin" "$1/bin/xlings" install d2x@0.1.3 -y' _ "$OUT_DIR"; then
    fail "install d2x@0.1.3 failed. Set SKIP_NETWORK_VERIFY=1 to skip."
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
info "    ./bin/xlings self install"
info ""
info "  Or use without installing:"
info "    export PATH=\"\$(pwd)/subos/current/bin:\$(pwd)/bin:\$PATH\""
info "    xlings config"
info ""
