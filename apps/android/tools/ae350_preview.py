#!/usr/bin/env python3
"""Task-owned headless AE350 fixture UI tests; every ADB operation pins our emulator."""
import argparse,json,os,socket,subprocess
from toolchain import ROOT,CACHE
from build import VERSION
p=argparse.ArgumentParser();p.add_argument('action',choices=['start','state','test','stop']);p.add_argument('--profile',choices=['tablet','phone','phone-landscape'],default='tablet');p.add_argument('--baseline',action='store_true');args=p.parse_args()
out=ROOT/'build/ae350-emulator';evidence=ROOT/'docs/ae350-service-20261102/validation/native';serial='emulator-5596';port='5062';adb=CACHE/'platform-tools/platform-tools/adb'
def call(*command,timeout=45):
 return subprocess.run([str(adb),'-P',port,'-s',serial,*command],check=True,text=True,capture_output=True,timeout=timeout).stdout
if args.action=='start':
 if (out/'runtime.json').exists():raise SystemExit('Existing instance record preserved; inspect/stop it before reuse')
 for value in (5596,5597,5062):
  with socket.socket() as s:s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('127.0.0.1',value))
 instance=out/'avd/AE350.avd';instance.mkdir(parents=True)
 (instance/'config.ini').write_text('\n'.join(['AvdId=AE350','avd.ini.displayname=AE350 App Fixture Tests','hw.cpu.arch=x86_64','hw.cpu.ncore=2','hw.ramSize=1536','hw.gpu.enabled=yes','hw.gpu.mode=swiftshader_indirect','hw.keyboard=yes','hw.lcd.width=1600','hw.lcd.height=1000','hw.lcd.density=240','disk.dataPartition.size=2G','image.sysdir.1='+str(CACHE/'system-image/x86_64'),'tag.id=default','abi.type=x86_64','showDeviceFrame=no','skin.name=1600x1000','skin.path=1600x1000'])+'\n')
 (out/'avd/AE350.ini').write_text('avd.ini.encoding=UTF-8\npath='+str(instance)+'\ntarget=android-36\n')
 env=dict(os.environ,ANDROID_AVD_HOME=str(out/'avd'),ANDROID_USER_HOME=str(out/'android-user'),ANDROID_EMULATOR_HOME=str(out/'android-user'),ANDROID_ADB_SERVER_PORT=port)
 with (out/'adb.log').open('w') as log:subprocess.run([str(adb),'-P',port,'start-server'],env=env,check=True,stdout=log,stderr=subprocess.STDOUT)
 cmd=[str(CACHE/'emulator/emulator/emulator'),'-avd','AE350','-port','5596','-no-window','-no-audio','-no-snapshot','-no-boot-anim','-gpu','swiftshader_indirect','-accel','on','-feature','-WiFiPacketStream']
 with (out/'emulator.log').open('w') as log:proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 (out/'runtime.json').write_text(json.dumps({'pid':proc.pid,'serial':serial,'adb_port':int(port),'command':cmd},indent=2)+'\n');print('Started',serial,'PID',proc.pid)
elif args.action=='state':print(call('shell','getprop','sys.boot_completed'))
elif args.action=='stop':print(call('emu','kill'))
else:
 if call('shell','getprop','sys.boot_completed').strip()!='1':raise SystemExit('Dedicated emulator has not booted')
 size,density={'tablet':('1600x1000','240'),'phone':('1125x2400','480'),'phone-landscape':('1280x720','240')}[args.profile]
 print(call('shell','wm','size',size));print(call('shell','wm','density',density));print(call('shell','settings','put','system','font_scale','1.0' if args.profile=='tablet' else '1.5'))
 apk=ROOT/'delivery'/('fpga-synth-0.6.0.apk' if args.baseline else 'fpga-synth-'+VERSION+'.apk')
 print(call('install','--no-incremental','-r',str(apk)));print(call('install','--no-incremental','-r',str(ROOT/'build/ae350-smoke/ae350-tests.apk')))
 print(call('shell','pm','clear','cn.fpgasynth.control'))
 result=call('shell','am','instrument','-w','-e','profile',args.profile,'cn.fpgasynth.control.ae350.tests/cn.fpgasynth.control.tests.Ae350Smoke');print(result)
 evidence.mkdir(parents=True,exist_ok=True);log=evidence/('native-'+args.profile+('-red' if args.baseline else '')+'.txt');log.write_text(result)
 if args.baseline:
  if 'INSTRUMENTATION_RESULT: result=FAIL' not in result or 'KS route highlighted' not in result:raise SystemExit('Expected original UI regression not reproduced')
 else:
  if 'INSTRUMENTATION_RESULT: result=PASS' not in result:raise SystemExit('Native AE350 fixture UI failed')
  dest=evidence/('screenshots-'+args.profile);dest.mkdir(exist_ok=True);print(call('pull','/sdcard/Android/data/cn.fpgasynth.control/files/',str(dest)))
