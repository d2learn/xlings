# Task 02: Config Home Detection

## Goal

Change self-contained home detection from `xim/ + xmake.lua` markers to the new
bootstrap markers:

- `.xlings.json`
- `bin/xlings`

## Done When

- running `bin/xlings` inside a bootstrap package resolves that package root as
  `XLINGS_HOME`
- fallback to `~/.xlings` still works when the markers are absent
