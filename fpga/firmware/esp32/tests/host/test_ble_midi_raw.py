#!/usr/bin/env python3
"""Compile the actual ingress implementation without a NimBLE/radio dependency.

Only the radio-independent region is selected; no source-text assertions or
rewrites are performed. C tests execute the actual ring callback, RX task,
epoch helpers, unframer and raw submission calls with fake I/O/scheduling.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'main/ble_midi.c').read_text()
begin = source.index('static int s_conn_count;') + len('static int s_conn_count;')
end = source.index('// Issue #67 notes:', begin)
work = root / 'build/host-tests/work'
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='s3-ble-raw-test-', dir=work) as out:
    out = Path(out)
    (out / 'ble_midi_raw_under_test.inc').write_text(source[begin:end])
    cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-parameter', '-I' + str(root / 'tests/host/stubs'),
           '-I' + str(root / 'main'), '-I' + str(out),
           str(root / 'tests/host/test_ble_midi_raw.c'),
           str(root / 'main/ble_midi_unframer.c'),
           '-o', str(out / 'test')]
    if '--sanitize' in sys.argv:
        cmd[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(cmd, check=True)
    subprocess.run([str(out / 'test')], check=True)
