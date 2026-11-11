#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# Optional project-local Ubuntu xcb dependency; keep the system environment
# unchanged. LD_LIBRARY_PATH splits this project's semicolon-containing path.
if [[ -f .venv/local-libs/libxcb-cursor.so.0 ]]; then
    export LD_PRELOAD="$PWD/.venv/local-libs/libxcb-cursor.so.0${LD_PRELOAD:+:$LD_PRELOAD}"
fi
exec .venv/bin/python -B -m pcm_capture.app "$@"
