# AGENTS.md

## Cursor Cloud specific instructions

### Build overview

This project has **two build systems** (see `README.md` and `tools/linux_release.sh` for details):

| Component | Tool | Output |
|-----------|------|--------|
| C++23 core (`core/*.cppm`, `core/main.cpp`) | xmake + musl-gcc 15.1.0 | `build/linux/x86_64/release/xlings` |
| Rust xvm/xvm-shim (`core/xvm/`) | cargo | `core/xvm/target/release/xvm`, `xvm-shim` |

### Prerequisites

- **xlings** must be installed (`~/.xlings`) via the quick install script — it provides the bundled xmake v3.0.7 and can install musl-gcc.
- **musl-gcc@15.1.0** installed via `xlings install musl-gcc` — required for `import std` (C++23 modules).
- **Rust stable** toolchain.
- After installing musl-gcc, create the loader symlink:
  ```
  sudo mkdir -p /home/xlings/.xlings_data/lib
  sudo chown $(id -u):$(id -g) /home/xlings/.xlings_data/lib
  ln -sfn ~/.xlings/data/xpkgs/musl-gcc/15.1.0/x86_64-linux-musl/lib/libc.so /home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1
  ```

### Build commands

```bash
source ~/.bashrc
MUSL_SDK="$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:$PATH"

# Configure (only needed once or after clean)
xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y

# Build C++ binary
xmake build xlings

# Build Rust binaries
cd core/xvm && cargo build --release
```

### Testing

- **Rust**: `cargo test --manifest-path core/xvm/Cargo.toml`
- **Rust lint**: `cargo clippy --manifest-path core/xvm/Cargo.toml`
- **E2E tests**: `bash tests/e2e/linux_usability_test.sh` (requires a release tarball)
- **Bugfix regression**: `bash tests/e2e/bugfix_regression_test.sh`

### Key gotchas

- The root `xmake.lua` defines the C++ build target only. The `task("xim")` is defined in `core/xim/xmake.lua` — a *separate* xmake project used with `xmake xim -P <dir>`. Never pass the source root as `-P` for xim tasks.
- `source ~/.bashrc` is required after installing xlings to get `xmake`/`xvm` in PATH (they're shims in `~/.xlings/subos/current/bin/`).
- When `XLINGS_HOME` is set to the source tree, `xlings install` will still work because `find_xim_project_dir` falls through to `~/.xlings`.
