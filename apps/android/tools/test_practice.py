#!/usr/bin/env python3
"""Deterministic practice timing tests, compiled without Android or transport classes."""
import pathlib
import shlex
import subprocess
import tempfile

from toolchain import ROOT, JDK_BIN


def main():
    evidence = ROOT / 'build/practice-core'
    evidence.mkdir(parents=True, exist_ok=True)
    package = pathlib.Path('cn/fpgasynth/control/core')
    sources = [ROOT / 'core/src/main/java' / package / name
               for name in ('PracticeSong.java', 'PracticeSession.java')]
    sources.append(ROOT / 'core/src/test/java' / package / 'PracticeSessionTest.java')
    with tempfile.TemporaryDirectory(prefix='classes-', dir=evidence) as folder:
        commands = [[JDK_BIN / 'javac', '--release', '8', '-encoding', 'UTF-8',
                     '-Xlint:all', '-Werror', '-d', folder, *sources],
                    [JDK_BIN / 'java', '-ea', '-cp', folder,
                     'cn.fpgasynth.control.core.PracticeSessionTest']]
        for command in commands:
            print('$ ' + shlex.join(map(str, command)), flush=True)
            result = subprocess.run(list(map(str, command)), check=False)
            print('exit=' + str(result.returncode), flush=True)
            if result.returncode:
                return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
