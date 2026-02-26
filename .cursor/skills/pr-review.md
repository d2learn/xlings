# PR Review Skill

## When to Use
Use this skill when reviewing pull requests for the xlings project, including code changes to C++23 modules, Rust (xvm), Lua (xim), or shell scripts.

## PR Review Workflow

### 1. Fetch PR Details
Use `gh` CLI to get PR information:
```bash
gh pr view <PR_NUMBER> --json title,body,files,additions,deletions,reviewDecision
gh pr diff <PR_NUMBER>
```

Or use the GitHub MCP server tools if available (configured in `.cursor/mcp.json`).

### 2. Review Checklist

#### C++ Changes (`core/*.cppm`, `core/main.cpp`)
- Verify C++23 module syntax (`import std;`, `export module`)
- Check static linking flags for Linux (`-static`) and macOS (`-nostdlib++`)
- Verify `xmake.lua` build configuration if build targets change
- Ensure platform-specific code is properly guarded in platform modules

#### Rust Changes (`core/xvm/`)
- Run `cargo test --manifest-path core/xvm/Cargo.toml --all-targets`
- Run `cargo clippy --manifest-path core/xvm/Cargo.toml`
- Check xvm-shim binary behavior for version switching
- Verify YAML config compatibility (`versions.xvm.yaml`, `.workspace.xvm.yaml`)

#### Lua Changes (`core/xim/`)
- Verify xim module structure (self-contained in `xim/` directory)
- Check package index compatibility
- Test with `xmake xim -P $XLINGS_HOME -- <args>`

#### Shell Script Changes (`tools/`, `tests/e2e/`)
- Verify `set -euo pipefail` is present
- Check cross-platform compatibility (Linux/macOS)
- Run E2E tests: `SKIP_NETWORK_TESTS=1 bash tests/e2e/linux_usability_test.sh`

### 3. Build Verification
```bash
# Configure
MUSL_SDK="$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:/opt/xmake:$PATH"
xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- --cc="$CC" --cxx="$CXX" -y

# Build C++
xmake build xlings

# Build Rust
cargo test --manifest-path core/xvm/Cargo.toml --all-targets

# Full release build
SKIP_NETWORK_VERIFY=1 ./tools/linux_release.sh

# E2E tests
SKIP_NETWORK_TESTS=1 bash tests/e2e/linux_usability_test.sh build/xlings-*-linux-x86_64.tar.gz
```

### 4. Review Response
- Summarize changes and their impact
- Flag any issues found
- Suggest improvements if applicable
- Approve or request changes with clear reasoning
