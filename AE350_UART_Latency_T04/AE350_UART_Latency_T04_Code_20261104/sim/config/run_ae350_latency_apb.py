#!/usr/bin/env python3
"""Actual APB + held CDC + production 50MHz/200k UART directed verification."""
from pathlib import Path
import datetime
import os
import tempfile
import hashlib
import json
import re
import resource
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
out = Path(os.environ.get('ECT_RESULTS_DIR', root / 'sim/results')) / 'latency_apb' / stamp
out.mkdir(parents=True, exist_ok=False)
work = Path(os.environ.get('ECT_SIMULATION_TMP', tempfile.gettempdir())) / ('t04_apb_' + stamp)
work.mkdir(parents=True, exist_ok=False)
alias = Path('/tmp') / ('ae350_t04_apb_' + stamp)
alias.symlink_to(work, target_is_directory=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
sources = ['src/common/cdc/held_bus_cdc.sv', 'src/midi_decode/debug/midi_diag_tx.sv',
           'rtl/ae350_latency_apb.sv', 'sim/tb/ae350_latency_apb_tb.sv']
record = {'status': 'RUNNING', 'scope': __doc__, 'tests': {},
          'inputs': {f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in sources},
          'clock_period_ns': {'PCLK_fixture': 26, 'UART_production': 20, 'capture_fixture': 20.834},
          'uart_baud': 200000, 'uart_bit_clocks': 250,
          'limits': ['Functional APB/CDC/UART RTL only; no CPU instruction, DSP, board, Gowin PnR or STA claim.']}
record['inputs'][str(Path(__file__).relative_to(root))] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def save():
    (out / 'validation.json').write_text(json.dumps(record, indent=2) + '\n')


def run(name, command):
    result = subprocess.run(list(map(str, command)), cwd=alias, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    (out / (name + '.log')).write_text(result.stdout)
    record['tests'][name] = {'command': list(map(str, command)), 'exit_code': result.returncode}
    save()
    return result


try:
    files = []
    for f in sources:
        target = work / 'source' / f
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / f, target)
        snapshot = out / 'source' / f
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / f, snapshot)
        files.append(alias / 'source' / f)
    version = run('version', [os.environ.get('VERILATOR', 'verilator'), '--version'])
    record['tool_version'] = version.stdout.strip()
    build = run('build', [os.environ.get('VERILATOR', 'verilator'), '--binary', '--timing', '--assert',
                         '-Wall', '-Wno-fatal', '--top-module', 'ae350_latency_apb_tb',
                         '--Mdir', alias / 'obj', '-j', '2', *files, '-o', 'sim'])
    critical = re.findall(r'^%Warning-(?:LATCH|UNOPTFLAT|IMPLICIT|SELRANGE|MULTIDRIVEN|WIDTH(?:TRUNC|EXPAND)?):.*$', build.stdout, re.M)
    if build.returncode or critical:
        raise RuntimeError('Compile failed or critical warnings: ' + repr(critical))
    sim = run('simulation', [alias / 'obj/sim'])
    passed = re.search(r'PASS ae350_latency_apb: (\d+) checks, (\d+) physical UART bytes', sim.stdout)
    if sim.returncode or not passed:
        raise RuntimeError('Functional verification failed')
    record['checks'], record['uart_bytes'] = map(int, passed.groups())
    record['inputs_unchanged'] = all(hashlib.sha256((root / f).read_bytes()).hexdigest() == value
                                    for f, value in record['inputs'].items())
    if not record['inputs_unchanged']:
        raise RuntimeError('Tested source changed')
    record['status'] = 'PASS'
except Exception as exc:
    record['status'] = 'FAIL'
    record['error'] = str(exc)
finally:
    if record['status'] == 'PASS':
        files = [p for p in work.rglob('*') if p.is_file()]
        record['cleanup'] = {'directory': str(work), 'files': len(files),
                             'bytes': sum(p.stat().st_size for p in files),
                             'removed': True, 'retained_sources_and_logs': str(out)}
        shutil.rmtree(work)
        alias.unlink()
    else:
        record['cleanup'] = {'directory': str(work), 'removed': False, 'alias': str(alias),
                             'reason': 'Retain failure evidence and debug executable.'}
    save()
print(json.dumps({'status': record['status'], 'results': str(out),
                  'checks': record.get('checks'), 'error': record.get('error')}))
sys.exit(record['status'] != 'PASS')
