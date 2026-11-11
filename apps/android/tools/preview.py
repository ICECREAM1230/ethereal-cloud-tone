#!/usr/bin/env python3
"""Dedicated emulator setup/run/test. Reject physical device serials."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import zipfile
import urllib.request
from build import APK

ROOT=pathlib.Path(__file__).resolve().parents[1]
LOCAL=ROOT/'.toolchain'
ADB=LOCAL/'platform-tools/platform-tools/adb'
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['setup','run','test','stop']);p.add_argument('--port',type=int,default=5564);p.add_argument('--mock',action='store_true');args=p.parse_args()
    if args.port<5554 or args.port>5682 or args.port%2:raise SystemExit('choose an even emulator port 5554..5682')
    serial='emulator-'+str(args.port)
    if args.action=='setup':
        for package in json.loads((ROOT/'tools/preview-toolchain-lock.json').read_text()):
            name=package['name'];dest=LOCAL/name
            if dest.exists() and any(dest.iterdir()):continue
            file=LOCAL/(name+'.zip');print('Downloading preview:',name,flush=True);urllib.request.urlretrieve(package['url'],file)
            if hashlib.sha256(file.read_bytes()).hexdigest()!=package['sha256']:raise SystemExit('checksum: '+name)
            dest.mkdir(parents=True,exist_ok=True)
            with zipfile.ZipFile(file) as archive:archive.extractall(dest)
            for f in dest.rglob('*'):
                if f.is_file() and f.suffix in ('','.so','.sh'):f.chmod(f.stat().st_mode|0o111)
        print('Preview toolchain ready; no physical device touched.');return
    def adb(*command,**options):return subprocess.run([str(ADB),'-P','5048','-s',serial,*command],check=True,**options)
    if args.action=='run':
        avd=LOCAL/'avd';instance=avd/'FPGASynthPreview.avd';instance.mkdir(parents=True,exist_ok=True)
        image=LOCAL/'system-image/x86_64'
        (instance/'config.ini').write_text('\n'.join(['AvdId=FPGASynthPreview','avd.ini.displayname=FPGA Synth Preview','hw.cpu.arch=x86_64','hw.cpu.ncore=2','hw.ramSize=2048','hw.gpu.enabled=yes','hw.gpu.mode=swiftshader_indirect','hw.keyboard=yes','hw.lcd.width=1080','hw.lcd.height=2400','hw.lcd.density=440','disk.dataPartition.size=2G','image.sysdir.1='+str(image),'tag.id=default','abi.type=x86_64','showDeviceFrame=no','skin.name=1080x2400','skin.path=1080x2400'])+'\n')
        (avd/'FPGASynthPreview.ini').write_text('avd.ini.encoding=UTF-8\npath='+str(instance)+'\ntarget=android-36\n')
        env=dict(os.environ,ANDROID_AVD_HOME=str(avd),ANDROID_USER_HOME=str(LOCAL/'android-user'),ANDROID_EMULATOR_HOME=str(LOCAL/'android-user'),ANDROID_ADB_SERVER_PORT='5048')
        subprocess.run([str(ADB),'-P','5048','start-server'],env=env,check=True)
        subprocess.run([str(LOCAL/'emulator/emulator/emulator'),'-avd','FPGASynthPreview','-port',str(args.port),'-no-window','-no-audio','-no-snapshot','-no-boot-anim','-gpu','swiftshader_indirect','-accel','on','-netdelay','none','-netspeed','full','-feature','-WiFiPacketStream'],env=env,check=True)
    elif args.action=='test':
        ready=subprocess.check_output([str(ADB),'-P','5048','-s',serial,'shell','getprop','sys.boot_completed'],text=True).strip()
        if ready!='1':raise SystemExit('emulator boot incomplete')
        subprocess.run(['python3',str(ROOT/'tools/build_smoke.py')],check=True)
        adb('install','-r',str(APK));adb('install','-r',str(ROOT/'build/smoke/smoke.apk'))
        command=['shell','am','instrument','-w']
        if args.mock:command+=['-e','endpoint','http://10.0.2.2:8765']
        command+=['cn.fpgasynth.control.tests/cn.fpgasynth.control.tests.NativeSmoke']
        output=subprocess.check_output([str(ADB),'-P','5048','-s',serial,*command],text=True)
        print(output)
        evidence=ROOT/'docs/validation';evidence.mkdir(parents=True,exist_ok=True);(evidence/'native-smoke.txt').write_text(output)
        if 'INSTRUMENTATION_RESULT: result=PASS' not in output:raise SystemExit('native smoke failed')
        images=ROOT/'docs/screenshots';images.mkdir(parents=True,exist_ok=True)
        adb('pull','/sdcard/Android/data/cn.fpgasynth.control/files/',str(images))
    else:adb('emu','kill')
if __name__=='__main__':main()
