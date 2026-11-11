#!/bin/bash
# Software integration only; no radio, serial port, FPGA or audio device access.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
bash tests/host/run_control_source.sh
bash tests/host/run_control_api.sh
bash tests/host/run_ble_control.sh
