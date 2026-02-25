#!/bin/bash
# Build xvm/xvm-shim for release.
# Usage: ./release-build.sh [linux|windows|all]
#   linux  - build Linux (musl) only (used by xlings linux_release.sh)
#   windows - build Windows only
#   all (default) - build both and pack archives

set -e

CURRENT_DATE=$(date +"%Y%m%d%H%M%S")
MODE="${1:-all}"

LINUX_RELEASE_DIR=$(pwd)/target/x86_64-unknown-linux-musl/release
WINDOWS_RELEASE_DIR=$(pwd)/target/x86_64-pc-windows-gnu/release

rustup default stable-x86_64-unknown-linux-gnu

build_linux_musl_release() {
  # Isolate from xlings wrapper toolchains in user shell (PATH/CC/LD, etc.).
  local clean_path="$HOME/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
  local base_rustflags="${RUSTFLAGS:-}"
  local enforced_flags="-C target-feature=+crt-static -C link-self-contained=yes -C linker=rust-lld"
  if [[ -n "$base_rustflags" ]]; then
    base_rustflags="${base_rustflags} ${enforced_flags}"
  else
    base_rustflags="${enforced_flags}"
  fi

  rustup target add x86_64-unknown-linux-musl >/dev/null
  (
    unset CC CXX LD AR AS
    unset CFLAGS CXXFLAGS LDFLAGS
    unset LIBRARY_PATH LD_LIBRARY_PATH
    export PATH="$clean_path"
    export RUSTFLAGS="$base_rustflags"
    export CARGO_TARGET_X86_64_UNKNOWN_LINUX_MUSL_LINKER="rust-lld"
    cargo build --target x86_64-unknown-linux-musl --release
  )
}

case "$MODE" in
  linux)
    build_linux_musl_release
    echo "✅ Linux (musl) release built: $LINUX_RELEASE_DIR"
    ;;
  windows)
    cargo build --target x86_64-pc-windows-gnu --release
    echo "✅ Windows release built: $WINDOWS_RELEASE_DIR"
    ;;
  all)
    build_linux_musl_release
    cargo build --target x86_64-pc-windows-gnu --release
    cd "$LINUX_RELEASE_DIR"
    LINUX_ARCHIVE_NAME="xvm-0.0.5-linux-x86_64-${CURRENT_DATE}.tar.gz"
    tar -czf "$LINUX_ARCHIVE_NAME" xvm xvm-shim
    echo "✅ Linux archive created: $LINUX_ARCHIVE_NAME"
    cd "$WINDOWS_RELEASE_DIR"
    WINDOWS_ARCHIVE_NAME="xvm-0.0.5-windows-x86_64-${CURRENT_DATE}.zip"
    zip -r "$WINDOWS_ARCHIVE_NAME" xvm.exe xvm-shim.exe
    echo "✅ Windows archive created: $WINDOWS_ARCHIVE_NAME"
    ;;
  *)
    echo "Usage: $0 [linux|windows|all]" >&2
    exit 1
    ;;
esac