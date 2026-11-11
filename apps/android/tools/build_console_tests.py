#!/usr/bin/env python3
"""Build the reflection-based UI runner independently of the app build or frozen FM outputs."""
import argparse,pathlib,zipfile
from build import ROOT,JDK,ANDROID,TOOLS,run

p=argparse.ArgumentParser();p.add_argument('--pads',action='store_true');args=p.parse_args()
package='cn.fpgasynth.control'+('.pads' if args.pads else '')
out=ROOT/'build/console-smoke'/('pads' if args.pads else 'main');out.mkdir(parents=True,exist_ok=True)
manifest=out/'AndroidManifest.xml'
manifest.write_text(f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{package}.console.tests"><uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36"/><application android:label="Console Emulator Tests"/><instrumentation android:name="cn.fpgasynth.control.tests.ConsoleSmoke" android:targetPackage="{package}"/></manifest>''')
run([TOOLS/'aapt2','link','-I',ANDROID,'--manifest',manifest,'-o',out/'res.apk'])
for name in ['classes','dex']:(out/name).mkdir(exist_ok=True)
run([JDK/'bin/javac','--release','8','-encoding','UTF-8','-classpath',ANDROID,'-d',out/'classes',ROOT/'tools/ConsoleSmoke.java'])
run([JDK/'bin/jar','cf',out/'tests.jar','-C',out/'classes','.'])
run([TOOLS/'d8','--min-api','26','--lib',ANDROID,'--output',out/'dex',out/'tests.jar'])
with zipfile.ZipFile(out/'res.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
 for path in (out/'dex').glob('*.dex'):z.write(path,path.name)
run([TOOLS/'zipalign','-f','4',out/'res.apk',out/'aligned.apk'])
run([TOOLS/'apksigner','sign','--ks',ROOT/'delivery/development.keystore','--ks-key-alias','development','--ks-pass','pass:android','--out',out/'console-tests.apk',out/'aligned.apk'])
print(out/'console-tests.apk')
