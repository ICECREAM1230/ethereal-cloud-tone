#!/usr/bin/env python3
"""Compile only core sources and the direct MIDI session test, with private outputs."""
import argparse
import json
import subprocess
from fm_contract import core_sources
from toolchain import ROOT, JDK_BIN

parser = argparse.ArgumentParser()
parser.add_argument('--label', default='verification')
args = parser.parse_args()
if not args.label.replace('-', '').replace('_', '').isalnum():
    parser.error('label must contain only letters, digits, dash or underscore')
out = ROOT / 'build/direct-midi-session'
classes = out / 'classes'
classes.mkdir(parents=True, exist_ok=True)
commands = [
    [str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8', '-Xlint:all',
     '-d', str(classes), *[str(p) for p in core_sources()],
     str(ROOT / 'core/src/test/java/cn/fpgasynth/control/core/DirectMidiSessionTest.java')],
    [str(JDK_BIN / 'java'), '-ea', '-cp', str(classes),
     'cn.fpgasynth.control.core.DirectMidiSessionTest'],
]
statuses = []
for phase, command in zip(('compile', 'test'), commands):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / (args.label + '-' + phase + '.log')).write_text(result.stdout)
    statuses.append({'phase': phase, 'exit_status': result.returncode})
    print(result.stdout, end='')
    if result.returncode:
        break
(out / (args.label + '-status.json')).write_text(json.dumps(statuses, indent=2) + '\n')
raise SystemExit(statuses[-1]['exit_status'])
