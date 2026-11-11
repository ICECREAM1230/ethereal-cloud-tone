#!/usr/bin/env python3
"""Focused actual HTTP boundary regression; no SDK, network or hardware writes."""
from pathlib import Path
import argparse,hashlib,json,re,resource,shutil,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--run',required=True);args=parser.parse_args()
if not re.fullmatch(r'[A-Za-z0-9_-]+',args.run):parser.error('simple unique run required')
root=Path(__file__).resolve().parents[2]
out=root.parent/'results/fm_ae350_parameter_port_20261101'/args.run
out.mkdir(parents=True,exist_ok=False)
work=Path('/media/ice/D;/FPGA_simulation/ae350_fm_http_20261101')/args.run
work.mkdir(parents=True,exist_ok=False)
# Main now shares the HTTP/BLE core and source lifecycle. Link those real
# production dependencies, preserving the existing HTTP boundary fixture.
linked=['main/web_control.c','main/control_source.c','main/patch.c',
        'tests/host/fake_dds_presets.c','main/web_raw_midi.c','main/web_raw_queue.c',
        'main/fdc_control.c','main/ks_timbre_control.c','main/ks_timbre_params.c',
        'main/ks_timbre_document.c','main/vendor/tomlc17/tomlc17.c','tests/host/fake_queue.c']
files=list(dict.fromkeys(linked+[str(f.relative_to(root))
    for directory in ('main','contract','tests/host')
    for f in (root/directory).rglob('*') if f.is_file() and '__pycache__' not in f.parts]))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
record={'scope':'actual HTTP handler C with observed CPU/FMC1 descriptor; SDK/socket/SPI/CPU execution not modeled',
        'inputs':{n:sha(root/n) for n in files},'commands':[],'status':'RUNNING'}
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def run(name,command):
    p=subprocess.run(list(map(str,command)),cwd=root,capture_output=True,text=True,timeout=120)
    (out/(name+'.log')).write_text(p.stdout+p.stderr)
    record['commands'].append({'command':list(map(str,command)),'exit_code':p.returncode})
    if p.returncode:raise RuntimeError(name+' failed')
try:
    run('compile',['cc','-std=gnu17','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
        '-fsanitize=undefined','-fno-omit-frame-pointer','-DCONFIG_FPGA_AE350_CONTROL=1',
        '-Itests/host/http_stubs','-Itests/host/stubs','-Imain','tests/host/test_ae350_fm_http.c',
        *linked,'-Wl,--gc-sections','-lm','-lz','-o',work/'http'])
    run('http',[work/'http',out/'capabilities.json',out/'state.json',out/'presets.json'])
    assert all(sha(root/n)==h for n,h in record['inputs'].items())
    record['status']='PASS'
except Exception as e:record.update(status='FAIL',error=str(e))
finally:
    record['cleanup']={'directory':str(work),'files':len(list(work.rglob('*')))}
    shutil.rmtree(work);(out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({'status':record['status'],'out':str(out),'error':record.get('error')}))
raise SystemExit(record['status']!='PASS')
