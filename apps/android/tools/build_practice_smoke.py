#!/usr/bin/env python3
"""Build instrumentation only; never touches a device."""
import pathlib
source = pathlib.Path(__file__).with_name('build_ks_smoke.py').read_text()
source = source.replace('build/ks-smoke', 'build/practice-smoke').replace('cn.fpgasynth.control.ks.tests', 'cn.fpgasynth.control.practice.tests').replace('KsNativeSmoke', 'PracticeSmoke').replace('ks-tests.apk', 'practice-tests.apk')
exec(compile(source, __file__, 'exec'))
