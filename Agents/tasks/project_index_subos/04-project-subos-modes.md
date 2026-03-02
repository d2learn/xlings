# Task 04: Project Subos Modes

## Goal

Make anonymous and named project subos modes actually load and affect runtime
resolution.

## Changes

- define project config field for optional named subos
- anonymous mode:
  - merge global active subos workspace with project workspace overlay
- named mode:
  - load workspace from project-local subos file
  - do not inherit global active subos workspace
- update workspace save/load paths accordingly

## Acceptance

- anonymous mode overrides global workspace without isolating it
- named mode uses project-local isolated workspace
- shim runtime chooses the effective workspace correctly

## Tests

- effective-workspace unit tests
- project subos load/save tests
- shim dispatch resolution tests
