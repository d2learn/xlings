# AGENTS.md

## Cursor Cloud specific instructions

### Project Overview
Xlings is a cross-platform package manager with three core components:
- **C++23 main binary** (`core/*.cppm`, `core/main.cpp`) — CLI entry point, built with xmake + musl-gcc 15.1
- **xvm** (`core/xvm/`) — Rust version manager (multi-version switching via shims)
- **xim** (`core/xim/`) — Lua package manager (runs on xmake's Lua runtime)

### Required Toolchain
- **xmake v3.0.7** — installed at `/opt/xmake/xmake`
- **musl-gcc 15.1.0** — installed at `$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0` via `xlings install musl-gcc@15.1.0 -y`
- **Rust stable** — system Rust with `x86_64-unknown-linux-musl` target added
- A pre-built xlings release is installed at `$HOME/.xlings` (used for bootstrapping musl-gcc)

### Key Gotchas
- The `core/xvm/release-build.sh` script hard-codes `$HOME/.cargo/bin` in its internal PATH. If Rust is installed at `/usr/local/cargo/bin`, you need a symlink: `ln -sfn /usr/local/cargo/bin $HOME/.cargo/bin`.
- The musl-gcc toolchain has a baked-in loader path. The build requires a symlink at `/home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1` pointing to the musl libc.so. The release script creates this automatically.
- When running `linux_release.sh`, ensure PATH includes both xmake and cargo/rustup locations.
- Use `SKIP_NETWORK_VERIFY=1` with `linux_release.sh` and `SKIP_NETWORK_TESTS=1` with E2E tests when network access to package mirrors is unreliable.

### Build Commands
See `docs/architecture.md` for full details. Quick reference:

```bash
# Set environment
export MUSL_SDK="$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:/opt/xmake:$HOME/.cargo/bin:$PATH"

# Configure xmake
xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y

# Build C++ binary
xmake build xlings

# Run Rust tests
cargo test --manifest-path core/xvm/Cargo.toml --all-targets

# Full release build (skipping network tests)
SKIP_NETWORK_VERIFY=1 ./tools/linux_release.sh

# E2E usability tests
SKIP_NETWORK_TESTS=1 bash tests/e2e/linux_usability_test.sh build/xlings-*-linux-x86_64.tar.gz
```

### Lint
- **Rust**: `cargo clippy --manifest-path core/xvm/Cargo.toml` and `cargo fmt --manifest-path core/xvm/Cargo.toml --check`
- **C++/Lua/Shell**: No dedicated linter configured; rely on build success and E2E tests.

### PR Review
A PR review skill is available at `.cursor/skills/pr-review.md`. The GitHub MCP server is configured in `.cursor/mcp.json` (requires `GITHUB_TOKEN` environment variable or secret).

### MCP Tools
- **GitHub MCP Server** (`github-mcp-server` v0.31.0) is installed at `/usr/local/bin/github-mcp-server` and configured in `.cursor/mcp.json`. It provides PR review, issue management, and repository operations via MCP protocol.
