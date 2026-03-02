#!/usr/bin/env bash
set -euo pipefail

SCENARIO_NAME="xlings_res_multi_server" \
EXPECTED_RES_SERVER="https://gitcode.com/xlings-res" \
HOME_NAME="xlings_res_multi_server_home" \
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/project_xlings_res_test.sh"
