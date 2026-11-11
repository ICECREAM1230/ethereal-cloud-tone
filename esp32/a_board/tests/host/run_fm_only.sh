#!/bin/bash
set -euo pipefail
ulimit -c 0
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
# Header paths only: production tests never link the retired additive driver.
host_headers=(-Itests/host/stubs -Imain)
out=$(mktemp -d /tmp/firmware-fm-only-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=c11 -Wall -Wextra -Werror -Itests/host/stubs -Imain)
cc "${common[@]}" -DCONFIG_FPGA_FM_ONLY=1 tests/host/test_fm_only_template.c \
    main/midi_template_link.c "${host_headers[@]}" main/control_source.c -o "$out/template"
"$out/template"
for mode in fm-only production; do
    flags=()
    if [[ $mode == fm-only ]]; then flags=(-DCONFIG_FPGA_FM_ONLY=1); fi
    cc "${common[@]}" "${flags[@]}" tests/host/fake_queue.c tests/host/fake_runtime.c \
        tests/host/test_fm_only_engine.c main/midi_template_link.c main/event_bus.c "${host_headers[@]}" main/control_source.c -o "$out/$mode"
    "$out/$mode" valid
    "$out/$mode" main-image
    if [[ $mode == fm-only ]]; then
        "$out/$mode" bad-version
        "$out/$mode" identity-io
        "$out/$mode" bad-fm-digest
        for check in once timeout first io reset fault muted panic; do
            "$out/$mode" "readback-$check"
        done
    fi
done
cc "${common[@]}" -DCONFIG_FPGA_FM_ONLY=1 tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_fm_only_owner.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c \
    main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c \
    -lm "${host_headers[@]}" -o "$out/owner"
"$out/owner"
cc "${common[@]}" -std=gnu17 -DCONFIG_FPGA_FM_ONLY=1 -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs tests/host/test_fm_only_http.c main/web_control.c main/control_source.c main/patch.c \
    tests/host/fake_dds_presets.c -Wl,--gc-sections   "${host_headers[@]}" main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http"
"$out/http"
