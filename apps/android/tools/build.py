#!/usr/bin/env python3
"""Native Android build with official AAPT2, javac, D8, zipalign and apksigner."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import zipfile
import argparse
import xml.etree.ElementTree as ET
from toolchain import ROOT, JDK, TOOLS, ANDROID
from fm_contract import check_fm_contract, core_sources

BUILD = ROOT/'build'
VERSION = '0.10.0'
APK = ROOT/('delivery/fpga-synth-'+VERSION+'.apk')

def run(args):
    env = dict(os.environ, JAVA_HOME=str(JDK), PATH=str(JDK/'bin')+os.pathsep+os.environ.get('PATH',''))
    subprocess.run([str(a) for a in args], cwd=ROOT, env=env, check=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--variant',choices=['main','pads'],default='main');args=parser.parse_args()
    pads=args.variant=='pads';build=ROOT/'build/pads' if pads else BUILD
    apk=ROOT/('delivery/fpga-synth-'+VERSION+('-pads' if pads else '')+'.apk')
    package='cn.fpgasynth.control'+('.pads' if pads else '')
    check_fm_contract()
    if not ANDROID.exists() or not (JDK/'bin/javac').exists():
        raise SystemExit('Run python3 tools/setup.py or set FPGA_APP_TOOLCHAIN to the pinned installed toolchain')
    build.mkdir(parents=True,exist_ok=True)
    apk.parent.mkdir(parents=True,exist_ok=True)
    for folder in ('classes','generated','dex'):
        path=build/folder
        if path.exists(): shutil.rmtree(path)
        path.mkdir()
    manifest=ROOT/'app/src/main/AndroidManifest.xml'
    if pads:
        ET.register_namespace('android','http://schemas.android.com/apk/res/android');tree=ET.parse(manifest);tree.getroot().set('package',package)
        app=tree.find('application');app.set('{http://schemas.android.com/apk/res/android}label','掌心合成器 · Pad备选版')
        for activity in app.findall('activity'):
            key='{http://schemas.android.com/apk/res/android}name';name=activity.get(key)
            if name.startswith('.'):activity.set(key,'cn.fpgasynth.control'+name)
        manifest=build/'AndroidManifest.xml';tree.write(manifest,encoding='utf-8',xml_declaration=True)
    config=build/'generated/cn/fpgasynth/control/BuildConfig.java';config.parent.mkdir(parents=True,exist_ok=True)
    config.write_text('package cn.fpgasynth.control; public final class BuildConfig {public static final boolean PAD_EDITION='+str(pads).lower()+';public static final String VERSION="'+VERSION+'";private BuildConfig(){}}\n')
    run([TOOLS/'aapt2','compile','--dir',ROOT/'app/src/main/res','-o',build/'resources.zip'])
    run([TOOLS/'aapt2','link','-o',build/'resources.apk','-I',ANDROID,
         '--manifest',manifest,'--java',build/'generated',build/'resources.zip'])
    sources = core_sources()+sorted((ROOT/'app/src/main/java').rglob('*.java'))+sorted((build/'generated').rglob('*.java'))
    run([JDK/'bin/javac','-encoding','UTF-8','--release','8','-classpath',ANDROID,'-d',build/'classes',*sources])
    run([JDK/'bin/jar','cf',build/'classes.jar','-C',build/'classes','.'])
    run([TOOLS/'d8','--min-api','26','--lib',ANDROID,'--output',build/'dex',build/'classes.jar'])
    shutil.copyfile(build/'resources.apk',build/'unsigned.apk')
    with zipfile.ZipFile(build/'unsigned.apk','a',compression=zipfile.ZIP_DEFLATED) as z:
        for f in sorted((build/'dex').glob('*.dex')):z.write(f,f.name)
    run([TOOLS/'zipalign','-f','-p','4',build/'unsigned.apk',build/'aligned.apk'])
    key=ROOT/'delivery/development.keystore'
    if not key.exists():
        run([JDK/'bin/keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android',
             '-alias','development','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=FPGA Synth Development'])
    run([TOOLS/'apksigner','sign','--ks',key,'--ks-key-alias','development','--ks-pass','pass:android',
         '--key-pass','pass:android','--out',apk,build/'aligned.apk'])
    run([TOOLS/'apksigner','verify','--verbose','--print-certs',apk])
    run([TOOLS/'zipalign','-c','4',apk])
    digest=hashlib.sha256(apk.read_bytes()).hexdigest()
    (ROOT/'delivery/SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.name+'\n' for f in sorted((ROOT/'delivery').glob('fpga-synth-*.apk'))))
    (ROOT/('delivery/build-info-pads.json' if pads else 'delivery/build-info.json')).write_text(json.dumps({'version':VERSION,'package':package,'variant':args.variant,
        'minSdk':26,'targetSdk':36,'compileSdk':36,'buildTools':'36.0.0','java':'17.0.20.1+1',
        'signing':'development only; included development.keystore, password android',
        'apk_bytes':apk.stat().st_size,'apk_sha256':digest},indent=2)+'\n')
    print('APK:', apk, 'bytes:', apk.stat().st_size)

if __name__=='__main__':main()
