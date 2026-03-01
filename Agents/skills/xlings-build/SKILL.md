---
name: xlings-build
description: 构建 xlings 项目的三平台操作指南，覆盖 Linux、macOS、Windows 上的工具链准备、`xmake` 配置、release 脚本构建和构建后验证。Use this skill when Codex needs to build, package, or verify xlings locally or in CI, especially when aligning with `.github/workflows/*` and `tools/*_release.*`, or when setting up musl-gcc / LLVM / MSVC toolchains for this repository.
---

# XLINGS Build

## Overview

Use this skill as the build source of truth for xlings. Prefer repository CI and release scripts over ad-hoc command invention.

This repo has two build products:
- C++23 core via `xmake`
- Rust `xvm` / `xvm-shim` via `cargo`

Prefer the platform release scripts for final packaging:
- Linux: `tools/linux_release.sh`
- macOS: `tools/macos_release.sh`
- Windows: `tools/windows_release.ps1`

## Standard Workflow

Follow this order unless the user explicitly wants only one phase:

1. Install `xlings` first if the workflow depends on its bundled `xmake` or toolchain packages.
2. Install the platform toolchain.
3. Configure `xmake`.
4. Build the repo or run the release script.
5. Verify the built binary with `-h`.
6. Run a network smoke test with `xlings install d2x -y` when the environment allows network access.

## Linux Build

Use this path when building the static Linux binary with `musl-gcc@15.1.0`.

### 1) Install xlings

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
source ~/.bashrc
```

### 2) Install musl toolchain with xlings

```bash
xlings install musl-gcc@15.1.0 -y
xlings info musl-gcc
```

Current repo CI and release scripts use this SDK root:

```bash
MUSL_SDK="${XLINGS_HOME:-$HOME/.xlings}/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc
export CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:$PATH"
```

If the toolchain cannot run because of a missing musl loader path, create the loader symlink used by this repo:

```bash
sudo mkdir -p /home/xlings/.xlings_data/lib
sudo chown "$(id -u):$(id -g)" /home/xlings/.xlings_data/lib
ln -sfn "$MUSL_SDK/x86_64-linux-musl/lib/libc.so" /home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1
```

### 3) Configure xmake

Always configure with the musl SDK and keep `-y` so xmake can pull missing helper packages automatically.

```bash
xmake f -c -p linux -m release \
  --sdk="$MUSL_SDK" \
  --cross=x86_64-linux-musl- \
  --cc="$CC" \
  --cxx="$CXX" \
  -y
```

### 4) Build

Quick local build:

```bash
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

Release-package build aligned with CI:

```bash
chmod +x ./tools/linux_release.sh
SKIP_NETWORK_VERIFY=1 ./tools/linux_release.sh
```

### 5) Verify

```bash
./build/linux/x86_64/release/xlings -h
./build/linux/x86_64/release/xlings install d2x -y
```

If using the packaged release tree, prefer `./bin/xlings -h` inside the assembled output directory.

## macOS Build

The checked-in CI uses Homebrew `llvm@20` plus `xmake`. If the user insists on an xlings-first workflow, install xlings first for consistency, but the canonical repo reference is still the CI setup.

### 1) Install xlings

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
source ~/.bashrc
```

### 2) Install toolchain

CI-aligned command:

```bash
brew install xmake llvm@20
```

Then configure with LLVM:

```bash
xmake f -p macosx -m release --toolchain=llvm --sdk=/opt/homebrew/opt/llvm@20 -y
```

### 3) Build

Quick local build:

```bash
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

Release-package build aligned with CI:

```bash
chmod +x ./tools/macos_release.sh
SKIP_NETWORK_VERIFY=1 ./tools/macos_release.sh
```

### 4) Verify

```bash
./build/macosx/arm64/release/xlings -h
./build/macosx/arm64/release/xlings install d2x -y
```

Also keep the release-script expectation in mind: the final binary should not retain LLVM dylib runtime dependency.

## Windows Build

Use the system MSVC environment. This repo's Windows CI does not install a compiler with xlings; it configures `xmake` for the default MSVC toolchain.

### 1) Prepare tools

Install `xmake` and ensure Rust is available. In CI, this is handled by `xmake-io/github-action-setup-xmake@v1`.

Optional: install xlings as a runtime/package-manager smoke-test target after build.

### 2) Configure xmake

```powershell
xmake f -p windows -m release -y
```

### 3) Build

Quick local build:

```powershell
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

Release-package build aligned with CI:

```powershell
pwsh ./tools/windows_release.ps1
```

### 4) Verify

```powershell
.\build\windows\x64\release\xlings.exe -h
.\build\windows\x64\release\xlings.exe install d2x -y
```

## Verification Rules

After any successful build, run at least:

```bash
xlings -h
xlings config
xvm --version
```

When testing package-manager behavior, also run:

```bash
xlings install d2x -y
```

If network access is not available, say that the smoke test was skipped rather than claiming success.

## Practical Notes

- On Linux, `musl-gcc@15.1.0` is required for `import std` in the C++23 build.
- The root `xmake.lua` only handles the C++ target. `xim` tasks live under a separate xmake project.
- Prefer release scripts for packaging because they also assemble `xim`, `xvm`, config files, bundled `xmake`, and post-build verification.
- When the user asks for exact CI parity, read the workflow file first and mirror it exactly rather than relying on memory.

## References

- Read [references/platforms.md](references/platforms.md) for platform-by-platform commands and file outputs.
- Read [references/links.md](references/links.md) for workflow, release script, and project links.
