#!/bin/bash
set -euo pipefail
ulimit -c 0
cd "$(dirname "$0")/../.."
out=${KS16_HOST_OUT:-'/media/ice/D;/FPGA_simulation/my_project/ks16-20261025/firmware/host'}
mkdir -p "$out"
common=(-std=c11 -Wall -Wextra -Werror)
if [[ ${SANITIZE:-0} == 1 ]]; then common+=(-g -fsanitize=undefined -fno-sanitize-recover=all); fi
cc "${common[@]}" -Itests/host/stubs -Imain tests/host/fake_runtime.c tests/host/test_ks16_spi.c main/spi_regs.c -o "$out/ks_spi"
"$out/ks_spi"
cc "${common[@]}" -Itests/host/stubs -Imain tests/host/fake_queue.c tests/host/fake_runtime.c tests/host/test_ks_engine.c tests/host/fake_performance_link.c main/event_bus.c -o "$out/ks_engine"
"$out/ks_engine"
cc "${common[@]}" -Itests/host/stubs -Imain tests/host/test_ks_voice.c main/ks_params.c main/patch.c -lm -o "$out/ks_voice"
"$out/ks_voice"
cc "${common[@]}" -Imain tests/host/test_midi_templates.c main/midi_templates.c main/ks_params.c main/patch.c -lm -o "$out/midi_templates"
"$out/midi_templates" tests/host/ks_parameters_expected.csv
cc "${common[@]}" -Imain tests/host/ks_parameter_dump.c main/ks_params.c -lm -o "$out/ks_parameters"
"$out/ks_parameters" > "$out/ks_parameters.csv"
python3 - "$out/ks_parameters.csv" <<'PY_GOLDEN'
import csv, sys
with open('tests/host/ks_parameters_expected.csv') as f:
    expected = list(csv.reader(f))[1:]
with open(sys.argv[1]) as f:
    actual = [row[:4] for row in csv.reader(f)]
assert len(actual) == 128 and actual == expected
assert all(row[1:] == ['0','0','0'] for row in actual[:19])
print('KS16 parameters: 0..18 rejected; 19..127 exact golden L/C/rho PASS')
PY_GOLDEN
