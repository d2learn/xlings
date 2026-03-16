# xlings Quick Install

One-line installer scripts for Linux, macOS, and Windows.

## Linux / macOS

```bash
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash
```

### Specify Version

```bash
# Positional argument
curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash -s -- v0.5.0

# Environment variable
XLINGS_VERSION=v0.5.0 curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash
```

Version numbers with or without the `v` prefix are both accepted (`v0.5.0` and `0.5.0`).
When a version is specified, the script skips the network query for the latest release.

### GitHub Mirror

If `github.com` is not accessible, set a mirror:

```bash
XLINGS_GITHUB_MIRROR=https://mirror.example.com curl -fsSL https://mirror.example.com/d2learn/xlings/main/tools/other/quick_install.sh | bash
```

### CI / Non-Interactive Mode

```bash
XLINGS_NON_INTERACTIVE=1 curl -fsSL https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.sh | bash
```

When piped (`curl | bash`), the script automatically redirects stdin from `/dev/tty` to support interactive prompts. Set `XLINGS_NON_INTERACTIVE=1` to disable this behavior in CI environments.

---

## Windows (PowerShell)

```powershell
powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.ps1 | iex"
```

### Specify Version

```powershell
# Environment variable
$env:XLINGS_VERSION = "v0.5.0"
powershell -ExecutionPolicy Bypass -c "irm https://raw.githubusercontent.com/d2learn/xlings/main/tools/other/quick_install.ps1 | iex"

# Or run the script directly with -Version parameter
.\quick_install.ps1 -Version v0.5.0
```

### GitHub Mirror

```powershell
$env:XLINGS_GITHUB_MIRROR = "https://mirror.example.com"
powershell -ExecutionPolicy Bypass -c "irm https://mirror.example.com/d2learn/xlings/main/tools/other/quick_install.ps1 | iex"
```

---

## Environment Variables Reference

| Variable | Platform | Description |
|----------|----------|-------------|
| `XLINGS_VERSION` | All | Install a specific version (e.g. `v0.5.0`) |
| `XLINGS_GITHUB_MIRROR` | All | GitHub mirror base URL |
| `XLINGS_NON_INTERACTIVE` | Linux/macOS | Disable TTY redirect for CI |

## How Version Resolution Works

When no version is specified, the scripts resolve the latest release by following the HTTP redirect from `https://github.com/d2learn/xlings/releases/latest` and extracting the tag name from the final URL. This avoids the GitHub API rate limit (60 requests/hour for unauthenticated users).
