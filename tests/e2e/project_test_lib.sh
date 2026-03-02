#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
FIXTURE_INDEX_DIR="$ROOT_DIR/tests/fixtures/xim-pkgindex"

log()  { echo "[project-e2e] $*"; }
fail() { echo "[project-e2e] FAIL: $*" >&2; exit 1; }

find_xlings_bin() {
  local candidate="${XLINGS_BIN:-}"
  if [[ -n "$candidate" && -f "$candidate" && -x "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="$(find "$BUILD_DIR" -path '*/release/xlings' -type f | head -1)"
  [[ -n "$candidate" && -x "$candidate" ]] || fail "xlings binary not found; set XLINGS_BIN"
  printf '%s\n' "$candidate"
}

require_fixture_index() {
  [[ -d "$FIXTURE_INDEX_DIR/pkgs" ]] || fail "fixture index repo not found at $FIXTURE_INDEX_DIR"
}

runtime_home_dir() {
  local name="$1"
  printf '%s\n' "$ROOT_DIR/tests/e2e/runtime/$name"
}

prepare_scenario() {
  local scenario_dir="$1"
  local home_dir="$2"
  local backup_file
  backup_file="$(mktemp)"
  cp "$scenario_dir/.xlings.json" "$backup_file"
  rm -rf "$home_dir" "$scenario_dir/.xlings"
  printf '%s\n' "$backup_file"
}

restore_scenario() {
  local scenario_dir="$1"
  local home_dir="$2"
  local backup_file="$3"
  rm -rf "$home_dir" "$scenario_dir/.xlings"
  if [[ -f "$backup_file" ]]; then
    cp "$backup_file" "$scenario_dir/.xlings.json"
    rm -f "$backup_file"
  fi
}

write_home_config() {
  local home_dir="$1"
  local mirror="${2:-GLOBAL}"
  mkdir -p "$home_dir"
  mkdir -p "$home_dir/subos/default/bin"
  cp "$(find_xlings_bin)" "$home_dir/xlings"
  cat > "$home_dir/.xlings.json" <<EOF
{
  "mirror": "$mirror",
  "index_repos": [
    {
      "name": "official",
      "url": "$FIXTURE_INDEX_DIR"
    }
  ]
}
EOF
}

run_xlings() {
  local home_dir="$1"
  local workdir="$2"
  shift 2
  env XLINGS_HOME="$home_dir" "$(find_xlings_bin)" "$@" 
}

platform_name() {
  case "$(uname -s)" in
    Darwin) printf 'macosx\n' ;;
    Linux) printf 'linux\n' ;;
    *) fail "unsupported OS: $(uname -s)" ;;
  esac
}

node_archive_name() {
  case "$(platform_name)" in
    macosx) printf 'node-v%s-darwin-arm64.tar.gz\n' "$1" ;;
    linux) printf 'node-v%s-linux-x64.tar.xz\n' "$1" ;;
    *) fail "unsupported platform for node archive" ;;
  esac
}

mdbook_archive_name() {
  case "$(platform_name)" in
    macosx) printf 'mdbook-%s-macosx-arm64.tar.gz\n' "$1" ;;
    linux) printf 'mdbook-%s-linux-x86_64.tar.gz\n' "$1" ;;
    *) fail "unsupported platform for mdbook archive" ;;
  esac
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  local message="$3"
  echo "$haystack" | grep -F "$needle" >/dev/null || fail "$message"
}
