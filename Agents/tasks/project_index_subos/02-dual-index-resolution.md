# Task 02: Dual Index Resolution

## Goal

Resolve packages from project and global indexes with project priority and
explicit ambiguity handling.

## Changes

- maintain separate `IndexManager` instances for global and project indexes
- build an effective resolver that:
  - checks explicit namespace first
  - checks project matches before global matches for bare names
  - fails with candidate list if more than one bare-name candidate remains
- infer namespace from repo name when package metadata omits it

## Acceptance

- bare unique names still work without namespace
- explicit `ns:pkg` resolves only in that namespace
- ambiguous bare names fail with a clear candidate list

## Tests

- namespace inheritance tests
- project-over-global resolution tests
- ambiguity reporting tests
