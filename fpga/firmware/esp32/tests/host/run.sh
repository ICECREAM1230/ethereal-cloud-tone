#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
# Header paths only: production tests never link the retired additive driver.
host_headers=(-Itests/host/stubs -Imain)
fw=main
out=$(mktemp -d /tmp/firmware-host.XXXXXX)
trap 'rm -rf "$out"' EXIT
python3 -B tools/generate_dds_factory.py --check
bash tests/host/run_feedback.sh
bash tests/host/run_control_ports.sh
cc -std=c17 -Wall -Wextra -Werror -Wno-format-truncation -I"$fw" \
    tests/host/test_dds_documents.c "$fw/dds_document.c" "$fw/dds_patch_units.c" \
    "$fw/dds_files.c" "$fw/patch.c" "$fw/vendor/tomlc17/tomlc17.c" -lm "${host_headers[@]}" -o "$out/dds_documents"
cc -std=c17 -Wall -Wextra -Werror -Wno-format-truncation -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_dds_owner.c \
    "$fw/event_bus.c" "$fw/patch.c" "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" \
    "$fw/fm_voice.c" tests/host/fake_fm_engine.c "$fw/dds_files.c" "$fw/dds_document.c" \
    "$fw/dds_patch_units.c" "$fw/vendor/tomlc17/tomlc17.c" -lm "${host_headers[@]}" -o "$out/dds_owner"
"$out/dds_owner"
"$out/dds_documents"
cc -std=c11 -Wall -Wextra -Werror -DRELIABLE_EVENTS -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/test_event_bus.c "$fw/event_bus.c" "${host_headers[@]}" -o "$out/event_bus"
"$out/event_bus"
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -DTRANSACTION_ENGINE -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_engine_order.c tests/host/fake_performance_link.c "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/engine"
"$out/engine"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_voice_deadline.c "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c -lm "${host_headers[@]}" -o "$out/voice_deadline"
"$out/voice_deadline"
cc -std=c11 -Wall -Wextra -Werror -DRC_RELEASE -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_release_model.c "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c -lm "${host_headers[@]}" -o "$out/release"
"$out/release"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_runtime.c tests/host/test_spi_fences.c "$fw/spi_regs.c" "${host_headers[@]}" main/control_source.c -o "$out/spi_fences"
"$out/spi_fences"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_transport_progress.c tests/host/fake_performance_link.c \
    "$fw/spi_regs.c" "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/transport_progress"
"$out/transport_progress"
"$out/transport_progress" fm 60
"$out/transport_progress" fm 100
"$out/transport_progress" fm-select 60
"$out/transport_progress" fm-select 100
cc -std=c11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3 -I"$fw" \
    tests/host/test_status_led.c "$fw/status_led_pattern.c" "${host_headers[@]}" -o "$out/status_led"
"$out/status_led"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DCONFIG_IDF_TARGET_ESP32S3 \
    -Itests/host/panel_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_slider.c tests/host/fake_dds_presets.c "${host_headers[@]}" main/control_source.c -o "$out/slider"
"$out/slider"
cc -std=c11 -Wall -Wextra -Werror -DCONFIG_IDF_TARGET_ESP32S3 \
    -Itests/host/panel_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_panel_midi.c "$fw/midi_parser.c" "${host_headers[@]}" -o "$out/panel_midi"
"$out/panel_midi"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_midi_source.c "${host_headers[@]}" -o "$out/midi_source"
"$out/midi_source"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_web_control.c "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c -lm "${host_headers[@]}" -o "$out/web_control"
"$out/web_control"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_ks_engine.c tests/host/fake_performance_link.c "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/ks_engine"
"$out/ks_engine"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_ks_voice.c "$fw/ks_params.c" "$fw/patch.c" -lm "${host_headers[@]}" -o "$out/ks_voice"
"$out/ks_voice"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_ks_web.c \
    "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c -lm "${host_headers[@]}" -o "$out/ks_web"
"$out/ks_web"
cc -std=c11 -Wall -Wextra -Werror -I"$fw" \
    tests/host/ks_parameter_dump.c "$fw/ks_params.c" -lm "${host_headers[@]}" -o "$out/ks_parameters"
"$out/ks_parameters" > "$out/ks_parameters.csv"
python3 - "$out/ks_parameters.csv" <<'PY'
import csv, sys
with open('tests/host/ks_parameters_expected.csv') as f:
    expected = list(csv.reader(f))[1:]
with open(sys.argv[1]) as f:
    actual = [row[:4] for row in csv.reader(f)]
assert len(actual) == 128 and actual == expected, 'L/C/rho diverged from independent 128-note golden'
print('KS parameters: all 128 MIDI L/C/rho exactly match independent golden PASS')
PY
python3 -B tools/compress_web.py "$repo/main/web/index.html" "$out/index.html.gz"
python3 - "$repo/main/web/index.html" "$out/page.S" "$out/index.html.gz" <<'PY'
from pathlib import Path
import sys
asset = str(Path(sys.argv[1])).replace('\\', '\\\\').replace('"', '\\"')
Path(sys.argv[2]).write_text('.section .rodata\n.global _binary_index_html_start\n'
    '_binary_index_html_start:\n.incbin "' + asset + '"\n.byte 0\n'
    '.global _binary_index_html_end\n_binary_index_html_end:\n'
    '.global _binary_index_html_gz_start\n_binary_index_html_gz_start:\n.incbin "' + sys.argv[3] + '"\n'
    '.global _binary_index_html_gz_end\n_binary_index_html_gz_end:\n'
    '.section .note.GNU-stack,"",@progbits\n')
PY
cc -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_http_api.c "$fw/web_control.c" "$fw/control_source.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$out/page.S" \
    -Wl,--gc-sections   "${host_headers[@]}" main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http_api"
"$out/http_api" "$repo/main/web/index.html" "$out/firmware-state.json" "$out/served-index.html.gz"
cc -std=gnu17 -Wall -Wextra -Werror -Wno-format-truncation -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_dds_http.c tests/host/fake_queue.c "$fw/web_control.c" "$fw/control_source.c" "$fw/patch.c" \
    "$fw/dds_files.c" "$fw/dds_document.c" "$fw/dds_patch_units.c" \
    "$fw/vendor/tomlc17/tomlc17.c" "$out/page.S" -Wl,--gc-sections   "${host_headers[@]}" main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c -lm -lz -o "$out/dds_http"
"$out/dds_http"
python3 - "$repo/main/web/index.html" "$out/served-index.html.gz" <<'PY'
from pathlib import Path
import gzip, sys
assert gzip.decompress(Path(sys.argv[2]).read_bytes()) == Path(sys.argv[1]).read_bytes()
print('HTTP gzip: delivered body decompresses to the exact authoritative HTML PASS')
PY
"${NODE_BINARY:-node}" tests/web/check-firmware-state.mjs "$out/firmware-state.json"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_web_v2_parse.c "$fw/web_control.c" "$fw/control_source.c" "${host_headers[@]}" -o "$out/web_v2_parse"
"$out/web_v2_parse"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_web_v2_owner.c \
    "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c -lm "${host_headers[@]}" -o "$out/web_v2_owner"
"$out/web_v2_owner"
cc -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_http_v2.c "$fw/web_control.c" "$fw/control_source.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$out/page.S" \
    -Wl,--gc-sections   "${host_headers[@]}" main/web_raw_midi.c main/web_raw_queue.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http_v2"
"$out/http_v2" "$out/v2-capabilities.json" "$out/v2-state.json" "$out/v2-catalog.json" \
    "$out/v2-catalog-page-8.json" "$out/v2-profile.json" "$out/v2-presets.json" \
    "$out/v2-live-capabilities.json" "$out/v2-live-state.json"
"${NODE_BINARY:-node}" tests/web/check-http-v2-fixtures.mjs "$out"
if [[ -n ${S3_TEST_FIXTURE_DIR:-} ]]; then
    mkdir -p "$S3_TEST_FIXTURE_DIR"
    cp "$out"/v2-*.json "$S3_TEST_FIXTURE_DIR/"
fi

cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_fm_engine.c tests/host/fake_performance_link.c "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/fm_engine"
"$out/fm_engine"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_fm_races.c tests/host/fake_performance_link.c "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/fm_races"
"$out/fm_races"
cc -std=c11 -Wall -Wextra -Werror -I"$fw" \
    tests/host/test_fm_voice.c tests/host/fake_fm_engine.c "$fw/fm_voice.c" "$fw/patch.c" -lm "${host_headers[@]}" -o "$out/fm_voice"
"$out/fm_voice"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_fm_owner.c \
    "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" \
    tests/host/fake_ks_engine.c "$fw/fm_voice.c" tests/host/fake_fm_engine.c -lm "${host_headers[@]}" -o "$out/fm_owner"
"$out/fm_owner"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c -DS3_LEGACY_HOST_TEST tests/host/test_engine_select_owner.c \
    "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" "$fw/ks_params.c" "$fw/ks_voice.c" \
    "$fw/fm_voice.c" tests/host/fake_fm_engine.c -lm "${host_headers[@]}" -o "$out/engine_select_owner"
"$out/engine_select_owner"
python3 -B tests/host/test_fm_profile_identity.py

# Frozen raw MIDI transport and source-isolated ingress (no physical device).
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_raw_midi.c "$fw/raw_midi.c" "${host_headers[@]}" -o "$out/raw_midi"
"$out/raw_midi"
cc -std=c11 -Wall -Wextra -Werror -I"$fw" \
    tests/host/test_ble_midi_unframer.c "$fw/ble_midi_unframer.c" "$fw/midi_parser.c" "${host_headers[@]}" -o "$out/ble_unframer"
"$out/ble_unframer"
cc -std=c11 -Wall -Wextra -Werror -I"$fw" \
    tests/host/test_midi_templates.c "$fw/midi_templates.c" "$fw/ks_params.c" "$fw/patch.c" -lm "${host_headers[@]}" -o "$out/midi_templates"
"$out/midi_templates" tests/host/ks_parameters_expected.csv
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_midi_template_link.c "$fw/midi_template_link.c" "${host_headers[@]}" main/control_source.c -o "$out/template_link"
"$out/template_link"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_midi_parameter_isolation.c "$fw/midi_template_link.c" \
    "$fw/midi_templates.c" "$fw/ks_params.c" "$fw/patch.c"  "${host_headers[@]}" main/control_source.c -lm -o "$out/parameter_isolation"
for scenario in dds ks fm-expression unchanged shared-volume generations latest fence inactive-io health-cadence publish-race first-fence; do
    "$out/parameter_isolation" "$scenario"
done
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -DTRANSACTION_ENGINE \
    -Itests/host/stubs -I"$fw" tests/host/fake_queue.c tests/host/fake_runtime.c \
    tests/host/test_engine_performance.c "$fw/event_bus.c" "${host_headers[@]}" main/control_source.c -o "$out/engine_performance"
"$out/engine_performance"
python3 -B tests/host/test_ble_midi_raw.py
python3 -B tests/host/test_midi_debug_client.py
cc -std=c11 -Wall -Wextra -Werror -I"$fw" \
    tests/host/test_web_raw_midi.c "$fw/web_raw_midi.c" "${host_headers[@]}" -o "$out/web_raw_midi"
"$out/web_raw_midi"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/test_web_raw_queue.c "$fw/web_raw_queue.c" "${host_headers[@]}" -o "$out/web_raw_queue"
"$out/web_raw_queue"
cc -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_http_raw_midi.c "$fw/web_control.c" "$fw/control_source.c" "$fw/patch.c" \
    tests/host/fake_dds_presets.c "$fw/web_raw_queue.c" "$fw/web_raw_midi.c" "$out/page.S" \
    -Wl,--gc-sections   "${host_headers[@]}" main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http_raw_midi"
"$out/http_raw_midi"
cc -std=gnu17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Itests/host/http_stubs -Itests/host/stubs -I"$fw" \
    tests/host/test_http_raw_order.c "$fw/web_control.c" "$fw/control_source.c" "$fw/patch.c" \
    tests/host/fake_dds_presets.c "$fw/web_raw_midi.c" "$fw/web_raw_queue.c" "$out/page.S" \
    -Wl,--gc-sections   "${host_headers[@]}" main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c -lm -lz -o "$out/http_raw_order"
"$out/http_raw_order"
cc -std=c11 -Wall -Wextra -Werror -Itests/host/stubs -I"$fw" \
    tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_raw_web_owner.c \
    "$fw/event_bus.c" "$fw/patch.c" tests/host/fake_dds_presets.c "$fw/web_control.c" "$fw/control_source.c" \
    "$fw/ks_params.c" "$fw/ks_voice.c" "$fw/fm_voice.c" tests/host/fake_fm_engine.c \
    tests/host/fake_ks_engine.c "$fw/web_raw_queue.c" "$fw/midi_templates.c" \
    "$fw/parameter_cc.c" -lm "${host_headers[@]}" -o "$out/raw_web_owner"
"$out/raw_web_owner"

# Read-only FM validation diagnostics; production must not read this extension.
bash tests/host/run_fm_diagnostics.sh

# Three-engine ownership, independent reverb and inactive-backend isolation.
bash tests/host/run_local_engine.sh
bash tests/host/run_reverb.sh
bash tests/host/run_engine_isolation.sh
