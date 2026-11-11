#!/usr/bin/env python3
"""Build App-dependent, explicit-fixture AE350 instrumentation; no device access."""
import zipfile
from build import ROOT, JDK, ANDROID, TOOLS, run
out=ROOT/'build/ae350-smoke';out.mkdir(parents=True,exist_ok=True)
(out/'AndroidManifest.xml').write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="cn.fpgasynth.control.ae350.tests"><uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36"/><application android:label="AE350 fixture tests"/><instrumentation android:name="cn.fpgasynth.control.tests.Ae350Smoke" android:targetPackage="cn.fpgasynth.control"/></manifest>''')
run([TOOLS/'aapt2','link','-I',ANDROID,'--manifest',out/'AndroidManifest.xml','-o',out/'res.apk'])
for name in ('classes','dex'):(out/name).mkdir(exist_ok=True)
run([JDK/'bin/javac','--release','8','-encoding','UTF-8','-classpath',str(ANDROID)+':'+str(ROOT/'build/classes'),'-d',out/'classes',ROOT/'tools/Ae350Smoke.java'])
run([JDK/'bin/jar','cf',out/'tests.jar','-C',out/'classes','.'])
run([TOOLS/'d8','--min-api','26','--lib',ANDROID,'--classpath',ROOT/'build/classes.jar','--output',out/'dex',out/'tests.jar'])
with zipfile.ZipFile(out/'res.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
 for path in (out/'dex').glob('*.dex'):z.write(path,path.name)
 for name in ('capabilities.json','state-initial-client-ks.json'):z.write(ROOT/'contracts/ae350-service-20261102/fixtures'/name,'assets/ae350/'+name)
run([TOOLS/'zipalign','-f','4',out/'res.apk',out/'aligned.apk'])
run([TOOLS/'apksigner','sign','--ks',ROOT/'delivery/development.keystore','--ks-key-alias','development','--ks-pass','pass:android','--out',out/'ae350-tests.apk',out/'aligned.apk'])
print(out/'ae350-tests.apk')
