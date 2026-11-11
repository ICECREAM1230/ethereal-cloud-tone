#!/usr/bin/env python3
"""AE350 route regression against frozen production C serializer shapes."""
import argparse
import subprocess
from toolchain import ROOT, JDK_BIN
from fm_contract import core_sources, check_fm_contract

p = argparse.ArgumentParser()
p.add_argument('--fixtures', default=str(ROOT/'contracts/ae350-service-20261102/fixtures'))
args = p.parse_args()
check_fm_contract()
out = ROOT/'build/ae350-tests'
out.mkdir(parents=True, exist_ok=True)
subprocess.run([str(JDK_BIN/'javac'), '--release', '8', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', str(out),
                *map(str, core_sources()), str(ROOT/'core/src/test/java/cn/fpgasynth/control/core/Ae350RoutingTest.java')], check=True)
subprocess.run([str(JDK_BIN/'java'), '-ea', '-cp', str(out), 'cn.fpgasynth.control.core.Ae350RoutingTest', args.fixtures], check=True)
