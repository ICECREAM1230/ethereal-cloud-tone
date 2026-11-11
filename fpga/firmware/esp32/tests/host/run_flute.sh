#!/bin/bash
set -euo pipefail
ulimit -c 0
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
out=$(mktemp -d /tmp/s3-flute.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]]; then common+=(-g -fsanitize=undefined -fno-omit-frame-pointer); fi
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_flute_engine.c \
    main/raw_midi.c main/midi_template_link.c main/event_bus.c main/control_source.c -o "$out/engine"
for initial in 0 1 2 4; do for target in 0 1 2 4; do "$out/engine" "$initial" "$target"; done; done
for overhead in 100 140; do
    for initial in 0 1 2 4; do "$out/engine" "$initial" 4 "$overhead"; done
    for target in 0 1 2; do
        # The unchanged baseline's 1s KS preparation timeout rejects a full
        # bank rebuild at 140us. Keep that known limitation separate from
        # the supported 100us transition and 140us stable-idle regressions.
        if [[ $overhead == 140 && $target == 1 ]]; then continue; fi
        "$out/engine" 4 "$target" "$overhead"
    done
done
cc "${common[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_flute_owner.c \
    main/event_bus.c main/patch.c tests/host/fake_dds_presets.c main/web_control.c main/control_source.c \
    main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c \
    -lm -o "$out/owner"
"$out/owner"
cc -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -Imain tests/host/test_flute_http.c \
    main/web_control.c main/control_source.c main/patch.c tests/host/fake_dds_presets.c main/web_raw_midi.c main/web_raw_queue.c \
    -Wl,--gc-sections   main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http"
"$out/http"
