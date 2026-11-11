#!/usr/bin/env python3
import pathlib
import subprocess
import json
import sys
from toolchain import ROOT, JDK_BIN as JDK
from fm_contract import CONTRACT_DIRECTORY, check_fm_contract, core_sources
check_fm_contract()
OUT=ROOT/'build/test-classes'
OUT.mkdir(parents=True,exist_ok=True)
sources=core_sources()+[ROOT/'tools/RuntimeModel.java']+sorted((ROOT/'core/src/test/java').rglob('*.java'))
subprocess.run([str(JDK/'javac'),'-encoding','UTF-8','--release','8','-Xlint:all','-d',str(OUT),*[str(f) for f in sources]],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.SessionTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.ControlPortsTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.ConsoleInteractionTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.MotionSensorMathTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.MotionInstrumentTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.SensorMidiMapperTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.BleMidiPortTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.DirectMidiSessionTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.MidiPortIntegrationTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.PluckedInstrumentTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.PracticeSessionTest'],check=True)
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.ScoreFilePolicyTest'],check=True)
bundle=ROOT/CONTRACT_DIRECTORY
fixture=json.loads((bundle/'tests/stage2-fixtures.json').read_text())
# Java is typed; raw float/bool/string cases remain the S3 wire parser's responsibility.
fixture['invalid_java_uint32']=[v for v in fixture.pop('invalid_uint32') if type(v) is int]
fixture_path=ROOT/'build/fm-java-fixtures.json'
fixture_path.write_text(json.dumps(fixture,separators=(',',':'))+'\n')
subprocess.run([str(JDK/'java'),'-ea','-cp',str(OUT),'cn.fpgasynth.control.core.GeneratedContractTest',
                str(fixture_path),str(bundle/'manifest.json'),str(bundle/'schema/fm-control.schema.json')],check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'tools/test_fm_current.py')],check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'tools/test_ae350_service_capture.py')],check=True)
subprocess.run([sys.executable,'-B',str(ROOT/'tools/test_ae350.py')],check=True)
