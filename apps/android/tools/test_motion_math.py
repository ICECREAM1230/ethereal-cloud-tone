#!/usr/bin/env python3
import subprocess
from toolchain import ROOT,JDK_BIN
out=ROOT/'build/motion-math';out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(JDK_BIN/'javac'),'--release','8','-d',str(out),str(ROOT/'core/src/main/java/cn/fpgasynth/control/core/MotionSensorMath.java'),str(ROOT/'core/src/test/java/cn/fpgasynth/control/core/MotionSensorMathTest.java')],check=True)
subprocess.run([str(JDK_BIN/'java'),'-ea','-cp',str(out),'cn.fpgasynth.control.core.MotionSensorMathTest'],check=True)
