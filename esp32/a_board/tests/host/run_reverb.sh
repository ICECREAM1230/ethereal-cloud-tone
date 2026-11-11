#!/bin/bash
# Actual reverb owner/driver/HTTP paths; no physical device or network access.
set -euo pipefail
ulimit -c 0
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out=$(mktemp -d /tmp/s3-reverb-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]]; then
    common+=(-g -fsanitize=undefined -fno-omit-frame-pointer)
fi
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_reverb_link.c main/event_bus.c main/control_source.c -o "$out/link"
"$out/link"
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_reverb_owner.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c \
    main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c \
    -lm -o "$out/owner"
"$out/owner"
cc "${common[@]}" -std=gnu17 -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs tests/host/test_reverb_http.c main/web_control.c main/control_source.c main/patch.c \
    tests/host/fake_dds_presets.c -Wl,--gc-sections   main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http"
"$out/http"
