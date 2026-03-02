#!/usr/bin/env bash
set -euo pipefail

bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_local_repo_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_global_fallback_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_override_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_region_map_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_multi_server_test.sh"
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/subos_payload_refcount_test.sh"
