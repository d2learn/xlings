# Task 01: Config Model And Workspace

## Goal

Split global and project configuration state so project data no longer mutates
the global state in memory or on save.

## Changes

- add separate storage for:
  - global index repos
  - project index repos
  - global versions
  - project versions
  - global workspace
  - project workspace
  - project subos mode metadata
- add helpers for effective merged versions/workspace
- add helpers for project data/index/store directories
- change project default install target generation to read `workspace`

## Acceptance

- project save does not write merged global versions into project config
- `xlings install` in a project without args uses `workspace`
- no-project behavior remains unchanged

## Tests

- config merge/save unit tests
- project workspace install-target generation tests
