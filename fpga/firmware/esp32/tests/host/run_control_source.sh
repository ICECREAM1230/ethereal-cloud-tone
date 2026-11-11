#!/bin/bash
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/../.."
out=${CONTROL_SOURCE_HOST_OUT:-/home/ice/Project/ae350-app-control-20261101/source-host}
mkdir -p "$out"
scratch=$(mktemp -d /tmp/ae350-source-host.XXXXXX)
cleanup() {
    code=$?
    python3 - "$scratch" "$out" "$code" <<'PYEND'
from pathlib import Path
import json,shutil,sys
p=Path(sys.argv[1]);out=Path(sys.argv[2]);files=[x.name for x in p.iterdir()]
assert p.parent==Path('/tmp') and p.name.startswith('ae350-source-host.')
shutil.rmtree(p)
(out/'cleanup.json').write_text(json.dumps({'removed':str(p),'files':files,'exit_code':int(sys.argv[3])},indent=2)+'\n')
(out/'exit').write_text(sys.argv[3]+'\n')
PYEND
}
trap cleanup EXIT
common=(-std=c17 -Wall -Wextra -Werror -Wno-format-truncation -Itests/host/stubs -Imain)
owner=(tests/host/fake_queue.c tests/host/fake_runtime.c main/event_bus.c main/patch.c tests/host/fake_dds_presets.c main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c tests/host/fake_ks_engine.c main/web_raw_queue.c main/midi_templates.c main/parameter_cc.c)
parser=(main/ks_timbre_params.c main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c)
engine=(main/raw_midi.c main/midi_template_link.c main/fdc_control.c main/ae350_control.c main/event_bus.c main/control_source.c tests/host/fake_runtime.c tests/host/fake_queue.c)
run() {
    name=$1;shift
    cc "${common[@]}" "$@" -lm -o "$scratch/$name" >"$out/$name.compile.log" 2>&1
    "$scratch/$name" >"$out/$name.log" 2>&1
    cat "$out/$name.log"
}
run source tests/host/test_control_source.c main/control_source.c
run exhaustion tests/host/test_control_source_exhaustion.c
run source_owner tests/host/test_control_source_owner.c "${owner[@]}"
run source_pending tests/host/test_control_source_pending.c "${owner[@]}"
run source_channels -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_control_source_channels.c "${owner[@]}"
run source_dds -DS3_LEGACY_HOST_TEST tests/host/test_control_source_dds.c tests/host/fake_queue.c tests/host/fake_runtime.c main/event_bus.c main/patch.c main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c main/dds_files.c main/dds_document.c main/dds_patch_units.c main/vendor/tomlc17/tomlc17.c
run existing_dds -DS3_LEGACY_HOST_TEST tests/host/test_dds_owner.c tests/host/fake_queue.c tests/host/fake_runtime.c main/event_bus.c main/patch.c main/web_control.c main/control_source.c main/ks_params.c main/ks_voice.c main/fm_voice.c tests/host/fake_fm_engine.c main/dds_files.c main/dds_document.c main/dds_patch_units.c main/vendor/tomlc17/tomlc17.c
run source_engine -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_control_source_engine.c "${engine[@]}"
run source_store tests/host/test_control_source_store.c tests/host/fake_runtime.c tests/host/fake_queue.c main/control_source.c main/ks_timbre_control.c "${parser[@]}"
run source_kst_link tests/host/test_control_source_kst_link.c main/midi_template_link.c main/ks_timbre_params.c main/control_source.c
# This fixture replaces just the legacy KS observation boundary to count Panic.
extended=()
for f in "${owner[@]}"; do [[ $f == tests/host/fake_ks_engine.c ]] || extended+=("$f"); done
run source_extended tests/host/test_control_source_extended_owner.c "${extended[@]}" main/ks_timbre_control.c main/fdc_control.c "${parser[@]}"
run existing_raw tests/host/test_raw_web_owner.c "${owner[@]}"
run existing_ae350 -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_ae350_owner.c "${owner[@]}"
run existing_channels -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_midi_channel_owner.c "${owner[@]}"
run existing_parallel -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_audio_routes_owner.c "${owner[@]}"
run existing_reverb tests/host/test_reverb_owner.c "${owner[@]}"
run existing_fdc tests/host/fdc/test_fdc_owner.c main/fdc_control.c "${owner[@]}"
run existing_kst tests/host/test_ks_timbre_owner.c main/ks_timbre_control.c "${parser[@]}" "${owner[@]}"
run existing_store tests/host/test_ks_timbre_store.c tests/host/fake_runtime.c tests/host/fake_queue.c main/control_source.c main/ks_timbre_control.c "${parser[@]}"
run existing_aor -DCONFIG_FPGA_AE350_CONTROL=1 tests/host/test_audio_routes_link.c "${engine[@]}"
printf 'Control-source migration: all checks PASS\n'
