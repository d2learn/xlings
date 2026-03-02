# Task 04: Minimal Release Package

## Goal

Make release archives ship only the bootstrap files required to start xlings.

## Done When

- release archives contain `.xlings.json` and `bin/xlings`
- runtime directories are created by `self init`, not bundled in the archive
- release verification reflects the bootstrap model
