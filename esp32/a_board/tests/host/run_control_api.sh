#!/bin/bash
# Actual common API, target feature parsers and RAM services. No device I/O.
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/../.."
out=$(mktemp -d /tmp/ae350-control-api.XXXXXX)
trap 'rm -rf "$out"' EXIT
common=(-std=gnu17 -Wall -Wextra -Werror -Wno-misleading-indentation
    -ffunction-sections -fdata-sections -Itests/host/http_stubs -Itests/host/stubs -Imain)
if [[ ${SANITIZE:-0} == 1 ]];then common+=(-g -fsanitize=undefined -fno-sanitize-recover=all);fi
deps=(main/control_source.c main/web_control.c main/patch.c main/web_raw_queue.c
    main/web_raw_midi.c main/fdc_control.c main/ks_timbre_control.c main/ks_timbre_params.c
    main/ks_timbre_document.c main/vendor/tomlc17/tomlc17.c tests/host/fake_queue.c
    tests/host/fake_dds_presets.c)
for name in test_control_api test_control_api_memory test_control_api_edges test_control_api_wifi_events test_control_api_ae350;do
    target=();if [[ $name == test_control_api_ae350 ]];then target=(-DCONFIG_FPGA_AE350_CONTROL=1);fi
    cc "${common[@]}" "${target[@]}" "tests/host/$name.c" "${deps[@]}" -Wl,--gc-sections -lm -lz -pthread -o "$out/$name"
    "$out/$name"
done
# Instrument only a temporary copy: excessive syntax nesting must stop before
# the pinned TOML parser is entered, regardless of preceding quote forms.
python3 - "$out/toml-depth-probe.c" <<'PY'
from pathlib import Path
import sys
source=Path('main/vendor/tomlc17/tomlc17.c').read_text()
signature='toml_result_t toml_parse(const char *src, int len) {'
assert source.count(signature)==1
source=source.replace(signature,'unsigned ks_toml_probe_calls;\n'+signature+'\n    ks_toml_probe_calls++;')
Path(sys.argv[1]).write_text(source)
PY
cc "${common[@]}" -DKS_TIMBRE_TOML_PROBE -Imain/vendor/tomlc17 \
    tests/host/test_ks_timbre_document.c main/ks_timbre_document.c main/ks_timbre_params.c \
    "$out/toml-depth-probe.c" -lm -o "$out/toml-depth-probe"
"$out/toml-depth-probe"
