#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FIXTURE_INDEX_DIR="${1:-$ROOT_DIR/tests/fixtures/xim-pkgindex}"
XIM_PKGINDEX_REF="${XIM_PKGINDEX_REF:-xlings_0.4.0}"
XIM_PKGINDEX_URL="${XIM_PKGINDEX_URL:-https://github.com/d2learn/xim-pkgindex.git}"

if [[ -d "$FIXTURE_INDEX_DIR/pkgs" ]]; then
  echo "[fixture] reuse existing fixture index: $FIXTURE_INDEX_DIR"
  exit 0
fi

rm -rf "$FIXTURE_INDEX_DIR"
mkdir -p "$(dirname "$FIXTURE_INDEX_DIR")"

echo "[fixture] cloning $XIM_PKGINDEX_URL (ref: $XIM_PKGINDEX_REF) -> $FIXTURE_INDEX_DIR"
git clone --depth 1 --branch "$XIM_PKGINDEX_REF" "$XIM_PKGINDEX_URL" "$FIXTURE_INDEX_DIR"

if [[ ! -d "$FIXTURE_INDEX_DIR/pkgs" ]]; then
  echo "[fixture] FAIL: missing pkgs directory after clone: $FIXTURE_INDEX_DIR" >&2
  exit 1
fi

echo "[fixture] ready: $FIXTURE_INDEX_DIR"
