#!/bin/bash

CURRENT_DATE=$(date +"%Y%m%d%H%M%S")

LINUX_RELEASE_DIR=$(pwd)/target/release
WINDOWS_RELEASE_DIR=$(pwd)/target/x86_64-pc-windows-gnu/release

cargo build --release
cargo build --target x86_64-pc-windows-gnu --release

cd "$LINUX_RELEASE_DIR"
LINUX_ARCHIVE_NAME="xvm-dev-x86_64-linux-gnu-${CURRENT_DATE}.tar.gz"
tar -czf "$LINUX_ARCHIVE_NAME" xvm xvm-shim
echo "✅ Linux archive created: $LINUX_ARCHIVE_NAME"

cd "$WINDOWS_RELEASE_DIR"
WINDOWS_ARCHIVE_NAME="xvm-dev-x86_64-pc-windows-gnu-${CURRENT_DATE}.zip"
zip -r "$WINDOWS_ARCHIVE_NAME" xvm.exe xvm-shim.exe
echo "✅ Windows archive created: $WINDOWS_ARCHIVE_NAME"