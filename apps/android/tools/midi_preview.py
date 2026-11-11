#!/usr/bin/env python3
"""Dedicated-emulator direct MIDI UI checks; never selects a physical Android device."""
import argparse
import subprocess
import sys
from build import VERSION
from toolchain import ROOT, CACHE

parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['start', 'state', 'test', 'stop'])
parser.add_argument('--profile', choices=['tablet', 'phone', 'phone-landscape'], default='tablet')
args = parser.parse_args()
if args.action != 'test':
    raise SystemExit(subprocess.run([sys.executable, '-B', str(ROOT / 'tools/ae350_preview.py'), args.action]).returncode)
adb = [str(CACHE / 'platform-tools/platform-tools/adb'), '-P', '5062', '-s', 'emulator-5596']
def call(*command):
    return subprocess.check_output([*adb, *map(str, command)], text=True, timeout=90)
assert call('shell', 'getprop', 'sys.boot_completed').strip() == '1'
size, density = {'tablet': ('1600x1000', '240'), 'phone': ('1125x2400', '480'), 'phone-landscape': ('1280x720', '240')}[args.profile]
call('shell', 'wm', 'size', size)
call('shell', 'wm', 'density', density)
call('shell', 'settings', 'put', 'system', 'font_scale', '1.0' if args.profile == 'tablet' else '1.5')
print(call('install', '--no-incremental', '-r', ROOT / 'delivery' / ('fpga-synth-' + VERSION + '.apk')))
print(call('install', '--no-incremental', '-r', ROOT / 'build/midi-smoke/midi-tests.apk'))
print(call('shell', 'pm', 'clear', 'cn.fpgasynth.control'))
result = call('shell', 'am', 'instrument', '-w', '-e', 'profile', args.profile, 'cn.fpgasynth.control.midi.tests/cn.fpgasynth.control.tests.MidiDirectSmoke')
print(result)
out = ROOT / 'docs/ble-midi-direct-20261102/native'
out.mkdir(exist_ok=True)
(out / (args.profile + '.txt')).write_text(result)
if 'INSTRUMENTATION_RESULT: result=PASS' not in result:
    raise SystemExit('Native MIDI UI test failed')
shots = out / ('screenshots-' + args.profile)
shots.mkdir(exist_ok=True)
print(call('pull', '/sdcard/Android/data/cn.fpgasynth.control/files/', shots))
