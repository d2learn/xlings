#!/usr/bin/env bash
set -euo pipefail

bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_local_repo_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_global_fallback_test.sh"
