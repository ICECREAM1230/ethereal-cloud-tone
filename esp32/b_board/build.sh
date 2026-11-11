#!/usr/bin/env bash
set -euo pipefail
B_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
B_IDF_ROOT="${B_IDF_PATH:-/home/ice/esp/esp-idf-v6.1}"
source "$B_IDF_ROOT/export.sh"
idf.py --version
# The initial CMake/Ninja build mangled the Unicode specs-file path. Keep
# sources in place and use an ASCII output path for this toolchain setup.
idf.py -C "$B_ROOT" -B "${B_BUILD_DIR:-$B_ROOT/.build/idf}" reconfigure "$@" build
