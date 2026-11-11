#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
out=$(mktemp -d /tmp/engine-isolation.XXXXXX)
trap 'rm -rf "$out"' EXIT
flags=(-Wall -Wextra -Werror)
if [[ ${S3_UBSAN:-0} == 1 ]]; then
    flags+=(-g -fsanitize=undefined -fno-omit-frame-pointer)
    export UBSAN_OPTIONS=halt_on_error=1
fi
cc -std=c11 "${flags[@]}" -Itests/host/stubs -Imain \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_engine_isolation.c \
    main/event_bus.c main/control_source.c -o "$out/engine"
for scenario in ks-idle fm-idle ks-during-fm fm-during-ks fm-idle-not-ready \
    fm-active-unavailable ks-shared ks-flight ks-active fm-shared fm-flight fm-active \
    local-idle-fm local-idle-ks local-active-fm local-active-ks local-contradiction \
    local-invalid-selected local-leave-fm-offline local-leave-ks-offline; do
    "$out/engine" "$scenario"
done
# Additive mailbox scenarios are retired; the unchanged source remains in the
# pre-removal archive and is deliberately absent from this active runner.
cc -std=c11 "${flags[@]}" -Itests/host/stubs -Imain \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_isolation_owner.c \
    main/event_bus.c main/patch.c tests/host/fake_dds_presets.c main/web_control.c main/control_source.c \
    main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c \
    -lm -o "$out/owner"
"$out/owner"
cc -std=gnu17 "${flags[@]}" -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -Imain tests/host/test_isolation_http.c \
    main/web_control.c main/control_source.c main/patch.c tests/host/fake_dds_presets.c main/web_raw_midi.c \
    main/web_raw_queue.c \
    -Wl,--gc-sections   main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http"
"$out/http"
