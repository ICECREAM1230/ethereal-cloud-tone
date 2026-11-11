#!/bin/bash
# Software-only regression: real FMC discovery -> DDS worker / Web owner.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
ulimit -c 0

# A caller may point at a preserved voice_alloc.c for a red/green comparison.
voice_dir=${S3_DDS_VOICE_ALLOC_DIR:-$root/main}
scratch_root=${S3_SIM_TMPDIR:-/media/ice/D;/FPGA_simulation/esp32_dds_channel_routes}
mkdir -p "$scratch_root"
out=$(mktemp -d "$scratch_root/host.XXXXXX")
trap 'rm -rf "$out"' EXIT
flags=(-std=gnu17 -Wall -Wextra -Werror -Wno-format-truncation
       -ffunction-sections -fdata-sections -g -fsanitize=undefined
       -fno-sanitize-recover=all -DCONFIG_FPGA_AE350_CONTROL=1
       -Itests/host/stubs -Itests/host)
export UBSAN_OPTIONS=halt_on_error=1

"${CC:-cc}" "${flags[@]}" -Imain tests/host/test_dds_channel_routes_fmc.c \
    main/raw_midi.c main/midi_template_link.c main/ae350_control.c main/fdc_control.c \
    tests/host/fake_queue.c tests/host/fake_runtime.c main/event_bus.c main/control_source.c \
    -Wl,--gc-sections -lm -o "$out/fmc"
"$out/fmc" "$out/fmc.bin"
if [[ -n ${S3_TEST_FIXTURE_DIR:-} ]]; then
    mkdir -p "$S3_TEST_FIXTURE_DIR"
    cp "$out/fmc.bin" "$S3_TEST_FIXTURE_DIR/dds-channel-routes-fmc.bin"
fi

owner_sources=(tests/host/fake_queue.c tests/host/fake_runtime.c main/event_bus.c
    main/patch.c main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c
    main/fm_voice.c tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c
    main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c)
"${CC:-cc}" "${flags[@]}" -I"$voice_dir" -Imain tests/host/test_dds_channel_routes_load.c \
    "${owner_sources[@]}" main/dds_files.c main/dds_document.c main/dds_patch_units.c \
    main/vendor/tomlc17/tomlc17.c -Wl,--gc-sections -lm -o "$out/load"
"${CC:-cc}" "${flags[@]}" -I"$voice_dir" -Imain tests/host/test_dds_channel_routes_cc.c \
    "${owner_sources[@]}" tests/host/fake_dds_presets.c \
    -Wl,--gc-sections -lm -o "$out/cc"

load_cases=(global-ks-channel global-fm-channel global-dds
    global-dds-fmc global-ks-fmc global-fm-fmc single-editor-dds-fmc
    single-editor-ks single-editor-dds cap-invalid-ready cap-unsupported-ready
    cap-invalid-closing cap-unsupported-closing blocked-request out-of-range-request
    desired3-request applied3-request applied42-request route-fault-ready
    route-invalid-ready route-fault-closing source-revoke-ready source-revoke-closing
    context-change-ready context-change-closing reset-closing stop-fault-closing
    stop-unavailable-closing stop-timeout)
cc_cases=(dds route-ks route-fm single-editor cap-invalid cap-unsupported
    source-revoked context-stale route-fault desired3 applied3 applied42)
failed=0
for scenario in "${load_cases[@]}"; do
    mkdir "$out/storage-$scenario"
    if "$out/load" "$scenario" "$out/fmc.bin" "$out/storage-$scenario"; then
        echo "RESULT LOAD $scenario exit=0"
    else
        status=$?
        echo "RESULT LOAD $scenario exit=$status"
        failed=$((failed+1))
    fi
done
for scenario in "${cc_cases[@]}"; do
    if "$out/cc" "$scenario" "$out/fmc.bin"; then
        echo "RESULT CC $scenario exit=0"
    else
        status=$?
        echo "RESULT CC $scenario exit=$status"
        failed=$((failed+1))
    fi
done
echo "DDS channel routes: ${#load_cases[@]} LOAD scenarios, ${#cc_cases[@]} CC scenarios, failures=$failed"
test "$failed" -eq 0
