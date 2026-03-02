#!/usr/bin/env bash
set -euo pipefail

# Ensure musl-gcc helper binaries and musl-linked outputs can run on the host.
# Why this is needed:
# 1) Some helper binaries in the musl-gcc toolchain (cc1/as/collect2) are built
#    with a fixed interpreter path: /home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1
# 2) musl-linked binaries typically use /lib/ld-musl-x86_64.so.1 as interpreter.
# 3) musl runtime libs (libstdc++.so.6/libgcc_s.so.1) must be visible in musl
#    loader search paths to avoid runtime linker errors.

MUSL_SDK="${1:-${MUSL_SDK:-}}"
if [[ -z "$MUSL_SDK" ]]; then
  echo "[musl-setup] FAIL: MUSL_SDK is required" >&2
  exit 1
fi

LIBC_SO="$MUSL_SDK/x86_64-linux-musl/lib/libc.so"
[[ -f "$LIBC_SO" ]] || { echo "[musl-setup] FAIL: missing libc.so at $LIBC_SO" >&2; exit 1; }

LOADER_DIR="/home/xlings/.xlings_data/lib"
sudo mkdir -p "$LOADER_DIR"
sudo ln -sfn "$LIBC_SO" "$LOADER_DIR/ld-musl-x86_64.so.1"
sudo ln -sfn "$LIBC_SO" /lib/ld-musl-x86_64.so.1

echo "$MUSL_SDK/x86_64-linux-musl/lib" | sudo tee /etc/ld-musl-x86_64.path > /dev/null
sudo ln -sfn "$MUSL_SDK/x86_64-linux-musl/lib/libstdc++.so.6" /lib/libstdc++.so.6
sudo ln -sfn "$MUSL_SDK/x86_64-linux-musl/lib/libgcc_s.so.1" /lib/libgcc_s.so.1

echo "[musl-setup] runtime loader and libs configured for $MUSL_SDK"
