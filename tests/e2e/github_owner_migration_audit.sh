#!/usr/bin/env bash
# Audit the minimal openxlings migration contract:
# - active product/CI/docs entrypoints must not point at d2learn GitHub owners
# - xlings.d2learn.org and forum.d2learn.org stay as public site/forum URLs
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

scan_paths=(
  ".agents/skills/xlings-build"
  ".agents/skills/xlings-quickstart"
  ".github"
  ".gitmodules"
  "README.md"
  "README.zh.md"
  "config"
  "docs/README.md"
  "docs/quick-install.md"
  "src"
  "tests/e2e/bootstrap_home_test.ps1"
  "tests/e2e/mirror_fallback_test.sh"
  "tests/e2e/prepare_fixture_index.sh"
  "tests/e2e/release_quick_install_test.sh"
  "tests/e2e/release_test_lib.ps1"
  "tests/e2e/scenarios/linux_headers/.xlings.json"
  "tests/unit/test_main.cpp"
  "tools"
)

forbidden_patterns=(
  "github\\.com/d2learn/(xlings|xim-pkgindex|xim-pkgindex-scode|xim-pkgindex-fromsource|xpkgindex|xim-pkgindex-skills|xim-pkgindex-awesome|xim-pkgindex-template)([/.\"?#]|$)"
  "raw\\.githubusercontent\\.com/d2learn/xlings([/.]|$)"
  "d2learn\\.github\\.io/xim-pkgindex"
  "github/d2learn"
  "GITHUB_REPO=\"d2learn/xlings"
  "\\\$GITHUB_REPO = \"d2learn/xlings"
  "repos=d2learn/(xlings|xim-pkgindex)"
  "repo=d2learn/xlings"
  "#d2learn/(xlings|xim-pkgindex)"
  "mirror\\.example\\.com/d2learn/xlings"
)

failed=0
for pattern in "${forbidden_patterns[@]}"; do
  if rg -n "$pattern" "${scan_paths[@]}"; then
    echo "FAIL: old GitHub owner reference remains: $pattern" >&2
    failed=1
  fi
done

if ! rg -q --fixed-strings "xlings.d2learn.org" README.md README.zh.md docs/README.md config/i18n .agents/skills/xlings-quickstart; then
  echo "FAIL: xlings.d2learn.org should remain for the minimal migration" >&2
  failed=1
fi

if ! rg -q --fixed-strings "forum.d2learn.org" README.md README.zh.md docs/README.md tools/other .agents/skills/xlings-quickstart; then
  echo "FAIL: forum.d2learn.org should remain for the minimal migration" >&2
  failed=1
fi

exit "$failed"
