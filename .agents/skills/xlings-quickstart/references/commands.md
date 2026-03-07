# XLINGS Quickstart Command Cheatsheet

## Install and Verify

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/refs/heads/main/tools/other/quick_install.sh | bash
xlings help
xim -h
```

## Package Operations

```bash
xlings install <name>
xlings install <name>@<version>
xlings remove <name>
xlings search <keyword>
xlings update <name>
```

## XIM Direct Operations

```bash
xim -s <keyword>
xim -l
xim --update index
xim --add-xpkg /path/to/your.xpkg.lua
xim --add-indexrepo <namespace>:<git-repo-url>
```

## Namespace and Version Patterns

```text
<name>
<name>@<version>
<namespace>:<name>
<namespace>:<name>@<version>
```

## Multi-Version and Environment Isolation

```bash
xlings use <tool>
xlings use <tool> <version>

xlings subos list
xlings subos new <env-name>
xlings subos use <env-name>
```
