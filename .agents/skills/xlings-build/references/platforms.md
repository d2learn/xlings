# XLINGS Build Platforms

## Linux

Install and configure:

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
source ~/.bashrc
xlings install musl-gcc@15.1.0 -y
xlings info musl-gcc

MUSL_SDK="${XLINGS_HOME:-$HOME/.xlings}/data/xpkgs/musl-gcc/15.1.0"
export CC=x86_64-linux-musl-gcc
export CXX=x86_64-linux-musl-g++
export PATH="$MUSL_SDK/bin:$PATH"

xmake f -c -p linux -m release \
  --sdk="$MUSL_SDK" \
  --cross=x86_64-linux-musl- \
  --cc="$CC" \
  --cxx="$CXX" \
  -y
```

Build outputs:

- `build/linux/x86_64/release/xlings`
- `core/xvm/target/release/xvm` for plain cargo build
- `build/xlings-*-linux-x86_64.tar.gz` for release script build

## macOS

CI-aligned toolchain setup:

```bash
brew install xmake llvm@20
xmake f -p macosx -m release --toolchain=llvm --sdk=/opt/homebrew/opt/llvm@20 -y
```

Build outputs:

- `build/macosx/arm64/release/xlings`
- `core/xvm/target/release/xvm`
- `build/xlings-*-macosx-arm64.tar.gz`

## Windows

CI-aligned setup:

```powershell
xmake f -p windows -m release -y
```

Build outputs:

- `build\windows\x64\release\xlings.exe`
- `core\xvm\target\release\xvm.exe`
- `build\xlings-*-windows-x86_64.zip`

## Release Scripts

- Linux: `tools/linux_release.sh`
- macOS: `tools/macos_release.sh`
- Windows: `tools/windows_release.ps1`
