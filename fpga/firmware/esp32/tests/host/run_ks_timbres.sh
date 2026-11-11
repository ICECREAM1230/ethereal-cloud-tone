#!/bin/bash
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/../.."
out=${KS_TIMBRE_HOST_OUT:-'/media/ice/D;/FPGA_simulation/my_project/ks_timbres_20261026/firmware/host'}
mkdir -p "$out"
common=(-std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]];then common+=(-g -fsanitize=undefined -fno-sanitize-recover=all);fi
parser=(main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c)
runtime=(tests/host/fake_queue.c main/ks_timbre_control.c)
cc "${common[@]}" tests/host/test_ks_timbre_document.c "${parser[@]}" -lm -o "$out/document"
"$out/document"
cc "${common[@]}" -DS3_KS_TIMBRE_HOST_TEST=1 tests/host/fake_runtime.c "${runtime[@]}" tests/host/test_ks_timbre_owner.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c "${parser[@]}" -lm -o "$out/owner"
"$out/owner"
cc "${common[@]}" -std=gnu17 -ffunction-sections -fdata-sections -Itests/host/http_stubs tests/host/test_ks_timbre_http.c main/web_control.c main/control_source.c main/patch.c tests/host/fake_dds_presets.c "${runtime[@]}" "${parser[@]}" -Wl,--gc-sections   main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c -lm -lz -o "$out/http"
"$out/http"

cc "${common[@]}" tests/host/test_ks_timbre_link.c main/midi_template_link.c main/control_source.c main/ks_timbre_params.c -lm -o "$out/link"
"$out/link"
cc "${common[@]}" tests/host/test_ks_timbre_store.c "${runtime[@]}" "${parser[@]}" tests/host/fake_runtime.c main/control_source.c -lm -o "$out/store"
"$out/store"
