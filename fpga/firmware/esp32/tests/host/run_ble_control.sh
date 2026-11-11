#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$(mktemp -d /tmp/ble-control-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
ulimit -c 0
flags=(-std=c17 -Wall -Wextra -Werror -pedantic -g)
if [[ ${BLE_CONTROL_SANITIZE:-0} == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie)
fi
cc "${flags[@]}" \
    -I"$root/main" "$root/tests/host/test_ble_control_codec.c" \
    "$root/main/ble_control_codec.c" -o "$out/codec"
"$out/codec"
for header in esp_err.h esp_log.h esp_timer.h freertos/FreeRTOS.h freertos/queue.h \
    freertos/semphr.h freertos/task.h freertos/timers.h host/ble_hs.h host/ble_gap.h \
    host/ble_gatt.h nimble/nimble_port.h os/os_mbuf.h; do
    mkdir -p "$out/stubs/$(dirname "$header")"
    printf '#include "%s/tests/host/test_ble_control_platform.h"\n' "$root" > "$out/stubs/$header"
done
cc "${flags[@]}" -pthread -I"$out/stubs" -I"$root/main" \
    "$root/tests/host/test_ble_control_gatt.c" "$root/main/ble_control.c" \
    "$root/main/ble_control_codec.c" -o "$out/gatt"
"$out/gatt"
if [[ -n ${BLE_CONTROL_FIXTURES:-} ]]; then
    python3 - "$out/codec" "$BLE_CONTROL_FIXTURES" <<'PY'
import json, math, subprocess, sys
from pathlib import Path
folder=Path(sys.argv[2])
fixtures=json.loads((folder/'capture-manifest.json').read_text())['fixtures']
result=subprocess.run([sys.argv[1],*[str(folder/f['file']) for f in fixtures]],
                      check=True,capture_output=True,text=True)
counts={Path(line.split('\t')[0]).name:int(line.split('\t')[1])
        for line in result.stdout.splitlines() if '\t' in line}
for fixture in fixtures:
    name=fixture['file'];size=counts[name]
    assert size==fixture['ble_envelope_bytes'],(name,size,fixture)
    for limit,count in fixture['fragments_by_packet_limit'].items():
        assert math.ceil(size/(int(limit)-12))==count,(name,limit,count)
    fragments=fixture['fragments_by_packet_limit']['20']
    print(f'ble_control_fixture: {name} envelope={size}B packets20={fragments} '
          f'burst16_ticks_ms={fragments//16*10}')
print(f'ble_control_fixture: {len(fixtures)} actual shared-core bodies matched C streaming encoder')
PY
fi
