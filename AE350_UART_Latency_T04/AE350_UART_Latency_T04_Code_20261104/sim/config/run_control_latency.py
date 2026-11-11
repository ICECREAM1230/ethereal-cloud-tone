#!/usr/bin/env python3
"""AE350 latency firmware host execution, offline decoder and RV32 object build.

No physical device, CPU instruction cycle or FPGA timing claims. Real C module
is executed with mocked ordered APB transactions and actual emitted bytes are
passed through the independent strict decoder.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,help='New result directory')
parser.add_argument('--work-dir',type=Path,help='New external temporary directory')
parser.add_argument('--rv32-prefix',default='riscv64-unknown-elf-')
parser.add_argument('--host-only',action='store_true',help='Run actual C/decoder checks; skip RV32 object check')
args=parser.parse_args()
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
out=args.output.resolve() if args.output else ROOT/'sim/results/ae350_t0t3_20261103/firmware'/stamp
out.mkdir(parents=True,exist_ok=False)
work=args.work_dir.resolve() if args.work_dir else out/'work'
work.mkdir(parents=True,exist_ok=False)
owned=['firmware/control_latency.c','firmware/control_latency.h','sim/tb/test_control_latency.c',
       'scripts/read_ae350_t0t3.py','sim/tb/test_ae350_t0t3.py','sim/config/run_control_latency.py']
record={'status':'RUNNING','scope':__doc__,'inputs':{},'commands':[],'cleanup':'NOT_DONE'}
for name in owned:
    data=(ROOT/name).read_bytes();record['inputs'][name]=hashlib.sha256(data).hexdigest()
    target=work/'source'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
with tarfile.open(out/'tested-sources.tar.gz','w:gz') as archive:
    for name in owned:archive.add(work/'source'/name,arcname=name)

def run(name,argv,expected=0):
    proc=subprocess.run(list(map(str,argv)),cwd=work,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
    (out/(name+'.log')).write_text(proc.stdout)
    record['commands'].append({'name':name,'command':list(map(str,argv)),'exit_code':proc.returncode,'expected_exit':expected})
    if proc.returncode!=expected:raise RuntimeError(name+' failed: '+proc.stdout[-5000:])
    return proc.stdout

try:
    src=work/'source'
    run('host-build',['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                     '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(src/'firmware'),
                     src/'firmware/control_latency.c',src/'sim/tb/test_control_latency.c','-o',work/'unit'])
    text=run('host-unit',[work/'unit',out])
    match=re.search(r'PASS control_latency checks=(\d+) sizeof_state=(\d+)',text)
    if not match:raise RuntimeError('host checks marker missing')
    record['host_checks']=int(match[1]);record['sizeof_state_host']=int(match[2])
    run('decoder-unit',['python3','-B',src/'sim/tb/test_ae350_t0t3.py','-v'])
    for engine in range(3):
        name='engine-'+str(engine)+'-first20'
        run(name,['python3','-B',src/'scripts/read_ae350_t0t3.py','--input',out/(name+'.bin'),
                  '--output',out/(name+'.json'),'--engine',('SUB','KS','FM')[engine]])
        decoded=json.loads((out/(name+'.json')).read_text())
        if decoded['unique_records']!=20 or decoded['successful_records']!=20 or decoded['errors']:
            raise RuntimeError('real C first20 emitted bytes did not pass')
    for name in ['negative-low32-carry','conflict-diagnostic','pop-error-diagnostic',
                 'partial-timeout','loss-with-immutable-row','u64-wrap']:
        run(name,['python3','-B',src/'scripts/read_ae350_t0t3.py','--input',out/(name+'.bin'),
                  '--output',out/(name+'.json')],expected=2)
    if not args.host_only:
        compiler=args.rv32_prefix+'gcc';prefix=args.rv32_prefix
        run('rv32-object',[compiler,'-march=rv32im_zicsr','-mabi=ilp32','-mcmodel=medany','-mno-relax',
                           '-msmall-data-limit=0','-Os','-ffreestanding','-fno-builtin','-fno-pic',
                           '-fno-stack-protector','-Wall','-Wextra','-Werror','-fstack-usage',
                           '-I'+str(src/'firmware'),'-c',src/'firmware/control_latency.c','-o',out/'control_latency.o'])
        nm=run('rv32-undefined-symbols',[prefix+'nm','-u',out/'control_latency.o'])
        if nm.strip():raise RuntimeError('unexpected RV32 runtime helper dependencies: '+nm)
        run('rv32-disassembly',[prefix+'objdump','-dr',out/'control_latency.o'])
        stack=out/'control_latency.su'
        record['rv32_stack_usage']=stack.read_text() if stack.exists() else None
    for name in owned:
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=record['inputs'][name]:
            raise RuntimeError('input changed during validation: '+name)
    record.update({'status':'PASS_HOST_ONLY' if args.host_only else 'PASS',
                   'target_validation':'not run (--host-only)' if args.host_only else 'RV32 object checks passed','raw_bytes_first20_each_engine':4400,'first20_each_engine':20,
                   'decoder_unit_tests':27,'cleanup':'Removed only this runner own scratch directory; raw serial, source archive, object/disassembly/stack and logs retained.'})
    shutil.rmtree(work)
except Exception as exc:
    record['status']='FAIL';record['error']=str(exc);record['scratch_retained']=str(work)
finally:
    record['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    (out/'validation.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'status':record['status'],'out':str(out),'error':record.get('error')}))
if record['status'] not in ('PASS','PASS_HOST_ONLY'):raise SystemExit(1)
