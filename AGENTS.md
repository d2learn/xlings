## Cursor Cloud specific instructions

### Overview

xlings is a cross-platform package manager CLI tool. The codebase has three main build targets:

1. **C++23 main binary** (`xlings`) — built with xmake + musl-gcc 15.1.0
2. **Rust xvm** (version manager) — built with cargo
3. **Lua xim** (package installer) — runs via xmake, no compilation needed

### Build commands

```bash
# Configure (exactly like CI)
MUSL_SDK=$HOME/.xlings/data/xpkgs/musl-gcc/15.1.0
xmake f -c -p linux -m release --sdk="$MUSL_SDK" --cross=x86_64-linux-musl- \
  --cc=x86_64-linux-musl-gcc --cxx=x86_64-linux-musl-g++

# Build C++ binary
xmake build xlings

# Run Rust tests
cargo test --manifest-path core/xvm/Cargo.toml --all-targets

# Full release build (skip network tests)
SKIP_NETWORK_VERIFY=1 ./tools/linux_release.sh
```

### Key caveats

- **musl-gcc binutils**: The musl-gcc SDK's internal binutils (`x86_64-linux-musl/bin/as`, `ld`, etc.) are dynamically linked against musl libc with hardcoded interpreter path `/home/xlings/.xlings_data/lib/ld-musl-x86_64.so.1`. This path must exist (create a symlink to `$MUSL_SDK/x86_64-linux-musl/lib/libc.so`). Also fix the broken `ld-musl-x86_64.so.1` symlink inside the SDK lib dir.
- **xmake `--cross` flag is required**: Without `--cross=x86_64-linux-musl-`, gcc uses its internal assembler path which hits the broken musl binutils. The `--cross` prefix makes xmake use the statically-linked tools in `$SDK/bin/`.
- **`source ~/.bashrc`** is needed after installing xlings to pick up PATH changes.
- **Package index**: If `xlings search` returns empty, delete `~/.xlings/data/xim_last_update_time` to force index sync.
- **xlings config**: To use GitHub mirrors instead of Gitee/Gitcode, edit `~/.xlings/.xlings.json` and change `index-repo`, `res-server`, and `repo` URLs.
