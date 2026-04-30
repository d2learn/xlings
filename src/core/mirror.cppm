// xlings.core.mirror — top-level module entry for the GitHub URL mirror
// fallback subsystem.
//
// All actual logic lives in partition files under src/core/mirror/.
// This file's only job is to re-export the public surface so callers
// (xim/downloader.cppm, xim/repo.cppm, etc.) can `import xlings.core.mirror`
// and reach `xlings::mirror::expand(...)` without depending on the
// individual partition modules.
//
// Layout:
//   mirror.cppm           — this file (re-exports)
//   mirror/types.cppm     — ResourceType / Form / Mode enums + Mirror struct
//   mirror/forms.cppm     — three URL-rewriting strategies (internal)
//   mirror/registry.cppm  — embedded default list + user-override loader
//   mirror/expand.cppm    — classify() + expand() public API
//
// See docs/plans/2026-05-01-mirror-fallback-step1.md for the full design.

export module xlings.core.mirror;

export import xlings.core.mirror.types;
export import xlings.core.mirror.expand;
// registry and forms are implementation details, not re-exported.
