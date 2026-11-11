#!/usr/bin/env python3
"""Pure-Java interaction behavior. Uses only the classes under test, not FM build outputs."""
import subprocess
from toolchain import ROOT, JDK_BIN

out=ROOT/'build/console-test/classes'
out.mkdir(parents=True,exist_ok=True)
names=['ControlGesture','PerformanceFeedback','EnvelopeShape']
sources=[ROOT/'core/src/main/java/cn/fpgasynth/control/core'/f'{name}.java' for name in names]
sources.append(ROOT/'core/src/test/java/cn/fpgasynth/control/core/ConsoleInteractionTest.java')
subprocess.run([str(JDK_BIN/'javac'),'--release','8','-encoding','UTF-8','-Xlint:all','-d',str(out),*map(str,sources)],check=True)
subprocess.run([str(JDK_BIN/'java'),'-ea','-cp',str(out),'cn.fpgasynth.control.core.ConsoleInteractionTest'],check=True)
