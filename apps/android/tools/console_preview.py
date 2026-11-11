#!/usr/bin/env python3
"""Task-owned headless emulator. Every device command pins a dedicated emulator serial."""
import argparse,json,os,pathlib,socket,subprocess
from toolchain import ROOT,CACHE
from build import VERSION

p=argparse.ArgumentParser();p.add_argument('action',choices=['start','state','stop','test','layout']);p.add_argument('--baseline',action='store_true');p.add_argument('--pads',action='store_true');p.add_argument('--profile',default='tablet');p.add_argument('--font-scale',choices=['1.0','1.5'],default='1.0');p.add_argument('--evidence',default='docs/console-20261028/validation');args=p.parse_args()
out=ROOT/'build/console-emulator';serial='emulator-5588';port='5054';adb=CACHE/'platform-tools/platform-tools/adb'
def call(*command):return subprocess.run([str(adb),'-P',port,'-s',serial,*command],check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout
if args.action=='start':
 for value in (5588,5589,5054):
  with socket.socket() as s:
   s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
   s.bind(('127.0.0.1',value))
 instance=out/'avd/Console.avd';instance.mkdir(parents=True)
 (instance/'config.ini').write_text('\n'.join(['AvdId=Console','avd.ini.displayname=Synth Console Tests','hw.cpu.arch=x86_64','hw.cpu.ncore=2','hw.ramSize=2048','hw.gpu.enabled=yes','hw.gpu.mode=swiftshader_indirect','hw.keyboard=yes','hw.lcd.width=1600','hw.lcd.height=1000','hw.lcd.density=240','disk.dataPartition.size=2G','image.sysdir.1='+str(CACHE/'system-image/x86_64'),'tag.id=default','abi.type=x86_64','showDeviceFrame=no','skin.name=1600x1000','skin.path=1600x1000'])+'\n')
 (out/'avd/Console.ini').write_text('avd.ini.encoding=UTF-8\npath='+str(instance)+'\ntarget=android-36\n')
 env=dict(os.environ,ANDROID_AVD_HOME=str(out/'avd'),ANDROID_USER_HOME=str(out/'android-user'),ANDROID_EMULATOR_HOME=str(out/'android-user'),ANDROID_ADB_SERVER_PORT=port)
 with (out/'adb.log').open('w') as log:subprocess.run([str(adb),'-P',port,'start-server'],env=env,check=True,stdout=log,stderr=subprocess.STDOUT)
 cmd=[str(CACHE/'emulator/emulator/emulator'),'-avd','Console','-port','5588','-no-window','-no-audio','-no-snapshot','-no-boot-anim','-gpu','swiftshader_indirect','-accel','on','-feature','-WiFiPacketStream']
 with (out/'emulator.log').open('w') as log:proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 (out/'runtime.json').write_text(json.dumps({'pid':proc.pid,'serial':serial,'adb_port':int(port),'command':cmd},ensure_ascii=False,indent=2)+'\n');print('Started',serial,'PID',proc.pid)
elif args.action=='state':print(call('shell','getprop','sys.boot_completed'))
elif args.action=='stop':print(call('emu','kill'))
elif args.action=='layout':
 sizes={'tablet':('1600x1000','240'),'phone':('1125x2400','480'),'phone-landscape':('1280x720','240')}
 if args.profile not in sizes:raise SystemExit('Choose tablet, phone or phone-landscape')
 size,density=sizes[args.profile]
 print(call('shell','wm','size',size));print(call('shell','wm','density',density));print(call('shell','settings','put','system','font_scale',args.font_scale))
else:
 if call('shell','getprop','sys.boot_completed').strip()!='1':raise SystemExit('Dedicated emulator not booted')
 build=['python3',str(ROOT/'tools/build_console_tests.py')]+(['--pads'] if args.pads else []);subprocess.run(build,check=True)
 apk=ROOT/'delivery'/('fpga-synth-0.3.0.apk' if args.baseline else 'fpga-synth-'+VERSION+('-pads' if args.pads else '')+'.apk')
 test=ROOT/'build/console-smoke'/('pads' if args.pads else 'main')/'console-tests.apk'
 print(call('install','--no-incremental','-r',str(apk)));print(call('install','--no-incremental','-r',str(test)))
 package='cn.fpgasynth.control'+('.pads' if args.pads else '')
 result=call('shell','am','instrument','-w','-e','profile',args.profile,package+'.console.tests/cn.fpgasynth.control.tests.ConsoleSmoke');print(result)
 evidence=ROOT/args.evidence;evidence.mkdir(parents=True,exist_ok=True)
 (evidence/('native-'+args.profile+('-pads' if args.pads else '')+('-red' if args.baseline else '')+'.log')).write_text(result)
 if args.baseline:
  if 'INSTRUMENTATION_RESULT: result=FAIL' not in result:raise SystemExit('Expected RED not observed')
 if not args.baseline:
  dest=evidence/('screenshots-'+args.profile+('-pads' if args.pads else ''));dest.mkdir(exist_ok=True)
  print(call('pull','/sdcard/Android/data/'+package+'/files/',str(dest)))
  if 'INSTRUMENTATION_RESULT: result=PASS' not in result:raise SystemExit('Console native test failed')
