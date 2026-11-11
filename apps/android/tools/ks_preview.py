#!/usr/bin/env python3
"""KS native UI checks on a task-owned emulator; never targets a physical phone."""
import argparse
import json
import os
import socket
import subprocess
from toolchain import ROOT, CACHE
from build import VERSION

parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['start', 'state', 'test', 'stop'])
parser.add_argument('--profile', choices=['tablet', 'phone', 'phone-landscape'], default='tablet')
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
out = ROOT / 'build/ks-emulator'
serial, port = 'emulator-5598', '5064'
adb = CACHE / 'platform-tools/platform-tools/adb'
def call(*command):
    return subprocess.check_output([str(adb), '-P', port, '-s', serial, *map(str, command)], text=True, stderr=subprocess.STDOUT, timeout=120)
if args.action == 'start':
    if (out / 'runtime.json').exists():
        raise SystemExit('Existing runtime preserved; inspect before reuse')
    for value in (5598, 5599, 5064):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', value))
    instance = out / 'avd/KS.avd'
    instance.mkdir(parents=True)
    (instance / 'config.ini').write_text('\n'.join(['AvdId=KS', 'avd.ini.displayname=KS Native UI', 'hw.cpu.arch=x86_64', 'hw.cpu.ncore=2', 'hw.ramSize=1536', 'hw.gpu.enabled=yes', 'hw.gpu.mode=swiftshader_indirect', 'hw.keyboard=yes', 'hw.lcd.width=1600', 'hw.lcd.height=1000', 'hw.lcd.density=240', 'disk.dataPartition.size=2G', 'image.sysdir.1=' + str(CACHE / 'system-image/x86_64'), 'tag.id=default', 'abi.type=x86_64', 'showDeviceFrame=no', 'skin.name=1600x1000', 'skin.path=1600x1000']) + '\n')
    (out / 'avd/KS.ini').write_text('avd.ini.encoding=UTF-8\npath=' + str(instance) + '\ntarget=android-36\n')
    env = dict(os.environ, ANDROID_AVD_HOME=str(out / 'avd'), ANDROID_USER_HOME=str(out / 'android-user'), ANDROID_EMULATOR_HOME=str(out / 'android-user'), ANDROID_ADB_SERVER_PORT=port)
    with (out / 'adb.log').open('w') as log:
        subprocess.run([str(adb), '-P', port, 'start-server'], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
    command = [str(CACHE / 'emulator/emulator/emulator'), '-avd', 'KS', '-port', '5598', '-no-window', '-no-audio', '-no-snapshot', '-no-boot-anim', '-gpu', 'swiftshader_indirect', '-accel', 'on', '-feature', '-WiFiPacketStream']
    with (out / 'emulator.log').open('w') as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (out / 'runtime.json').write_text(json.dumps({'pid': process.pid, 'serial': serial, 'adb_port': int(port), 'command': command}, indent=2) + '\n')
    print('Started', serial, 'PID', process.pid)
elif args.action == 'state':
    print(call('shell', 'getprop', 'sys.boot_completed'))
elif args.action == 'stop':
    print(call('emu', 'kill'))
else:
    assert call('shell', 'getprop', 'sys.boot_completed').strip() == '1'
    size, density = {'tablet': ('1600x1000', '240'), 'phone': ('1125x2400', '480'), 'phone-landscape': ('1280x720', '240')}[args.profile]
    call('shell', 'wm', 'size', size)
    call('shell', 'wm', 'density', density)
    call('shell', 'settings', 'put', 'system', 'font_scale', '1.0' if args.profile == 'tablet' else '1.5')
    apk = ROOT / 'delivery' / ('fpga-synth-0.8.0.apk' if args.baseline else 'fpga-synth-' + VERSION + '.apk')
    print(call('install', '--no-incremental', '-r', apk))
    print(call('install', '--no-incremental', '-r', ROOT / 'build/ks-smoke/ks-tests.apk'))
    print(call('shell', 'pm', 'clear', 'cn.fpgasynth.control'))
    result = call('shell', 'am', 'instrument', '-w', '-e', 'profile', args.profile, 'cn.fpgasynth.control.ks.tests/cn.fpgasynth.control.tests.KsNativeSmoke')
    evidence = ROOT / 'docs/ks-native-20261103/native'
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / (args.profile + ('-red' if args.baseline else '') + '.txt')).write_text(result)
    print(result)
    if args.baseline:
        assert 'INSTRUMENTATION_RESULT: result=FAIL' in result and 'plucked' in result
    else:
        assert 'INSTRUMENTATION_RESULT: result=PASS' in result
        shots = evidence / ('screenshots-' + args.profile)
        shots.mkdir(exist_ok=True)
        print(call('pull', '/sdcard/Android/data/cn.fpgasynth.control/files/', shots))
