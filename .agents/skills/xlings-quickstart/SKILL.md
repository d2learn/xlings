---
name: xlings-quickstart
description: 使用 xlings (XLINGS Quickstart) 进行工具安装、版本管理、包索引扩展和 subos 环境隔离的操作指南。When tasks involve `xlings`/`xim` package install-search-update-remove flows, namespace package handling, self-hosted/custom index repo setup, or multi-version/subos troubleshooting, use this skill.
---

# XLINGS Quickstart

## Overview

Use this skill to handle the full operational lifecycle of xlings:
- Install xlings and verify runtime.
- Install/search/remove/update tools and env presets via `xlings`/`xim`.
- Work with namespace packages and version selectors.
- Add custom/self-hosted package index repos and refresh index.
- Diagnose multi-version switching and subos environment isolation behavior.

## Installation Workflow

### 1) Install xlings

Linux/macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
```

Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.ps1 | iex
```

### 2) Verify installation

```bash
xlings help
xim -h
```

If `xlings`/`xim` is not found, reload shell profile (`source ~/.bashrc`) and retry.

## Common Usage and Scenarios

Use these commands first for daily operations:

```bash
xlings install gcc@15
xlings remove gcc
xlings search gcc
xlings update gcc
```

Use `xim` directly when you need lower-level package-index operations:

```bash
xim -s gcc
xim -l
xim --update index
```

Scenario guide:
- Need one tool quickly: `xlings install <name>` or `xlings install <name>@<version>`.
- Need to inspect candidates first: `xlings search <keyword>` / `xim -s <keyword>`.
- Need to cleanup: `xlings remove <name>`.
- Need to refresh package metadata: `xim --update index`.

## Namespace and Custom Index

Package name forms to support:
- `<name>`
- `<name>@<version>`
- `<namespace>:<name>`
- `<namespace>:<name>@<version>`

For custom or self-hosted index repos, register sub-indexrepo with namespace binding:

```bash
xim --add-indexrepo <namespace>:<git-repo-url>
xim --update index
```

For ad-hoc package file onboarding:

```bash
xim --add-xpkg /path/to/your.xpkg.lua
# or
xim --add-xpkg https://your-host/path/to/your.xpkg.lua
```

## Multi-Version and Subos Isolation

Use `xvm` through `xlings` for version switching:

```bash
xlings use <tool>
xlings use <tool> <version>
```

Use subos commands when workloads need isolated env views:

```bash
xlings subos list
xlings subos new <env-name>
xlings subos use <env-name>
```

Operational model to remember:
- Package store can be shared globally.
- Active tool mapping/version visibility is managed per subos via xvm metadata.
- Namespace + version selector avoids collisions for similarly named packages.

## References

- Read [references/commands.md](references/commands.md) for command cheatsheet.
- Read [references/links.md](references/links.md) for project, index, docs, and community URLs.
