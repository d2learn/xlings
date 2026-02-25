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


> [!CAUTION]
> xlings is currently migrating from Lua to MC++ with a modular architecture. Some packages may be unstable during this transition. If you run into any problems, please report them via [Issues] or the [Forum].

[Issues]: https://github.com/d2learn/xlings/issues

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
