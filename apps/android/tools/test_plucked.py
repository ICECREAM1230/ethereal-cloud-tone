#!/usr/bin/env python3
"""Transport-free KS pulse tests, using private disposable JVM classes."""
import pathlib
import shlex
import subprocess
import tempfile

from toolchain import ROOT, JDK_BIN


def main():
    evidence = ROOT / 'build/ks-native-component'
    evidence.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / 'core/src/main/java/cn/fpgasynth/control/core/PluckedInstrument.java',
               ROOT / 'core/src/test/java/cn/fpgasynth/control/core/PluckedInstrumentTest.java']
    with tempfile.TemporaryDirectory(prefix='model-classes-', dir=evidence) as folder:
        commands = [[JDK_BIN / 'javac', '--release', '8', '-encoding', 'UTF-8',
                     '-Xlint:all', '-Werror', '-d', pathlib.Path(folder), *sources],
                    [JDK_BIN / 'java', '-ea', '-cp', folder,
                     'cn.fpgasynth.control.core.PluckedInstrumentTest']]
        for command in commands:
            print('$ ' + shlex.join(map(str, command)), flush=True)
            result = subprocess.run(list(map(str, command)), check=False)
            print('exit=' + str(result.returncode), flush=True)
            if result.returncode:
                return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
