#!/usr/bin/env python3
"""Actual four-point capture RTL; synthetic asynchronous event stimuli only."""
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
import tarfile

ROOT = Path(__file__).resolve().parents[2]
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUT = Path(os.environ.get('ECT_RESULTS_DIR', ROOT / 'sim/results')) / 'latency_capture' / STAMP
WORK = Path(os.environ.get('ECT_SIMULATION_TMP', tempfile.gettempdir())) / ('ae350_t04_capture_' + STAMP)
ALIAS = Path('/tmp') / ('ae350_t04_capture_' + STAMP)
FILES = ['rtl/ae350_latency_capture.sv', 'rtl/midi_pcm_latency_probe.sv',
         'src/common/cdc/held_bus_cdc.sv', 'sim/tb/ae350_latency_capture_tb.sv']
OUT.mkdir(parents=True, exist_ok=False)
WORK.mkdir(parents=True, exist_ok=False)
ALIAS.symlink_to(WORK, target_is_directory=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
record = {'status': 'RUNNING', 'scope': __doc__, 'clock_period_ns': {'control': 20.834, 'CPU_APB': 20.0, 'audio': 40.686},
          'quality': 'Actual pcm_request_audio bit0 matched pair && !bit3 test tone; normal current producer0x37 and status variants0x25/0x35 checked. No literal equality.',
          'commands': [], 'inputs': {}, 'limits': [
              'Synthetic accepted USB packet/CPU BEGIN+ACK/playback events; no USB PHY, real C, DSP arithmetic or CPU instruction model.',
              'Single active row and single held result; too-fast notes visibly lose original IDs and sticky-fail the batch.',
              'T1/T2/T3 include coherent-event synchronizer latency; functional phase bound is not SDF/STA/board precision evidence.',
              'Counter running at48MHz approximate period20.834ns in fixture; nominal rate field48000000Hz.',
              'T0=0 presence is checked; full2^64 hardware counter wrap cannot be naturally simulated here. Host arithmetic wrap is a separate check.']}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(name, argv):
    command = list(map(str, argv))
    with (OUT / (name + '.log')).open('w') as log:
        proc = subprocess.run(command, cwd=ALIAS, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    record['commands'].append({'name': name, 'command': command, 'exit_code': proc.returncode})
    data = (OUT / (name + '.log')).read_text()
    if proc.returncode:
        raise RuntimeError(name + ' failed: ' + data[-6000:])
    return data


try:
    for rel in FILES:
        record['inputs'][rel] = sha(ROOT / rel)
        target = WORK / 'source' / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / rel).read_bytes())
    record['inputs'][str(Path(__file__).relative_to(ROOT))] = sha(Path(__file__))
    record['version'] = run('version', [os.environ.get('VERILATOR', 'verilator'), '--version']).strip()
    build = run('build', [os.environ.get('VERILATOR', 'verilator'), '--binary', '--timing', '--assert',
                          '-Wall', '-Wno-fatal', '--top-module', 'ae350_latency_capture_tb',
                          '--Mdir', ALIAS / 'obj', '-j', '2'] + [ALIAS / 'source' / rel for rel in FILES] + ['-o', 'sim'])
    critical = re.findall(r'^%Warning-(?:LATCH|UNOPTFLAT|IMPLICIT|SELRANGE|MULTIDRIVEN):.*$', build, re.M)
    record['critical_warnings'] = critical
    if critical:
        raise RuntimeError('Critical RTL diagnostic: ' + '\n'.join(critical))
    log = run('simulation', [ALIAS / 'obj/sim'])
    passed = re.search(r'PASS AE350 latency capture checks=(\d+) scenarios=(\d+) normal_rows=(\d+) independent_events=(\d+) min_delay_ns=([\d.]+) max_delay_ns=([\d.]+)', log)
    if not passed:
        raise RuntimeError('Expected nonempty functional checks missing')
    record.update(dict(zip(['checks', 'scenarios', 'normal_rows', 'independent_events'], map(int, passed.groups()[:4]))))
    record['observation_delay_ns'] = {'min': float(passed[5]), 'max': float(passed[6])}
    if record['normal_rows'] != 60 or record['independent_events'] != 180:
        raise RuntimeError('Expected SUB/KS/FM20 rows and180 independent stage event bounds')
    shutil.copy2(WORK / 'capture_records.csv', OUT / 'capture_records.csv')
    if any(sha(ROOT / rel) != value for rel, value in record['inputs'].items()):
        raise RuntimeError('Test source drift during validation')
    with tarfile.open(OUT / 'validated-sources.tar.gz', 'w:gz') as archive:
        for rel in FILES:
            archive.add(WORK / 'source' / rel, arcname=rel)
        archive.add(Path(__file__), arcname=str(Path(__file__).relative_to(ROOT)))
    record['status'] = 'PASS'
except Exception as error:
    record['status'] = 'FAIL'
    record['error'] = str(error)
finally:
    record['scratch'] = str(WORK)
    record['cleanup'] = 'Owned successful scratch and alias removed; validation/logs/raw rows/source archive retained.' if record['status'] == 'PASS' else 'Failure scratch and alias retained for diagnosis.'
    if record['status'] == 'PASS':
        ALIAS.unlink()
        shutil.rmtree(WORK)
    (OUT / 'validation.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'status': record['status'], 'out': str(OUT), 'checks': record.get('checks'), 'error': record.get('error')}, ensure_ascii=False))
raise SystemExit(0 if record['status'] == 'PASS' else 1)
