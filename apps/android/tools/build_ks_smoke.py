#!/usr/bin/env python3
"""Build KS native instrumentation only; does not access any emulator or device.

Compile after tools/build.py (the 0.8.0 classes are sufficient for the RED run).
Install/run is an explicit, separate action owned by the caller.
"""
import shutil
import zipfile
from build import ROOT, JDK, ANDROID, TOOLS, run

out = ROOT / 'build/ks-smoke'
out.mkdir(parents=True, exist_ok=True)
(out / 'AndroidManifest.xml').write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="cn.fpgasynth.control.ks.tests"><uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36"/><application android:label="KS native tests"/><instrumentation android:name="cn.fpgasynth.control.tests.KsNativeSmoke" android:targetPackage="cn.fpgasynth.control"/></manifest>''', encoding='utf-8')
run([TOOLS / 'aapt2', 'link', '-I', ANDROID, '--manifest', out / 'AndroidManifest.xml', '-o', out / 'res.apk'])
for name in ('classes', 'dex'):
    path = out / name
    if path.exists():
        shutil.rmtree(path)  # Only this runner's reproducible compiler outputs.
    path.mkdir()
run([JDK / 'bin/javac', '--release', '8', '-encoding', 'UTF-8', '-classpath', str(ANDROID) + ':' + str(ROOT / 'build/classes'), '-d', out / 'classes', ROOT / 'tools/KsNativeSmoke.java'])
run([JDK / 'bin/jar', 'cf', out / 'tests.jar', '-C', out / 'classes', '.'])
run([TOOLS / 'd8', '--min-api', '26', '--lib', ANDROID, '--classpath', ROOT / 'build/classes.jar', '--output', out / 'dex', out / 'tests.jar'])
with zipfile.ZipFile(out / 'res.apk', 'a', compression=zipfile.ZIP_DEFLATED) as stream:
    for path in sorted((out / 'dex').glob('*.dex')):
        stream.write(path, path.name)
run([TOOLS / 'zipalign', '-f', '4', out / 'res.apk', out / 'aligned.apk'])
run([TOOLS / 'apksigner', 'sign', '--ks', ROOT / 'delivery/development.keystore', '--ks-key-alias', 'development', '--ks-pass', 'pass:android', '--out', out / 'ks-tests.apk', out / 'aligned.apk'])
print(out / 'ks-tests.apk')
