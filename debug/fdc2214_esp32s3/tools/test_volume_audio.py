#!/usr/bin/env python3
"""Host-only calibration/PCM and existing regressions. Never opens a device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New result directory')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    results = []

    def run(name, command):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
        results.append({'name': name, 'command': list(map(str, command)), 'exit': result.returncode})
        if result.returncode:
            raise RuntimeError(name + ': ' + result.stdout + result.stderr)
        return result.stdout.strip()

    sources = ['components/volume_audio/volume_audio.c', 'components/volume_audio/sine_table.h',
               'components/volume_audio/include/volume_audio.h', 'tests/host/test_volume_audio.c']
    hashes = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources}
    common = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
              '-Icomponents/volume_audio/include', 'tests/host/test_volume_audio.c',
              'components/volume_audio/volume_audio.c', '-lm']
    with tempfile.TemporaryDirectory(prefix='fdc-volume-tests-') as temp:
        binary = str(Path(temp)/'test')
        run('native-build', common + ['-O2', '-o', binary])
        native = run('native-tests', [binary])
        run('sanitized-build', common + ['-O1', '-g', '-fsanitize=address,undefined',
                                    '-fno-omit-frame-pointer', '-o', binary])
        sanitized = run('sanitized-tests', [binary])
        run('existing-driver-build', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wshadow',
            '-fsanitize=address,undefined', '-Icomponents/fdc2214/include', 'tests/host/test_fdc.c',
            'components/fdc2214/fdc2214.c', 'components/fdc2214/fdc_ranges.c', '-o', binary])
        driver = run('existing-driver-tests', [binary])
    run('existing-csv-tests', [sys.executable, '-B', '-m', 'unittest', 'discover', '-s', 'tests/host', '-p', 'test_*.py'])
    run('existing-gui-tests', [sys.executable, '-B', 'gui/test_app.py'])
    drift = [p for p, digest in hashes.items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest() != digest]
    assert not drift
    report = {'result': 'PASS', 'native': native, 'sanitized': sanitized, 'driver': driver,
              'commands': results, 'source_sha256': hashes, 'source_drift': drift,
              'temporary_binaries_removed': True,
              'scope': 'Native C/PCM behavior and existing mock/parser/GUI regressions only; no I2S pin or DAC validation.'}
    (args.output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({'result': 'PASS', 'native': native, 'driver': driver,
                      'report': str(args.output/'report.json')}, ensure_ascii=False))

if __name__ == '__main__':
    main()
