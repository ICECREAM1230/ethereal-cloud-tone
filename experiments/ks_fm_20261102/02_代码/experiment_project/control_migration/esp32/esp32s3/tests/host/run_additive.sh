#!/bin/bash
# Pure host validation; no flash, reset, serial, JTAG, network or Git writes.
set -euo pipefail
ulimit -c 0
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out=$(mktemp -d /tmp/s3-additive-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]]; then
    common+=(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
cc "${common[@]}" -Wno-misleading-indentation tests/host/test_additive_link.c \
    main/additive_patch.c main/additive_link.c -o "$out/link"
"$out/link"
cc "${common[@]}" -pthread tests/host/test_web_additive.c main/additive_patch.c -o "$out/parser"
"$out/parser" contract/additive/20261019/presets.json
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_additive_owner.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c \
    main/web_control.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/additive_patch.c \
    main/web_additive.c main/parameter_cc.c -lm -o "$out/owner"
"$out/owner"
cc "${common[@]}" -std=gnu17 -DCONFIG_FPGA_FM_ONLY=0 -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs tests/host/test_http_additive.c \
    main/web_control.c main/patch.c tests/host/fake_dds_presets.c \
    main/web_raw_midi.c main/web_raw_queue.c main/web_additive.c main/additive_patch.c \
    -Wl,--gc-sections -lm -lz -o "$out/http"
"$out/http"
