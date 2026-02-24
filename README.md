<div align=right>

  [中文] | [English] | [Todo]
</div>

<div align=center>
  <img width="120" src="https://xlings.d2learn.org/imgs/xlings-logo.png">

  <em>Xlings | Highly abstract [ package manager ] - <b>"Multi-version management + Everything can be a package"</b></em>

  <b> [Website] | [Quick Start] | [Package Index] | [XPackage] | [Contributing] | [Forum] </b>
</div>

[中文]: README.zh.md
[繁體中文]: README.zh.hant.md
[English]: README.md
[Todo]: README.md

[Website]: https://xlings.d2learn.org
[Quick Start]: https://xlings.d2learn.org/en/documents/quick-start/one-click-install.html
[Package Index]: https://d2learn.github.io/xim-pkgindex
[XPackage]: https://xlings.d2learn.org/en/documents/xpkg/intro.html
[Contributing]: https://xlings.d2learn.org/en/documents/community/contribute/add-xpkg.html
[Forum]: https://forum.d2learn.org/category/9/xlings

## Quick Start

<details>
  <summary>click to view xlings installation command</summary>

---

#### Linux/MacOS

```bash
curl -fsSL https://d2learn.org/xlings-install.sh | bash
```

#### Windows - PowerShell

```bash
irm https://d2learn.org/xlings-install.ps1.txt | iex
```

> tips: xlings -> [details](https://xlings.d2learn.org)

---

</details>

**`Type1: Install Software/Tools`**

```bash
xlings install vscode
xlings install devcpp
xlings install gcc@15
```

**`Type2: Config Environment`**

```bash
xlings install config:rust-crates-mirror
xlings install config:xxx
```

**`Type3: Install Tutorial` - [Interactive C++ Tutorial](https://github.com/Sunrisepeak/mcpp-standard)**

```bash
xlings install d2x:mcpp-standard
```

👉 [more details...](https://xlings.d2learn.org/en/documents/quick-start/install-and-version.html)

## Environment

When using the **Linux release package** (e.g. `xlings-*-linux-x86_64.tar.gz`):

- **`bin/`** — Real binaries (`xlings`, `xvm`, `xvm-shim`). Use these with your existing `XLINGS_HOME`/`XLINGS_DATA` (or defaults). Add `bin/` to PATH for the default/system xlings behavior.
- **`data/bin/`** — xvm shims (including `xlings`, `xvm`, `xvm-shim` and any installed tools). Running anything from `data/bin/` uses the **package’s isolated environment** (package `XLINGS_HOME`/`XLINGS_DATA`). Add `data/bin/` to PATH to use the package in a self-contained way.

## Community

- Communication Group (Q): 167535744 / 1006282943
- [Community Forum](https://forum.d2learn.org/category/9/xlings): Discussions on related technologies, features, and issues

## Testing

Unit tests use [Google Test](https://github.com/google/googletest). From the project root:

```bash
# Smoke test only (no C++ modules, no SDK required)
xmake build xlings_smoke_test && xmake run xlings_smoke_test

# Full tests (requires same SDK as main binary, e.g. for import std)
xmake f --sdk=/path/to/gcc-15   # or your toolchain
xmake build xlings_test && xmake run xlings_test
```

Or run both via script (full tests only when `XLINGS_SDK` is set):

```bash
./tools/run_tests.sh
XLINGS_SDK=/path/to/gcc-15 ./tools/run_tests.sh   # include full tests
```

E2E usability test (isolated release package scenarios):

```bash
bash tests/e2e/linux_usability_test.sh
SKIP_NETWORK_TESTS=0 bash tests/e2e/linux_usability_test.sh
```

## Contributing

- [Issue Handling and Bug Fixing
](https://xlings.d2learn.org/en/documents/community/contribute/issues.html)
- [Adding New Packages](https://xlings.d2learn.org/en/documents/community/contribute/add-xpkg.html)
- [Documentation Writing](https://xlings.d2learn.org/en/documents/community/contribute/documentation.html)

**👥Contributors**

[![Star History Chart](https://api.star-history.com/svg?repos=d2learn/xlings,d2learn/xim-pkgindex&type=Date)](https://star-history.com/#d2learn/xlings&d2learn/xim-pkgindex&Date)

<a href="https://github.com/d2learn/xlings/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=d2learn/xlings" />
</a>
