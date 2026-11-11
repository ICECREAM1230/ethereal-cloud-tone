#!/usr/bin/env python3
"""Host MMIO integration + RV32 object checks; no IP/FPGA build or device write."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess

ap=argparse.ArgumentParser()
ap.add_argument('--run',required=True)
ap.add_argument('--rv32-prefix',default='riscv64-unknown-elf-')
ap.add_argument('--output',type=Path,help='New result directory')
ap.add_argument('--work-dir',type=Path,help='New external temporary directory')
ap.add_argument('--host-only',action='store_true',help='Run native C checks; explicitly skip RV32 object checks')
a=ap.parse_args()
if not re.fullmatch(r'[A-Za-z0-9_-]+',a.run):ap.error('run must contain only letters, numbers, _ or -')
root=Path(__file__).resolve().parents[2]
out=a.output.resolve() if a.output else root/'control_migration/sim/results/control_service'/a.run
temp=a.work_dir.resolve() if a.work_dir else root/'tests/.work'/('control_service_'+a.run)
out.mkdir(parents=True,exist_ok=False);temp.mkdir(parents=True,exist_ok=False)
sources=[root/'firmware/control_service.c']+[root/'control_migration/firmware'/n for n in ('midi_parser.c','voice_manager.c','control_runtime.c')]
test=root/'control_migration/tests/test_control_service.c'
inputs=sources+[test,root/'firmware/control_service.h',root/'control_migration/include/control_event.h']
inputs += [root/'control_migration/firmware'/n for n in ('midi_parser.h','voice_manager.h','control_runtime.h')]
record={'scope':'real C service/parser/runtime/voice managers with injected ordered MMIO; no physical AE350/APB/PCM','tests':{},'inputs':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
includes=['-I'+str(root/'firmware'),'-I'+str(root/'control_migration/include')]
def run(name,cmd):
    result=subprocess.run(list(map(str,cmd)),cwd=temp,capture_output=True,text=True,timeout=60)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    record['tests'][name]={'command':list(map(str,cmd)),'exit_code':result.returncode}
    (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
    if result.returncode:raise SystemExit(name+' failed: '+result.stdout+result.stderr)
    return result.stdout
common=['-std=c11','-Wall','-Wextra','-Werror','-pedantic',*includes,*sources,test]
run('host-version',['gcc','--version'])
run('host-compile',['gcc',*common,'-o',temp/'test'])
print(run('host-tests',[temp/'test']).strip())
run('sanitizer-compile',['gcc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*common,'-o',temp/'sanitized'])
run('sanitizer-tests',[temp/'sanitized'])
run('analyzer',['gcc','-std=c11','-Wall','-Wextra','-Werror','-fanalyzer','-ffreestanding','-fno-builtin',*includes,'-fsyntax-only',*sources])
if not a.host_only:
 cc=a.rv32_prefix+'gcc';objects=[]
 flags=['-march=rv32im_zicsr','-mabi=ilp32','-mcmodel=medany','-mno-relax','-msmall-data-limit=0','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-std=c11','-Wall','-Wextra','-Werror']
 run('rv32-version',[cc,'--version'])
 for src in sources:
     obj=temp/(src.stem+'.o');objects.append(obj)
     run(src.stem+'-rv32',[cc,*flags,*includes,'-fstack-usage','-c',src,'-o',obj])
     shutil.copy2(obj.with_suffix('.su'),out/(src.stem+'-stack.txt'))
 run('rv32-link',[cc,*flags,'-nostdlib','-r','-Wl,--no-relax',*objects,'-o',temp/'control_service_bundle.o'])
 undefined=run('rv32-undefined',[a.rv32_prefix+'nm','-u',temp/'control_service_bundle.o'])
 if undefined.strip():raise SystemExit('Unexpected RV32 runtime dependency: '+undefined)
 run('rv32-size',[a.rv32_prefix+'size',temp/'control_service_bundle.o'])
 probe=temp/'sizeof.c';probe.write_text('#include "control_service.h"\nchar service_storage[sizeof(cs_service_t)];\n')
 run('rv32-sizeof-compile',[cc,*flags,*includes,'-c',probe,'-o',temp/'sizeof.o'])
 run('rv32-sizeof',[a.rv32_prefix+'nm','-S','--size-sort',temp/'sizeof.o'])
 dis=run('rv32-disassembly',[a.rv32_prefix+'objdump','-d',objects[0]])
 if 'fence' not in dis:raise SystemExit('Missing real-target MMIO ordering instruction')
for rel,sha in record['inputs'].items():
    if hashlib.sha256((root/rel).read_bytes()).hexdigest()!=sha:raise SystemExit('Input changed during validation: '+rel)
record['status']='PASS_HOST_ONLY' if a.host_only else 'PASS'
record['target_validation']='not run (--host-only)' if a.host_only else 'RV32 checks passed'
if not a.host_only:record['undefined_symbols']=[]
record['cleanup']={'directory':str(temp),'files':sum(p.is_file() for p in temp.rglob('*')),'bytes':sum(p.stat().st_size for p in temp.rglob('*') if p.is_file())}
shutil.rmtree(temp)
(out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
print(record['status']+'; records '+str(out))
