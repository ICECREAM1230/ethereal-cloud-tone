#!/usr/bin/env python3
import subprocess
from toolchain import ROOT, JDK_BIN
from fm_contract import check_fm_contract, core_sources

check_fm_contract()
out = ROOT / 'build/midi-integration-tests'
out.mkdir(parents=True, exist_ok=True)
subprocess.run([str(JDK_BIN / 'javac'), '--release', '8', '-encoding', 'UTF-8', '-d', str(out),
                *map(str, core_sources()), str(ROOT / 'core/src/test/java/cn/fpgasynth/control/core/MidiPortIntegrationTest.java')], check=True)
subprocess.run([str(JDK_BIN / 'java'), '-ea', '-cp', str(out),
                'cn.fpgasynth.control.core.MidiPortIntegrationTest'], check=True)
