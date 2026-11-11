#!/usr/bin/env python3
"""Build/test the C reference and optionally replay a saved CSV. No hardware I/O."""
import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'ae350/theremin_control/theremin_control.c'
HEADER = ROOT / 'ae350/theremin_control/theremin_control.h'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='New result directory, must not exist')
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    if not shutil.which(args.cc):
        parser.error('Native host C compiler was not found')
    source_hash = hashlib.sha256(args.input.read_bytes()).hexdigest() if args.input else None
    args.output.mkdir(parents=True, exist_ok=False)
    commands = []

    def run(name, command, *, stdout_file=None):
        if stdout_file:
            with stdout_file.open('x') as stream:
                result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.PIPE, text=True)
            output = result.stderr
        else:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            output = result.stdout + result.stderr
        (args.output / (name + '.log')).write_text(output)
        commands.append({'name': name, 'command': [str(x) for x in command], 'exit': result.returncode})
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + output)
        return output

    common = [args.cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
              '-I' + str(HEADER.parent)]
    with tempfile.TemporaryDirectory(prefix='fdc-ae350-control-') as temp:
        temp = Path(temp)
        test = temp / 'test'
        native = run('native-build', common + ['-O2', str(ROOT/'tests/host/test_theremin_control.c'), str(CORE), '-lm', '-o', str(test)])
        native = run('native-tests', [str(test)])
        run('sanitized-build', common + ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            str(ROOT/'tests/host/test_theremin_control.c'), str(CORE), '-lm', '-o', str(test)])
        sanitized = run('sanitized-tests', [str(test)])
        obj = temp / 'control.o'
        run('core-object', common + ['-O2', '-c', str(CORE), '-o', str(obj)])
        sizes = run('native-size', ['size', str(obj)]) if shutil.which('size') else 'size unavailable'
        undefined = run('native-undefined-symbols', ['nm', '-u', str(obj)]) if shutil.which('nm') else 'nm unavailable'
        run('example-build', common + ['-O2', '-c', str(ROOT/'ae350/ae350_theremin_example.c'), '-o', str(temp/'example.o')])
        replay_summary = None
        if args.input:
            binary = temp / 'replay'
            run('replay-build', common + ['-O2', str(ROOT/'tests/host/replay_theremin_control.c'), str(CORE), '-lm', '-o', str(binary)])
            profile = json.loads(run('default-config', [str(binary), '--config']))
            (args.output/'profile.json').write_text(json.dumps(profile, ensure_ascii=False, indent=2)+'\n')
            run('replay', [str(binary), str(args.input.resolve())], stdout_file=args.output/'mapped_controls.csv')
            with (args.output/'mapped_controls.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            assert rows and all(4864 <= int(r['pitch_code']) <= 6912 and 0 <= int(r['gain_q16']) <= 65536 for r in rows)
            replay_summary = {'rows': len(rows), 'pitch_code_range': [min(int(r['pitch_code']) for r in rows), max(int(r['pitch_code']) for r in rows)],
                'frequency_hz_range': [min(float(r['frequency_hz']) for r in rows), max(float(r['frequency_hz']) for r in rows)],
                'gain_q16_range': [min(int(r['gain_q16']) for r in rows), max(int(r['gain_q16']) for r in rows)],
                'pitch_preview_accepted': sum(r['pitch_accepted']=='1' for r in rows),
                'volume_preview_accepted': sum(r['volume_accepted']=='1' for r in rows),
                'pitch_warning_rows': sum(r['pitch_quality']=='amplitude_warning' for r in rows),
                'volume_warning_rows': sum(r['volume_quality']=='amplitude_warning' for r in rows),
                'source_sha256': source_hash, 'profile': profile, 'kind': 'experimental_unlabelled_percentile_profile',
                'same_dataset_used_for_endpoints_and_preview': True,
                'caution': '10Hz saved-input replay is not an AE350142Hz latency or physical-distance validation.'}
            assert hashlib.sha256(args.input.read_bytes()).hexdigest() == source_hash
        source_files = [CORE, HEADER, ROOT/'ae350/ae350_theremin_example.c', ROOT/'tests/host/test_theremin_control.c', ROOT/'tests/host/replay_theremin_control.c']
        report = {'result': 'PASS', 'native_tests': native.strip(), 'sanitized_tests': sanitized.strip(),
            'native_object_size': sizes, 'native_undefined_symbols': undefined, 'commands': commands,
            'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files},
            'replay': replay_summary, 'validation_scope': 'Native host C99 only; no AE350 SDK/ISA/ABI/link/hardware claim.',
            'temporary_build_removed': True}
    (args.output/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({'result': report['result'], 'native_tests': native.strip(), 'sanitized_tests': sanitized.strip(),
                      'replay': replay_summary, 'report': str(args.output/'report.json')}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
