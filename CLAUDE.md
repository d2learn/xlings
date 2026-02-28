# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**xlings** is a cross-platform package manager and version manager — "everything can be a package." It has three independent build components:

| Component | Language | Build Tool | Output |
|-----------|----------|------------|--------|
| C++23 core (`core/*.cppm`, `core/main.cpp`) | C++23 modules | xmake + musl-gcc 15.1.0 | `build/linux/x86_64/release/xlings` |
| xvm (`core/xvm/src/`) | Rust | cargo | `core/xvm/target/release/xvm` |
| xvm-shim (`core/xvm/shim/`) | Rust | cargo | `core/xvm/target/release/xvm-shim` |
| xim (`core/xim/`) | Lua | xmake task | run via `xmake xim -P <dir>` |

## Prerequisites

- **xlings** installed at `~/.xlings` — provides bundled xmake v3.0.7 and can install musl-gcc
- **musl-gcc@15.1.0** via `xlings install musl-gcc` — required for C++23 `import std`
- **Rust stable** toolchain
- After installing musl-gcc, create the loader symlink:
  ```bash
  sudo mkdir -p /home/xlings/.xlings_data/lib
  sudo chown $(id -u):$(id -g) /home/xlings/.xlings_data/lib
  ln -sfn ~/.xlings/data/xpkgs/musl-gcc/15.1.0/x86_64-linux-musl/lib/libc.so /home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1
  ```
- `source ~/.bashrc` after installing xlings to get `xmake`/`xvm` in PATH (they are shims in `~/.xlings/subos/current/bin/`)

## Build Commands

```bash
source ~/.bashrc
MUSL_SDK="$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:$PATH"

# Configure (only needed once or after clean)
xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y

# Build C++ binary
xmake build xlings

# Build Rust binaries (xvm + xvm-shim)
cd core/xvm && cargo build --release
```

## Testing

```bash
# Rust unit tests
cargo test --manifest-path core/xvm/Cargo.toml

# Rust lint
cargo clippy --manifest-path core/xvm/Cargo.toml

# E2E Linux usability test (requires a release tarball in build/)
bash tests/e2e/linux_usability_test.sh

# E2E with a specific archive
bash tests/e2e/linux_usability_test.sh build/xlings-0.3.2-linux-x86_64.tar.gz

# Skip network-dependent tests
SKIP_NETWORK_TESTS=1 bash tests/e2e/linux_usability_test.sh

# Bugfix regression tests
bash tests/e2e/bugfix_regression_test.sh

# macOS E2E
bash tests/e2e/macos_usability_test.sh

# Windows E2E (PowerShell)
powershell -ExecutionPolicy Bypass -File tests/e2e/windows_usability_test.ps1
```

## Release / Local Testing

```bash
# Build + assemble release tarball, then verify with `install d2x -y`
./tools/linux_release.sh              # outputs to build/xlings/
./tools/linux_release.sh /path/out   # outputs to custom dir

# Run directly from assembled package (no env vars needed)
cd build/xlings
./bin/xlings config
./bin/xlings install d2x -y
```

## Architecture

### C++23 Core (`core/`)

Entry point: `core/main.cpp` — initializes paths, sets env vars (`XLINGS_HOME`, `XLINGS_DATA`, `XLINGS_SUBOS`), and dispatches to `xlings::cmdprocessor`.

C++23 modules (`*.cppm`):
- `cmdprocessor` — CLI argument parsing and command dispatch; delegates to xim/xvm via shell exec
- `config` — singleton `Config::paths()` for all path resolution; reads `$XLINGS_HOME/.xlings.json`
- `subos` — sub-OS environment management (create/use/list/remove isolated environments under `$XLINGS_HOME/subos/<name>/`)
- `xself` — self-management commands (init/update/config/clean/migrate/install)
- `platform` — platform abstraction (exec, env vars, file I/O, executable path)
- `profile`, `log`, `i18n`, `utils` — supporting utilities

**Path resolution priority (XLINGS_HOME):**
1. `XLINGS_HOME` env var
2. Auto-detect: if `xim/` exists next to the binary's parent → self-contained release layout
3. Default: `$HOME/.xlings`

**xim invocation pattern:** `xmake xim -P "<projectDir>" -- <args>` — the `projectDir` must contain both `xim/` and `xmake.lua`. The source tree layout (`core/xim/`) is NOT a valid `-P` target; the C++ code falls back to `~/.xlings` in that case.

### xvm (`core/xvm/`)

Rust binary (`xvm`) + shim binary (`xvm-shim`). Library code lives in `xvmlib/`:
- `versiondb.rs` — YAML-based version database
- `workspace.rs` — directory-scoped workspace isolation
- `shims.rs` — shim management for version-switching

The shim intercepts command invocations and resolves the active version based on workspace context.

### xim (`core/xim/`)

Lua package manager running inside xmake's Lua runtime. Invoked as an xmake task (`task("xim")` in `core/xim/xmake.lua`). Key files:
- `xim.lua` — entry point, argument parsing
- `CmdProcessor.lua` — command dispatch
- `libxpkg/` — XPackage script API for package authors
- `index/` — package index management
- `pm/` — platform-specific package manager wrappers

### Sub-OS Concept

Each sub-OS is an isolated environment under `$XLINGS_HOME/subos/<name>/` with `bin/`, `lib/`, `xvm/`, `usr/`, `generations/` subdirs. `subos/current` is a symlink to the active one. Tools installed into a sub-OS get xvm-shim entries in its `bin/` for version-switching.

### Configuration

Single config file: `$XLINGS_HOME/.xlings.json` — stores `activeSubos`, `subos` entries, `mirror`, `lang`, and data paths. Project-level deps use `<project-root>/.xlings.json` with a `"deps"` array.

## Key Gotchas

- The root `xmake.lua` only defines the C++ `xlings` build target. The `task("xim")` is in `core/xim/xmake.lua` — a **separate** xmake project. Never pass the source root as `-P` for xim tasks.
- On Linux, the release binary is fully statically linked (musl). macOS links libc++ statically but uses system libSystem.
- `source ~/.bashrc` is required after xlings install — xmake and xvm are shims and won't be in PATH otherwise.
- Sub-OS `new` requires `config/xvm/` to exist in the package (provides xvm config templates); if missing, it fails with "package incomplete."
- Shims are created at install time (`xlings self install`), not bundled in the release archive.
