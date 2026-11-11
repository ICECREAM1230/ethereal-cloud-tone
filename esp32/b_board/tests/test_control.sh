#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/harp-b-control.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
cc -std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer \
    -fsanitize=address,undefined -fno-pie -no-pie \
    -I"$root/components/b_control/include" -I"$root/components/harp_core/include" \
    "$root/tests/test_control.c" "$root/components/b_control/b_config.c" \
    "$root/components/b_control/b_protocol.c" "$root/components/b_control/b_engine.c" \
    "$root/components/b_control/b_pinmap.c" "$root/components/harp_core/harp_core.c" \
    -o "$build_dir/test_control"
"$build_dir/test_control"
"$build_dir/test_control" --frames > "$build_dir/frames.jsonl"
python3 - "$build_dir/frames.jsonl" <<'PY'
import json, sys
with open(sys.argv[1], encoding='ascii') as frames:
    decoded = [json.loads(line.removeprefix('@B ')) for line in frames]
assert len(decoded) == 3
reply, state, error = decoded
assert reply['id'] == reply['revision'] == 4294967295
assert reply['midi_channel_id'] == 1
assert reply['config']['notes'] == list(range(60, 72))
assert reply['state']['events'] == state['events']
assert len(state['events']) == 8 and state['events'][0][6] is True
assert state['notes'][0] == -1 and error['code'] == 'drain_timeout' and not error['ok']
print('PASS all emitted replies/state parse as JSON; bounded formatting never emits truncated frames')
PY
