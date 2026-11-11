#!/usr/bin/env python3
"""Bounded score-file policy checks, independent from Android or network access."""
import subprocess
from toolchain import ROOT, JDK_BIN

out=ROOT/'build/score-policy-classes'
out.mkdir(parents=True,exist_ok=True)
subprocess.run([str(JDK_BIN/'javac'),'--release','8','-encoding','UTF-8','-Xlint:all','-Werror','-d',str(out),
 str(ROOT/'core/src/main/java/cn/fpgasynth/control/core/ScoreFilePolicy.java'),
 str(ROOT/'core/src/test/java/cn/fpgasynth/control/core/ScoreFilePolicyTest.java')],check=True)
subprocess.run([str(JDK_BIN/'java'),'-ea','-cp',str(out),'cn.fpgasynth.control.core.ScoreFilePolicyTest'],check=True)
