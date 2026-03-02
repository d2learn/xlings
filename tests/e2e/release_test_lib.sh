#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUNTIME_ROOT="$ROOT_DIR/tests/e2e/runtime"
FIXTURE_INDEX_DIR="$ROOT_DIR/tests/fixtures/xim-pkgindex"

log()  { echo "[release-e2e] $*"; }
fail() { echo "[release-e2e] FAIL: $*" >&2; exit 1; }

minimal_system_path() {
  case "$(uname -s)" in
    Darwin) printf '/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin\n' ;;
    Linux) printf '/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin\n' ;;
    *) fail "unsupported OS: $(uname -s)" ;;
  esac
}

require_release_archive() {
  local archive="${1:-$ROOT_DIR/build/release.tar.gz}"
  [[ -f "$archive" ]] || fail "release archive not found: $archive"
}

require_fixture_index() {
  [[ -d "$FIXTURE_INDEX_DIR/pkgs" ]] || fail "fixture index repo missing at $FIXTURE_INDEX_DIR"
}

extract_release_archive() {
  local archive="$1"
  local name="$2"
  local extract_root="$RUNTIME_ROOT/$name"

  rm -rf "$extract_root"
  mkdir -p "$extract_root"
  tar -xzf "$archive" -C "$extract_root"

  local pkg_dir
  pkg_dir="$(find "$extract_root" -mindepth 1 -maxdepth 1 -type d -name 'xlings-*' | head -1)"
  [[ -n "$pkg_dir" ]] || fail "extracted release package dir not found under $extract_root"

  case "$(uname -s)" in
    Darwin) xattr -cr "$pkg_dir" 2>/dev/null || true ;;
  esac

  printf '%s\n' "$pkg_dir"
}

default_d2x_version() {
  case "$(uname -s)" in
    Darwin) printf '0.1.3\n' ;;
    Linux) printf '0.1.1\n' ;;
    *) fail "unsupported OS: $(uname -s)" ;;
  esac
}

write_fixture_release_config() {
  local pkg_dir="$1"
  cat > "$pkg_dir/.xlings.json" <<EOF
{
  "version": "0.4.0",
  "mirror": "GLOBAL",
  "activeSubos": "default",
  "subos": { "default": { "dir": "" } },
  "index_repos": [
    { "name": "official", "url": "$FIXTURE_INDEX_DIR" }
  ]
}
EOF
}
