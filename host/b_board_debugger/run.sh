#!/usr/bin/env bash
set -euo pipefail
B_DEBUG_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd -- "$B_DEBUG_ROOT"
exec python3 -m host.b_board_debugger "$@"
