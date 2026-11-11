#!/usr/bin/env python3
"""Exercise App-only control ports; no radio, device, firmware or emulator access."""
import subprocess
from toolchain import ROOT, JDK_BIN
from fm_contract import check_fm_contract, core_sources

check_fm_contract()
out = ROOT / 'build/control-ports/classes'
out.mkdir(parents=True, exist_ok=True)
sources = core_sources() + [ROOT / 'core/src/test/java/cn/fpgasynth/control/core/ControlPortsTest.java']
subprocess.run([str(JDK_BIN/'javac'), '-encoding', 'UTF-8', '--release', '8', '-Xlint:all',
                '-d', str(out), *map(str, sources)], check=True)
subprocess.run([str(JDK_BIN/'java'), '-ea', '-cp', str(out),
                'cn.fpgasynth.control.core.ControlPortsTest'], check=True)
