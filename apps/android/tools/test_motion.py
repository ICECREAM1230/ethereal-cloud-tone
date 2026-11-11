#!/usr/bin/env python3
"""Run the motion model on a plain JVM; no Android, radio, or device access."""
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
    evidence = ROOT / 'docs/motion-20261028'
    evidence.mkdir(parents=True, exist_ok=True)
    sources = [
        ROOT / 'core/src/main/java/cn/fpgasynth/control/core/MotionInstrument.java',
        ROOT / 'core/src/test/java/cn/fpgasynth/control/core/MotionInstrumentTest.java',
    ]
    # A private temporary directory prevents sharing mutable classes with APK builds.
    # Both a failing javac run and a failing assertion clean up only these classes.
    with tempfile.TemporaryDirectory(prefix='model-classes-', dir=evidence) as folder:
        out = pathlib.Path(folder)
        code = run([JDK_BIN / 'javac', '--release', '8', '-encoding', 'UTF-8',
                    '-Xlint:all', '-Werror', '-d', out, *sources])
        if code:
            return code
        return run([JDK_BIN / 'java', '-ea', '-cp', out,
                    'cn.fpgasynth.control.core.MotionInstrumentTest'])


if __name__ == '__main__':
    raise SystemExit(main())
