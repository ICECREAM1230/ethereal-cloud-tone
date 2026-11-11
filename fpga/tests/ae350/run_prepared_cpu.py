#!/usr/bin/env python3
"""Actual C CPU compiler/service tests and RV32 closure; no vendor/device work."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--run',required=True)
p.add_argument('--rv32-prefix',default='riscv64-unknown-elf-')
p.add_argument('--output',type=Path,help='New result directory')
p.add_argument('--work-dir',type=Path,help='New external temporary directory')
p.add_argument('--host-only',action='store_true',help='Run native C checks; explicitly skip RV32 object checks')
a=p.parse_args()
if not re.fullmatch(r'[A-Za-z0-9_-]+',a.run):p.error('unsafe run name')
root=Path(__file__).resolve().parents[2]
out=a.output.resolve() if a.output else root/'tests/results/prepared_cpu'/a.run;out.mkdir(parents=True,exist_ok=False)
temp=a.work_dir.resolve() if a.work_dir else root/'tests/.work'/('prepared_cpu_'+a.run);temp.mkdir(parents=True,exist_ok=False)
sources=[Path('firmware/ae350/app/control_compile.c'),Path('firmware/ae350/app/control_service.c')]+[Path('firmware/ae350/midi')/n for n in ('voice_manager.c','control_runtime.c','midi_parser.c')]
headers=[Path('firmware/ae350/app/control_compile.h'),Path('firmware/ae350/app/control_service.h'),Path('firmware/ae350/app/control_data/control_boot_tables.h')]+list(Path('firmware/ae350/include')/n for n in ('control_event.h','prepared_control.h'))+[Path('firmware/ae350/midi')/n for n in ('voice_manager.h','control_runtime.h','midi_parser.h')]
tests=[Path('tests/ae350/test_control_compile.c'),Path('tests/ae350/test_prepared_service.c')]
inputs=sources+headers+tests+[Path('tests/ae350/run_prepared_cpu.py'),Path('docs/control-prepared-v2.md')]
record={'scope':'Actual freestanding C compiler/service/parser/VM/runtime, expected native integers and ordered host MMIO unit model; no actual RTL/AE350/PCM execution','inputs':{str(f):hashlib.sha256((root/f).read_bytes()).hexdigest() for f in inputs},'tests':{}}
def run(name,cmd,timeout=60):
 r=subprocess.run(list(map(str,cmd)),cwd=temp,capture_output=True,text=True,timeout=timeout)
 (out/(name+'.log')).write_text(r.stdout+r.stderr)
 record['tests'][name]={'command':list(map(str,cmd)),'exit_code':r.returncode}
 (out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
 if r.returncode:raise SystemExit(name+' failed:\n'+r.stdout[-5000:]+r.stderr[-5000:])
 return r.stdout
run('host-version',['gcc','--version'])
include=['-I'+str(root/'firmware/ae350/app'),'-I'+str(root/'firmware/ae350/include')]
flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-ffreestanding','-fno-builtin']
for name,test in zip(('compiler','service'),tests):
 common=[*flags,*include,*[root/s for s in sources],root/test]
 run(name+'-compile',['gcc',*common,'-o',temp/name])
 print(run(name+'-tests',[temp/name]).strip())
 run(name+'-sanitizer-compile',['gcc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*common,'-o',temp/(name+'_san')])
 run(name+'-sanitizer-tests',[temp/(name+'_san')])
run('analyzer',['gcc',*flags,*include,'-fanalyzer','-fsyntax-only',*[root/s for s in sources]])
# The integer ROM is a literal extraction of the actual legacy RTL, not libm.
ks_path=root/'rtl/synthesizers/karplus_strong/control/ks_boot_template.sv'
fm_path=root/'rtl/synthesizers/fm/jt51_adapter_pkg.sv'
ks=ks_path.read_text();fm=fm_path.read_text();table=(root/headers[2]).read_text()
for path,content in ((ks_path,ks),(fm_path,fm)):
 sha=hashlib.sha256(content.encode()).hexdigest()
 # License/comment changes do not alter ROM values; compare every literal below.
 record.setdefault('integer_rom_sources',{})[str(path.relative_to(root))]=sha
literal=re.search(r'cf_ks_boot_words\[384\]=\{(.*?)\};',table,re.S).group(1)
words=[int(v,16) for v in re.findall(r'UINT32_C\(0x([0-9a-f]+)\)',literal)]
expected={int(i):int(v,16) for i,v in re.findall(r"9'd(\d+):word_o<=32'h([0-9a-fA-F]+)",ks)}
assert words==[expected[i] for i in range(384)]
att=[None]*128
for ids,val in re.findall(r"((?:7'd\d+,?)+):return 7'd(\d+);",fm):
 for i in re.findall(r"7'd(\d+)",ids):att[int(i)]=int(val)
assert [int(x) for x in re.findall(r'\d+',re.search(r'cf_fm_attenuation\[128\]=\{(.*?)\};',table,re.S).group(1))]==att
record['integer_rom_comparison']='PASS: every 384 KS words and 128 FM attenuation values matched source literals'
if not a.host_only:
 cc=a.rv32_prefix+'gcc';objects=[]
 rf=['-march=rv32im_zicsr','-mabi=ilp32','-mcmodel=medany','-mno-relax','-msmall-data-limit=0','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-std=c11','-Wall','-Wextra','-Werror']
 run('rv32-version',[cc,'--version'])
 for source in sources:
  obj=temp/(source.stem+'.o');objects.append(obj)
  run(source.stem+'-rv32',[cc,*rf,*include,'-fstack-usage','-c',root/source,'-o',obj])
  shutil.copy2(obj.with_suffix('.su'),out/(source.stem+'-stack.txt'))
 run('rv32-link',[cc,*rf,'-nostdlib','-r','-Wl,--no-relax',*objects,'-o',temp/'bundle.o'])
 undefined=run('rv32-undefined',[a.rv32_prefix+'nm','-u',temp/'bundle.o'])
 assert not undefined.strip(),undefined
 run('rv32-size',[a.rv32_prefix+'size',temp/'bundle.o'])
 probe=temp/'sizeof.c';probe.write_text('#include "control_service.h"\nchar service_storage[sizeof(cs_service_t)];\nchar compiler_storage[sizeof(control_compiler_t)];\n')
 run('rv32-sizeof-compile',[cc,*rf,*include,'-c',probe,'-o',temp/'sizeof.o'])
 run('rv32-sizeof',[a.rv32_prefix+'nm','-S','--size-sort',temp/'sizeof.o'])
 dis=run('rv32-mmio-order',[a.rv32_prefix+'objdump','-d',objects[1]])
 assert 'fence' in dis
for rel,sha in record['inputs'].items():assert hashlib.sha256((root/rel).read_bytes()).hexdigest()==sha,'changed during validation: '+rel
record['status']='PASS_HOST_ONLY' if a.host_only else 'PASS'
record['target_validation']='not run (--host-only)' if a.host_only else 'RV32 checks passed'
if not a.host_only:record['undefined_symbols']=[]
record['cleanup']={'directory':str(temp),'files':sum(f.is_file() for f in temp.rglob('*')),'bytes':sum(f.stat().st_size for f in temp.rglob('*') if f.is_file())}
shutil.rmtree(temp)
(out/'validation.json').write_text(json.dumps(record,indent=2)+'\n')
print(record['status']+'; records '+str(out))
