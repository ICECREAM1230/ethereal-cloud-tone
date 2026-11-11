#!/usr/bin/env python3
"""Bounded offline validation of actual FDC/AE SPI, owner and HTTP sources."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[3];o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
work=r/'build/host-tests/work';work.mkdir(parents=True,exist_ok=True)
w=Path(tempfile.mkdtemp(prefix='esp32-fdc-host-',dir=work))
f=o/'fixtures';f.mkdir()
flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-g','-fsanitize=undefined','-fno-omit-frame-pointer','-I'+str(r/'tests/host/stubs'),'-I'+str(r/'main')]
owner=['tests/host/fake_queue.c','tests/host/fake_runtime.c','main/event_bus.c','main/patch.c','tests/host/fake_dds_presets.c','main/web_control.c','main/ks_params.c','main/ks_voice.c','main/fm_voice.c','tests/host/fake_fm_engine.c','tests/host/fake_ks_engine.c','main/web_raw_queue.c','main/midi_templates.c','main/parameter_cc.c']
http=['-std=gnu17','-ffunction-sections','-fdata-sections','-I'+str(r/'tests/host/http_stubs')]
tests={
 'allowlist':([],['tests/host/fdc/test_fdc_allowlist.c','main/ae350_control.c'],[]),
 'fdc_link':(['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/fdc/test_fdc_link.c','tests/host/fake_runtime.c','tests/host/fake_queue.c','main/fdc_control.c','main/ae350_control.c'],[]),
 'fdc_owner':([],['tests/host/fdc/test_fdc_owner.c','main/fdc_control.c']+owner,[]),
 'fdc_http':(http,['tests/host/fdc/test_fdc_http.c','main/fdc_control.c','main/web_control.c','main/patch.c','tests/host/fake_dds_presets.c'],['-Wl,--gc-sections','-lz']),
 'cached_fresh':(http+['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/fdc/test_fdc_cached_fresh.c','main/fdc_control.c','main/web_control.c','main/patch.c','tests/host/fake_dds_presets.c'],['-Wl,--gc-sections','-lz']),
 'ae_engine':(['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/test_ae350_engine.c','main/raw_midi.c','main/midi_template_link.c','main/fdc_control.c','main/ae350_control.c','main/event_bus.c','tests/host/fake_runtime.c','tests/host/fake_queue.c'],[]),
 'ae_spi':(['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/test_ae350_spi.c','main/spi_regs.c','main/ae350_control.c'],[]),
 'ae_credit':(['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/test_ae350_template_credit.c','main/spi_regs.c','main/ae350_control.c','tests/host/fake_runtime.c'],[]),
 'ae_owner':(['-DCONFIG_FPGA_AE350_CONTROL=1'],['tests/host/test_ae350_owner.c']+owner,[]),
 'legacy_raw':([],['tests/host/test_raw_midi.c','main/raw_midi.c'],[]),
 'legacy_template':([],['tests/host/test_midi_template_link.c','main/midi_template_link.c'],[]),
 'reverb_link':([],['tests/host/test_reverb_link.c','main/event_bus.c','tests/host/fake_runtime.c','tests/host/fake_queue.c'],[]),
 'reverb_owner':([],['tests/host/test_reverb_owner.c']+owner,[]),
 'reverb_http':(http,['tests/host/test_reverb_http.c','main/web_control.c','main/patch.c','tests/host/fake_dds_presets.c'],['-Wl,--gc-sections','-lz'])}
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(x.relative_to(r)):h(x) for directory in ['main','tests/host'] for x in (r/directory).rglob('*') if x.is_file() and 'results' not in x.relative_to(r).parts}
record={'status':'RUNNING','start':time.time(),'inputs':inputs,'tests':{}}
def save():(o/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
def run(name,cmd):
 x=subprocess.run([str(t) for t in cmd],cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120,env={**os.environ,'FDC_FIXTURE_DIR':str(f),'PYTHONDONTWRITEBYTECODE':'1'})
 (o/(name+'.log')).write_text(x.stdout);record['tests'][name]={'exit_code':x.returncode,'command':[str(t) for t in cmd]};save()
 if x.returncode:raise RuntimeError(name+' failed')
 return x
try:
 for name,(extra,files,link) in tests.items():
  run(name+'_compile',flags+extra+[r/x for x in files]+link+['-lm','-o',w/name])
  x=run(name,[w/name])
  if 'PASS' not in x.stdout:raise RuntimeError(name+' lacks PASS marker')
 record['input_drift']=[n for n,v in inputs.items() if h(r/n)!=v]
 if record['input_drift']:raise RuntimeError('input drift')
 record['status']='PASS'
except Exception as e:record['status']='FAIL';record['error']=str(e)
finally:
 record['finished']=time.time();record['cleanup']={'directory':str(w),'files':len(list(w.iterdir()))};shutil.rmtree(w);save()
print(json.dumps({'status':record['status'],'out':str(o),'error':record.get('error')}));raise SystemExit(record['status']!='PASS')
