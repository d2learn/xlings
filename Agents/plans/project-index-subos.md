# Project Index And Subos Plan

## Goal

Implement project-aware package resolution and activation without breaking the
existing global workflow:

- global index/store remains the default
- project indexes live under the project
- project index packages shadow global packages during resolution
- ambiguous bare names must fail with explicit candidates
- `xlings install` in a project installs from `workspace`, not `deps`
- anonymous project mode inherits global workspace and overrides it locally
- named project subos mode enables project-local isolated workspace

## User-Facing Rules

### Indexes

- Global index repos stay under `$XLINGS_HOME/data`.
- Project index repos live under `<project>/.xlings/data`.
- Effective resolution order is:
  1. project indexes
  2. global indexes
- If a bare package name matches more than one candidate across those layers,
  resolution fails and prints all candidates.

### Namespace

- Every package has an effective namespace.
- If the package file explicitly sets `namespace`, use it.
- Otherwise, inherit the namespace from its index repo name.
- CLI may omit namespace only when the result is unique.

### Install Targets

- In project mode, `xlings install` with no positional packages reads
  `.xlings.json.workspace`.
- `deps` is no longer the default source of install targets.
- `workspace = { gcc = "15.1.0" }` is treated as install target `gcc@15.1.0`.

### Package Storage

- Packages resolved from global indexes install to global `data/xpkgs`.
- Packages resolved from project indexes install to project
  `.xlings/data/xpkgs`.
- Package identity for reuse is `(namespace, name, version)`.

### Workspace / Subos

- Anonymous project mode:
  - project has `.xlings.json`
  - no named project subos is configured
  - effective workspace = global active subos workspace + project workspace
    overlay
- Named project subos mode:
  - project config explicitly names a project subos
  - workspace is loaded from `<project>/.xlings/subos/<name>/.xlings.json`
  - no workspace inheritance from the global active subos

### Version Database

- Global versions and project versions must be stored separately.
- Effective runtime lookup merges them with project versions taking priority.
- Saving project state must not copy the merged global version database into the
  project config.

## Implementation Strategy

1. Split global/project config state instead of mutating one merged structure.
2. Build and query project/global indexes separately, then add an effective
   resolver layer on top.
3. Carry resolved package identity and source store through the install plan.
4. Change default project install target generation from `deps` to `workspace`.
5. Add project subos loading rules for anonymous and named modes.
6. Add tests for namespace inheritance, ambiguous bare names, workspace-driven
   install targets, store routing, and project subos behavior.

## Acceptance

- Existing global-only flows continue to work unchanged.
- Project-only repo packages resolve and install locally.
- Bare-name ambiguity produces a deterministic error listing candidates.
- Running a tool inside a project uses project overrides in anonymous mode.
- Named project subos uses project-local workspace without inheriting the global
  active subos workspace.
- Unit tests pass and repository build/tests succeed for the changed paths.
