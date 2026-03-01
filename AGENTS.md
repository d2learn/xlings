# AGENTS.md

## Project Overview

`xlings` is a cross-platform tool/package manager project with:
- a C++23 core application in `core/`
- a Rust-based `xvm` / `xvm-shim` runtime in `core/xvm/`
- a Lua-based `xim` package-management layer in `core/xim/`
- release assembly scripts in `tools/`
- CI workflows in `.github/workflows/`

Treat this repository as a multi-language, multi-platform build and packaging project. Do not assume Linux-only workflows unless the task is explicitly Linux-specific.

## Repository Areas

Important directories:
- `core/`: C++23 application modules and entrypoint
- `core/xvm/`: Rust binaries and tests
- `core/xim/`: Lua/xmake package-management logic
- `config/`: shipped config, i18n, shell, and xvm templates
- `tools/`: platform release scripts and install helpers
- `tests/e2e/`: end-to-end and regression tests
- `docs/`: design notes, migration docs, and implementation plans
- `Agents/skills/`: project-local Codex skills for this repo

## Build Systems

This repo has two build paths:

| Component | Tool | Typical output |
|-----------|------|----------------|
| C++23 core (`core/*.cppm`, `core/main.cpp`) | `xmake` | `build/<platform>/<arch>/release/xlings` |
| Rust xvm/xvm-shim (`core/xvm/`) | `cargo` | `core/xvm/target/release/{xvm,xvm-shim}` or platform target dir |

For packaged deliverables, prefer the checked-in release scripts:
- Linux: `tools/linux_release.sh`
- macOS: `tools/macos_release.sh`
- Windows: `tools/windows_release.ps1`

## Platform Guidance

### Linux

Canonical repo flow:
- install `xlings` first via quick install
- use `xlings install musl-gcc@15.1.0 -y`
- configure `xmake` with the musl SDK
- build C++ via `xmake`
- build Rust via `cargo`
- use `tools/linux_release.sh` for package assembly

Common setup:

```bash
source ~/.bashrc
MUSL_SDK="${XLINGS_HOME:-$HOME/.xlings}/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc
export CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:$PATH"

xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

If musl helper binaries fail due to missing loader path, create the loader symlink used by CI/release tooling:

```bash
sudo mkdir -p /home/xlings/.xlings_data/lib
sudo chown "$(id -u):$(id -g)" /home/xlings/.xlings_data/lib
ln -sfn "$MUSL_SDK/x86_64-linux-musl/lib/libc.so" /home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1
```

### macOS

Canonical repo flow:
- install `xmake` and `llvm@20` with Homebrew
- configure `xmake` with `--toolchain=llvm`
- build C++ via `xmake`
- build Rust via `cargo`
- use `tools/macos_release.sh` for package assembly

Common setup:

```bash
brew install xmake llvm@20
xmake f -p macosx -m release --toolchain=llvm --sdk=/opt/homebrew/opt/llvm@20 -y
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

### Windows

Canonical repo flow:
- use the default MSVC-capable environment
- configure `xmake` for Windows
- build C++ via `xmake`
- build Rust via `cargo`
- use `tools/windows_release.ps1` for package assembly

Common setup:

```powershell
xmake f -p windows -m release -y
xmake build xlings -y
cargo build --manifest-path core/xvm/Cargo.toml --release
```

## Testing

Default validation targets:
- Rust tests: `cargo test --manifest-path core/xvm/Cargo.toml --all-targets`
- Rust lint: `cargo clippy --manifest-path core/xvm/Cargo.toml`
- Linux regression: `bash tests/e2e/bugfix_regression_test.sh`
- Linux usability: `bash tests/e2e/linux_usability_test.sh`
- macOS usability: `bash tests/e2e/macos_usability_test.sh build/release.tar.gz`
- Windows usability: `pwsh tests/e2e/windows_usability_test.ps1 -ArchivePath build\\release.zip`

After any successful build, prefer these smoke checks:

```bash
xlings -h
xlings config
xvm --version
xlings install d2x -y
```

If network access is unavailable, explicitly report that the `d2x` install smoke test was skipped.

## Key Gotchas

- The root `xmake.lua` defines the C++ build target only.
- The `xim` task is defined in `core/xim/xmake.lua`, which is a separate xmake project used with `xmake xim -P <dir>`.
- Never pass the source root as `-P` for `xim` tasks unless the layout explicitly matches the package root expectations.
- After installing `xlings`, `source ~/.bashrc` may be required so `~/.xlings/subos/current/bin` is on `PATH`.
- When `XLINGS_HOME` points at the source tree, `xlings install` can still fall through to the installed `~/.xlings` layout.
- Linux release builds are intentionally aligned to `musl-gcc@15.1.0` for static output.
- Prefer CI workflow files over memory when reproducing exact platform behavior.

## Skills

This repo includes project-local skills under `Agents/skills/`.

Use these when they match the task:
- `xlings-quickstart`: installation, basic usage, namespace/index flows, and important project links
  - `Agents/skills/xlings-quickstart/SKILL.md`
- `xlings-build`: Linux/macOS/Windows build, toolchain prep, release scripts, and post-build verification
  - `Agents/skills/xlings-build/SKILL.md`

When a task matches one of these skills, open the corresponding `SKILL.md` first and follow it before inventing new workflow steps.

## Preferred Sources Of Truth

When instructions disagree, use this priority:
1. current CI workflow files in `.github/workflows/`
2. current release scripts in `tools/`
3. checked-in project skills in `Agents/skills/`
4. repo docs in `docs/`
5. older assumptions from prior conversations
