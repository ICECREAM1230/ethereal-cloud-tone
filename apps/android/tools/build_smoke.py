#!/usr/bin/env python3
"""Build signed instrumentation APK for an explicitly selected emulator only."""
import pathlib
import sys
import zipfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from build import ROOT,JDK,ANDROID,TOOLS,run
from fm_contract import check_fm_contract
check_fm_contract()
OUT=ROOT/'build/smoke'
OUT.mkdir(parents=True,exist_ok=True)
manifest=OUT/'AndroidManifest.xml'
manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="cn.fpgasynth.control.tests">
<uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36" />
<application android:label="FPGA Synth Emulator Tests" />
<instrumentation android:name="cn.fpgasynth.control.tests.NativeSmoke" android:targetPackage="cn.fpgasynth.control" android:label="Native smoke" />
<instrumentation android:name="cn.fpgasynth.control.tests.ControlPortsSmoke" android:targetPackage="cn.fpgasynth.control" android:label="Control ports" />
</manifest>''')
run([TOOLS/'aapt2','link','-I',ANDROID,'--manifest',manifest,'-o',OUT/'res.apk'])
for p in ('classes','dex'):(OUT/p).mkdir(exist_ok=True)
run([JDK/'bin/javac','--release','8','-encoding','UTF-8','-classpath',str(ANDROID)+':'+str(ROOT/'build/classes'),'-d',OUT/'classes',ROOT/'tools/NativeSmoke.java',ROOT/'tools/ControlPortsSmoke.java'])
run([JDK/'bin/jar','cf',OUT/'test.jar','-C',OUT/'classes','.'])
run([TOOLS/'d8','--min-api','26','--lib',ANDROID,'--classpath',ROOT/'build/classes.jar','--output',OUT/'dex',OUT/'test.jar'])
with zipfile.ZipFile(OUT/'res.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
    for p in (OUT/'dex').glob('*.dex'):z.write(p,p.name)
run([TOOLS/'zipalign','-f','4',OUT/'res.apk',OUT/'aligned.apk'])
run([TOOLS/'apksigner','sign','--ks',ROOT/'delivery/development.keystore','--ks-key-alias','development','--ks-pass','pass:android','--out',OUT/'smoke.apk',OUT/'aligned.apk'])
print(OUT/'smoke.apk')
