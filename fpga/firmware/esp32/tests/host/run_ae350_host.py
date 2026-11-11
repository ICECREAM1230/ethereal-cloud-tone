#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,resource,shutil,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--run',required=True);a=p.parse_args()
if not re.fullmatch(r'[A-Za-z0-9_-]+',a.run):p.error('unsafe run name')
r=Path(__file__).resolve().parents[2];out=r/'build/host-tests/results'/a.run;out.mkdir(parents=True,exist_ok=False)
work=r/'build/host-tests/work'/('s3_host_'+a.run);work.mkdir(parents=True,exist_ok=False)
flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=undefined','-fno-omit-frame-pointer','-I'+str(r/'tests/host/stubs'),'-I'+str(r/'main')]
common=['tests/host/fake_queue.c','tests/host/fake_runtime.c','main/event_bus.c','main/control_source.c']
owner=common+['main/patch.c','tests/host/fake_dds_presets.c','main/web_control.c','main/ks_params.c','main/ks_voice.c','main/fm_voice.c','tests/host/fake_fm_engine.c','tests/host/fake_ks_engine.c','main/web_raw_queue.c','main/midi_templates.c','main/parameter_cc.c']
tests={
 'ae350_engine':(1,['tests/host/test_ae350_engine.c','main/raw_midi.c','main/midi_template_link.c','main/ae350_control.c','main/fdc_control.c']+common),
 'ae350_fm32_transport':(1,['tests/host/test_ae350_fm32_transport.c','main/raw_midi.c','main/midi_template_link.c','main/ae350_control.c','main/fdc_control.c']+common),
 'ae350_fm_config':(1,['tests/host/test_ae350_fm_config.c','main/raw_midi.c','main/midi_template_link.c','main/ae350_control.c','main/fdc_control.c']+common),
 'fmc_reverb_coexist':(1,['tests/host/test_fmc_reverb_coexist.c','main/raw_midi.c','main/midi_template_link.c','main/ae350_control.c','main/fdc_control.c']+common),
 'ae350_spi':(1,['tests/host/test_ae350_spi.c','main/spi_regs.c','main/ae350_control.c','tests/host/fake_runtime.c','main/control_source.c']),
 'ae350_template_credit':(1,['tests/host/test_ae350_template_credit.c','main/spi_regs.c','main/ae350_control.c','tests/host/fake_runtime.c','main/control_source.c']),
 'ae350_owner':(1,['tests/host/test_ae350_owner.c']+owner),
 'legacy_raw':(0,['tests/host/test_raw_midi.c','main/raw_midi.c']),
 'legacy_template':(0,['tests/host/test_midi_template_link.c','main/midi_template_link.c','main/control_source.c'])}
record={'status':'RUNNING','tests':{},'inputs':{}}
for d in ['main','contract','tests/host']:
 for f in (r/d).rglob('*'):
  if f.is_file():record['inputs'][str(f.relative_to(r))]=hashlib.sha256(f.read_bytes()).hexdigest()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def save():(out/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
def run(name,cmd):
 x=subprocess.run([str(c) for c in cmd],cwd=r,capture_output=True,text=True,timeout=120,env={**__import__('os').environ,'UBSAN_OPTIONS':'halt_on_error=1'})
 (out/(name+'.log')).write_text(x.stdout+x.stderr);record['tests'][name]={'command':[str(c) for c in cmd],'exit_code':x.returncode};save();return x
try:
 run('compiler',['cc','--version'])
 for name,(cpu,files) in tests.items():
  build=run(name+'_compile',flags+['-DCONFIG_FPGA_AE350_CONTROL='+str(cpu)]+[r/f for f in files]+['-lm','-o',work/name])
  if build.returncode:raise RuntimeError(name+' compile failed')
  result=run(name,[work/name])
  if result.returncode or 'PASS' not in result.stdout:raise RuntimeError(name+' failed')
 for n,h in record['inputs'].items():
  if hashlib.sha256((r/n).read_bytes()).hexdigest()!=h:raise RuntimeError('input drift: '+n)
 record['status']='PASS'
except Exception as e:record['status']='FAIL';record['error']=str(e)
finally:
 files=[f for f in work.rglob('*') if f.is_file()];record['cleanup']={'directory':str(work),'files':len(files),'bytes':sum(f.stat().st_size for f in files)}
 shutil.rmtree(work);save()
print(json.dumps({'status':record['status'],'results':str(out),'error':record.get('error')}));sys.exit(record['status']!='PASS')
