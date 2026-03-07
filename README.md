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


> [!CAUTION]
> xlings is currently migrating from Lua to MC++ with a modular architecture. Some packages may be unstable during this transition. If you run into any problems, please report them via [Issues] or the [Forum].

[Issues]: https://github.com/d2learn/xlings/issues

## Quick Start

### Install (Github)


<details>
  <summary>click to view xlings installation command (old)</summary>

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

#### Linux/MacOS

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
```

#### Windows - PowerShell

```bash
irm https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.ps1 | iex
```


### Useage

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

### SubOS - Environment Isolation Mode

> xlings supports creating isolated workspaces through the `subos` command.

**Global isolated environment**

> Create an isolated environment and install Node.js into it.

```bash
# 0. Create a subos environment
xlings subos new my-subos

# 1. List all subos environments
xlings subos list

# 2. Switch to my-subos
xlings subos use my-subos

# 3. Install node inside the isolated environment
#    (without affecting the system / host environment / other subos)
xlings install node@24 -y
node --version

# 4. Remove the isolated environment
xlings subos remove my-subos
```

**Project isolated environment**

> xlings also supports project-scoped isolated environments through a `.xlings.json`
> file, and can install the configured project environment with one command.
> Below is a real configuration from the [d2x](https://github.com/d2learn/d2x) project:

```json
{
  "workspace": {
    "xmake": "3.0.7",
    "gcc": { "linux": "15.1.0" },
    "openssl": { "linux": "3.1.5" },
    "llvm": { "macos": "20" }
  }
}
```

Install and configure the project environment with one command:

> Run the following command in the project directory:

```bash
xlings install
```

**Note:** the SubOS mechanism uses a `[ version view + reference counting ]`
strategy to avoid repeatedly downloading the same package payloads.

## Community

- Communication Group (Q): 167535744 / 1006282943
- [Community Forum](https://forum.d2learn.org/category/9/xlings): Discussions on related technologies, features, and issues

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
