#!/usr/bin/env python3
"""Build RV32 firmware with a selected GCC prefix; usable on Windows or Linux."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--prefix',default='riscv64-unknown-elf-');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'firmware/build';out.mkdir(exist_ok=True)
gcc=a.prefix+'gcc';objcopy=a.prefix+'objcopy';size=a.prefix+'size'
cmd=[gcc,'-march=rv32im_zicsr','-mabi=ilp32','-mcmodel=medany','-mno-relax','-msmall-data-limit=0','-Os','-ffreestanding','-fno-builtin','-fno-pic','-fno-stack-protector','-Wall','-Wextra','-Werror','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--build-id=none','-Wl,-Map='+str(out/'control.map'),'-T',str(root/'firmware/control_link.ld'),str(root/'firmware/start.S'),'-I'+str(root/'control_migration/firmware'),'-I'+str(root/'control_migration/include'),'-I'+str(root/'firmware'),'-fstack-usage',str(root/'firmware/control_main.c'),str(root/'firmware/control_service.c'),str(root/'firmware/control_compile.c'),str(root/'firmware/control_latency.c'),str(root/'control_migration/firmware/midi_parser.c'),str(root/'control_migration/firmware/voice_manager.c'),str(root/'control_migration/firmware/control_runtime.c'),'-o',str(out/'control.elf')]
subprocess.run(cmd,check=True);subprocess.run([objcopy,'-O','binary',str(out/'control.elf'),str(out/'control.bin')],check=True)
data=(out/'control.bin').read_bytes();assert 0<len(data)<=65536
init=root/'vendor/ae350_control/itcm_initial';init.mkdir(parents=True,exist_ok=True)
# Exactly match the vendor make_hex layout: one byte per line, 4 byte lanes.
image=data.ljust(65536,b'\0')
for lane in range(4):(init/('itcm'+str(lane))).write_text(''.join('%02x\n'%b for b in image[lane::4]))
sources=list((root/'firmware').rglob('*.[ch]'))+[root/'firmware/control_link.ld',root/'firmware/start.S']+list((root/'control_migration/firmware').glob('*.[ch]'))+list((root/'control_migration/include').glob('*.h'))
record={'command':cmd,'compiler':subprocess.check_output([gcc,'--version'],text=True).splitlines()[0],'binary_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'itcm_base':'0x80000000','dtcm_base':'0x00000000','software_ram_origin':'0x00000100','stack_top':'0x00010000','cache':'reset default disabled','source_sha256':{str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources}}
(out/'control-build.json').write_text(json.dumps(record,indent=2)+'\n');subprocess.run([size,str(out/'control.elf')],check=True);print(json.dumps(record))
