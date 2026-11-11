#!/usr/bin/env python3
"""Link-only FDC candidate; does not touch production firmware/ITCM or hardware."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

p=argparse.ArgumentParser()
p.add_argument('--out',type=Path,required=True)
p.add_argument('--enable',type=int,choices=(0,1),default=1)
p.add_argument('--gcc',default='riscv64-unknown-elf-gcc')
a=p.parse_args()
src=Path(__file__).resolve().parent;root=src.parents[2]
out=a.out.resolve()
gcc=Path(a.gcc);prefix=str(gcc)[:-3]
common=['-march=rv32im_zicsr','-mabi=ilp32','-mcmodel=medany','-mno-relax','-msmall-data-limit=0','-Os','-std=c99','-ffreestanding','-fno-builtin','-fno-pic','-fno-stack-protector','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-DENABLE_AE350_FDC='+str(a.enable),'-I'+str(src),'-I'+str(src/'freestanding')]
sources=[root/'firmware/start.S',root/'sim/tb/fdc_ae350_firmware_entry.c',src/'fdc_ae350.c',src/'theremin_control.c',src/'freestanding/runtime.c']
inputs=sources+[src/'fdc_ae350.h',src/'theremin_control.h',src/'freestanding/math.h',src/'freestanding/string.h',root/'firmware/control_link.ld',Path(__file__).resolve()]
missing=[str(x) for x in inputs if not x.is_file()]
if missing:p.error('Required standalone source/fixture missing: '+', '.join(missing))
out.mkdir(parents=True,exist_ok=False)
record={'status':'RUNNING','scope':'RV32IM/ilp32 standalone link-only candidate; no hardware/WCET/native scheduler claim','enable':a.enable,'inputs':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},'commands':[]}
objects=[]
def run(name,argv):
 r=subprocess.run(list(map(str,argv)),cwd=out,capture_output=True,text=True)
 (out/(name+'.log')).write_text(r.stdout+r.stderr+'\nEXIT_STATUS='+str(r.returncode)+'\n')
 record['commands'].append({'name':name,'argv':list(map(str,argv)),'exit':r.returncode})
 (out/'target.json').write_text(json.dumps(record,indent=2)+'\n')
 if r.returncode:raise RuntimeError(name+' failed: '+r.stderr[-2500:])
 return r.stdout
try:
 record['compiler']=run('version',[gcc,'--version']).splitlines()[0]
 record['multilib']=run('multilib',[gcc,'-print-multi-lib'])
 libgcc=Path(run('libgcc',[gcc,*common[:2],'-print-libgcc-file-name']).strip()).resolve()
 assert 'rv32im/ilp32/libgcc.a' in str(libgcc),libgcc
 record['libgcc']={'path':str(libgcc),'sha256':hashlib.sha256(libgcc.read_bytes()).hexdigest()}
 for index,s in enumerate(sources):
  obj=out/(str(index)+'_'+s.stem+'.o');objects.append(obj)
  run('compile-'+s.stem,[gcc,*common,'-c',s,'-o',obj])
 elf=out/'fdc-candidate.elf'
 run('link',[gcc,*common,'-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--build-id=none','-Wl,--gc-sections','-Wl,-Map='+str(out/'fdc-candidate.map'),'-T',root/'firmware/control_link.ld',*objects,'-lgcc','-o',elf])
 undefined=run('undefined',[prefix+'nm','-u',elf]).strip();assert not undefined,undefined
 record['size']=run('size',[prefix+'size',elf]).strip()
 record['attributes']=run('attributes',[prefix+'readelf','-A',elf]).strip()
 run('elf-header',[prefix+'readelf','-h',elf])
 # This installed readelf prints only 0x0, not the words "soft-float ABI".
 # Inspect EF_RISCV_FLOAT_ABI (bits2:1) in the actual ELF32 header instead.
 header=elf.read_bytes();assert header[4:6]==bytes([1,1])
 record['elf_flags']=int.from_bytes(header[36:40],'little')
 assert record['elf_flags']&6==0
 assert re.search(r'Tag_RISCV_arch: "rv32i[^"\n]*_m',record['attributes'])
 run('objcopy',[prefix+'objcopy','-O','binary',elf,out/'fdc-candidate.bin'])
 record['binary_bytes']=(out/'fdc-candidate.bin').stat().st_size
 assert record['binary_bytes']<=65536
 record['binary_sha256']=hashlib.sha256((out/'fdc-candidate.bin').read_bytes()).hexdigest()
 record['stack_usage']={x.name:x.read_text() for x in sorted(out.glob('*.su'))}
 record['input_drift']=[str(x) for x in inputs if hashlib.sha256(x.read_bytes()).hexdigest()!=record['inputs'][str(x)]]
 assert not record['input_drift']
 record['status']='PASS'
except Exception as e:
 record.update(status='FAIL',error=str(e));raise
finally:
 # ELF/bin/map/stack evidence is retained. Only these just-created object
 # intermediates are redundant after linking (or a captured compile error).
 made=[x for x in objects if x.exists()]
 record['cleanup']={'objects':[str(x) for x in made],'bytes':sum(x.stat().st_size for x in made),'confirmed_current_build_only':True}
 for x in made:x.unlink()
 (out/'target.json').write_text(json.dumps(record,indent=2)+'\n')
print(record['size']);print('binary bytes:',record['binary_bytes']);print('result:',out/'target.json')
