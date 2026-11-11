#!/bin/bash
set -euo pipefail
ulimit -c 0
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
# Header paths only: production tests never link the retired additive driver.
host_headers=(-Itests/host/stubs -Imain)
out=$(mktemp -d /tmp/firmware-fm-diagnostics.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
cc "${common[@]}" tests/host/test_spi_observation.c tests/host/fake_runtime.c "${host_headers[@]}" -o "$out/driver"
"$out/driver"
for mode in fm-only production; do
    flags=(-DCONFIG_FPGA_FM_ONLY=0)
    if [[ $mode == fm-only ]]; then flags=(-DCONFIG_FPGA_FM_ONLY=1); fi
    cc "${common[@]}" "${flags[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
        tests/host/test_fm_diagnostics.c main/event_bus.c "${host_headers[@]}" -o "$out/$mode"
    for check in request budget complete io identity version mid1 end-identity context timeout; do
        "$out/$mode" "$check"
        if [[ $mode == production ]]; then break; fi
    done
    cc "${common[@]}" -std=gnu17 "${flags[@]}" -ffunction-sections -fdata-sections \
        -Itests/host/http_stubs tests/host/test_http_fm_diagnostics.c main/web_control.c main/patch.c \
        tests/host/fake_dds_presets.c -Wl,--gc-sections -lm -lz "${host_headers[@]}" -o "$out/http-$mode"
    "$out/http-$mode" "$out/diagnostics-$mode.json"
    if [[ $mode == fm-only ]]; then
        python3 -B - "$out/diagnostics-$mode.json" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
assert d['request_id']==8 and d['sample_request_id']==7 and d['pending'] and d['valid']
assert d['raw_cache']['source'][1]['flushing'] and d['raw_cache']['source'][1]['fault']==5
assert d['mid1']['events']==88 and d['validation']['raw_dispatch_count']==88
assert d['local_commands']['result']==8
assert d['raw_cache']['inflight'] and d['raw_cache']['inflight_opcode'] is None
assert len(d['blocks'])==4 and all(b['read_ok'] for b in d['blocks'])
print('diagnostics JSON: raw counters and source cache preserved; no fictitious Off/gate/lease fields PASS')
PY
    fi
done
