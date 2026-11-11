#!/usr/bin/env python3
"""Real USB transfer bank/packet CDC/ingress versus T04 observer padding regression.

Directed BEGIN/COMMIT-ACK/playback events; no USB PHY, actual CPU instructions,
DSP model, official implementation/STA, devices or claim about board USB payloads.
"""
from pathlib import Path
import argparse
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

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--label', default='check', choices=('check', 'red', 'green'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '_' + args.label
out = Path(os.environ.get('ECT_RESULTS_DIR', root / 'sim/results')) / 't04_usb_padding' / stamp
work = Path(os.environ.get('ECT_SIMULATION_TMP', tempfile.gettempdir())) / 'ect_t04_usb_padding' / stamp
out.mkdir(parents=True, exist_ok=False)
work.mkdir(parents=True, exist_ok=False)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
files = ['src/control_input/usb_midi/rtl/midi_packet_queue.sv',
         'src/control_input/usb_midi/midi_packet_cdc.sv',
         'src/midi_decode/parser/stream_fifo.sv', 'src/midi_decode/parser/usb_midi_ingress.sv',
         'control_migration/rtl/ae350_ingress_frontend.sv',
         'src/common/cdc/held_bus_cdc.sv', 'rtl/midi_pcm_latency_probe.sv',
         'rtl/ae350_latency_capture.sv', 'sim/tb/t04_usb_padding_tb.sv']
record = {'status': 'RUNNING', 'scope': __doc__, 'label': args.label, 'inputs': {}, 'commands': [],
          'expected': {'scenarios': 24, 'normal_rows': 9, 'invalid_rows': 15},
          'scratch': str(work), 'limits': [
              'Host transfer bank receives directed payload/length; actual physical USB payload of this board is not observed.',
              'Production bank/packet CDC/ingress/capture RTL are used unchanged.',
              'Same USB48/control48 clock net matches production clock_reset direct clock assignment.',
              'Startup flush completion is a fixture shim; CPU BEGIN/ACK and playback are directed events.',
              'Source backpressure delays word2 of a held transfer to place padding around BEGIN and ACK.',
              'Functional RTL only; no CPU instruction timing, hardware latency, official build or STA claim.']}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save():
    (out / 'validation.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')


def run(name, argv, timeout=240):
    entry = {'name': name, 'command': list(map(str, argv))}
    record['commands'].append(entry)
    save()
    with (out / (name + '.log')).open('w') as log:
        try:
            process = subprocess.run(entry['command'], cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
            entry['exit_code'] = process.returncode
        except subprocess.TimeoutExpired:
            entry.update(exit_code=None, timeout_seconds=timeout)
            raise
    save()
    text = (out / (name + '.log')).read_text(errors='replace')
    if entry['exit_code'] != 0:
        raise RuntimeError(name + ' exit=' + str(entry['exit_code']) + ': ' + text[-4000:])
    return text


try:
    for rel in files:
        record['inputs'][rel] = sha(root / rel)
        destination = work / 'source' / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / rel, destination)
    record['inputs'][str(Path(__file__).relative_to(root))] = sha(Path(__file__))
    record['version'] = run('version', [os.environ.get('VERILATOR', 'verilator'), '--version']).strip()
    built = run('build', [os.environ.get('VERILATOR', 'verilator'), '--binary', '--timing', '--assert',
                         '-Wall', '-Wno-fatal', '--top-module', 't04_usb_padding_tb',
                         '--Mdir', 'obj', '-j', '2'] + ['source/' + rel for rel in files] + ['-o', 'sim'])
    record['critical_warnings'] = re.findall(r'^%Warning-(?:LATCH|UNOPTFLAT|IMPLICIT|SELRANGE|MULTIDRIVEN):.*$', built, re.M)
    if record['critical_warnings']:
        raise RuntimeError('Critical RTL diagnostics: ' + '\n'.join(record['critical_warnings']))
    log = run('simulation', ['./obj/sim'])
    match = re.search(r'PASS T04_USB_PADDING checks=(\d+) scenarios=(\d+) normal_rows=(\d+) invalid_rows=(\d+)', log)
    if not match:
        raise RuntimeError('Expected complete functional check summary absent')
    record.update(dict(zip(('checks', 'scenarios', 'normal_rows', 'invalid_rows'), map(int, match.groups()))))
    if any(record[key] != value for key, value in record['expected'].items()):
        raise RuntimeError('Expected SUB/KS/FM padding and interference scenario coverage not completed')
    record['status'] = 'PASS'
except Exception as error:
    record.update(status='FAIL', error=str(error))
finally:
    drift = [rel for rel, digest in record['inputs'].items() if sha(root / rel) != digest]
    record['source_drift'] = drift
    if drift:
        record.update(status='FAIL', source_drift_error='Production/test input changed during run')
    raw = work / 'usb_words_bytes_rows.log'
    if raw.exists():
        shutil.copy2(raw, out / raw.name)
    with tarfile.open(out / 'validated-sources.tar.gz', 'w:gz') as archive:
        for rel in files:
            path = work / 'source' / rel
            if path.exists():
                archive.add(path, arcname=rel)
        archive.add(Path(__file__), arcname=str(Path(__file__).relative_to(root)))
    if record['status'] == 'PASS':
        shutil.rmtree(work)
        record['cleanup'] = 'Only this successful owned scratch directory removed; logs, raw evidence and source archive retained.'
    else:
        record['cleanup'] = 'Failure scratch retained for diagnosis; no files deleted.'
    save()
    print(json.dumps({'status': record['status'], 'out': str(out), 'checks': record.get('checks'),
                      'error': record.get('error')}, ensure_ascii=False))
raise SystemExit(0 if record['status'] == 'PASS' else 1)
