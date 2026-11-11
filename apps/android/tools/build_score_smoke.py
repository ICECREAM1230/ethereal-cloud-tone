#!/usr/bin/env python3
"""Build score instrumentation only; never touches a device."""
import pathlib
source = pathlib.Path(__file__).with_name('build_ks_smoke.py').read_text()
source = source.replace('build/ks-smoke', 'build/score-smoke').replace('cn.fpgasynth.control.ks.tests', 'cn.fpgasynth.control.score.tests').replace('KsNativeSmoke', 'ScoreSmoke').replace('ks-tests.apk', 'score-tests.apk')
exec(compile(source, __file__, 'exec'))
