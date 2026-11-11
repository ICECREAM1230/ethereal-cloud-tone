#!/usr/bin/env python3
"""Plain-JVM standard MIDI data, sensor mapping and legacy control adapter tests."""
import pathlib
import shlex
import subprocess
import tempfile

from toolchain import ROOT, JDK_BIN


def run(command):
    print('$ ' + shlex.join(map(str, command)), flush=True)
    result = subprocess.run(list(map(str, command)), check=False)
    print('exit=' + str(result.returncode), flush=True)
    return result.returncode


def main():
    evidence = ROOT / 'docs/sensor-midi-20261028'
    evidence.mkdir(parents=True, exist_ok=True)
    base = ROOT / 'core/src/main/java/cn/fpgasynth/control/core'
    sources = [base / (name + '.java') for name in
               ('MotionInstrument', 'MidiMessage', 'MidiOutput',
                'SensorMidiMapper', 'MidiControlAdapter')]
    sources.append(ROOT / 'core/src/test/java/cn/fpgasynth/control/core/SensorMidiMapperTest.java')
    with tempfile.TemporaryDirectory(prefix='model-classes-', dir=evidence) as folder:
        out = pathlib.Path(folder)
        code = run([JDK_BIN / 'javac', '--release', '8', '-encoding', 'UTF-8',
                    '-Xlint:all', '-Werror', '-d', out, *sources])
        if code:
            return code
        return run([JDK_BIN / 'java', '-ea', '-cp', out,
                    'cn.fpgasynth.control.core.SensorMidiMapperTest'])


if __name__ == '__main__':
    raise SystemExit(main())
