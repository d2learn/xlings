#!/usr/bin/env bash
set -euo pipefail

SCENARIO_NAME="xlings_res_region_map" \
EXPECTED_RES_SERVER="https://gitcode.com/xlings-res" \
HOME_NAME="xlings_res_region_map_home" \
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_test.sh"
