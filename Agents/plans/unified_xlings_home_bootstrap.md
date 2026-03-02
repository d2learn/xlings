# Unified XLINGS_HOME Bootstrap Plan

## Goal

Unify the user-facing model of portable and installed xlings homes:

- extracted release directory is a valid `XLINGS_HOME`
- installed `~/.xlings` is also a valid `XLINGS_HOME`
- both share the same top-level structure and initialization behavior

## Phases

1. Design and task split
2. Config/home detection refactor
3. Shared home bootstrap initialization
4. Minimal release package assembly
5. Validation, CI, and release workflow updates

## Key Rules

- portable mode must not create a nested `./.xlings/` home root
- the bootstrap package initially contains only `.xlings.json` and `bin/xlings`
- `self init` materializes runtime directories in place
- `self install` copies the bootstrap home and then initializes it
- CI must verify both portable bootstrap usage and installed usage
