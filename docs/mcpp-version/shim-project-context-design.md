# Shim Project Context Propagation via Environment Variable

> **Status**: Implemented | **Related**: `core/xvm/shim.cppm`, `core/config.cppm`

---

## Problem

When xmake (itself a shim) runs a build that spawns cmake, which in turn invokes
compiler shims (e.g. `cc`, `g++`), the CWD has moved to a cache directory like
`~/.xmake/cache/packages/...`. The existing `Config::load_project_config_()` walks
up from CWD to find `.xlings.json`, but fails because the cache directory is outside
the project tree. The config falls back to the global context, which has no version
entry for the compiler, producing: `"no version set for 'cc'"`.

Call chain:
```
shim(xmake) -> execvp(real xmake) -> spawn(cmake) -> shim(cc)
                                      CWD = ~/.xmake/cache/...
```

## Solution Options Considered

| Approach | Pros | Cons |
|----------|------|------|
| **XLINGS_PROJECT_DIR env var** | Simple, leverages exec/spawn env propagation | Requires shim to set env before exec |
| CWD traversal only | No env var needed | Breaks when CWD leaves project tree |
| argv[0] path inference | No env dependency | Fragile, doesn't work for all shim types |

**Chosen**: `XLINGS_PROJECT_DIR` environment variable propagation.

Key insight: `execvp()` and `spawn()` inherit the parent process environment.
When the first shim (e.g. xmake) resolves a project context, it sets
`XLINGS_PROJECT_DIR` in the environment. All descendant shims can recover
the project context from this variable even if CWD has changed.

## Implementation

### 1. `shim_dispatch()` sets `XLINGS_PROJECT_DIR` (shim.cppm)

After `Config::paths()` initializes the singleton (which loads project config),
if a project config was found, the shim sets `XLINGS_PROJECT_DIR` to the
project directory before `execvp()`. This propagates to all child processes.

### 2. `load_project_config_()` env var fallback (config.cppm)

The existing CWD-walk logic remains unchanged. If it fails to find a project
config, a new fallback checks `XLINGS_PROJECT_DIR`:

1. Read env var value
2. Verify `$XLINGS_PROJECT_DIR/.xlings.json` exists and is a regular file
3. Verify path is not the XLINGS_HOME directory (same guard as CWD walk)
4. Load project config from that directory

The extracted `load_project_config_from_dir_()` method is shared by both
the CWD-walk path and the env var fallback path.

## Security Considerations

- The env var is only consulted when CWD traversal finds nothing
- The `.xlings.json` file must exist at the specified path
- The XLINGS_HOME exclusion guard prevents circular loading
