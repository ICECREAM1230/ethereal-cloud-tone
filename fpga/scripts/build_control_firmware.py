#!/usr/bin/env python3
"""Build the actual RV32 application; optional isolated output never needs a board."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--prefix', default='riscv64-unknown-elf-')
p.add_argument('--enable-fdc', choices=(0, 1), type=int, default=1)
p.add_argument('--out-dir', type=Path)
p.add_argument('--no-install-itcm', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = a.out_dir.resolve() if a.out_dir else root / 'firmware/ae350/images'
out.mkdir(parents=True, exist_ok=True)
gcc, objcopy, size, nm = (a.prefix + name for name in ('gcc', 'objcopy', 'size', 'nm'))
isa = ['-march=rv32im_zicsr', '-mabi=ilp32']
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
source_names = [
    'firmware/ae350/app/start.S', 'firmware/ae350/app/control_main.c', 'firmware/ae350/app/control_service.c',
    'firmware/ae350/app/control_compile.c', 'firmware/ae350/midi/midi_parser.c',
    'firmware/ae350/midi/voice_manager.c',
    'firmware/ae350/midi/control_runtime.c',
]
flags = [
    gcc, *isa, '-mcmodel=medany', '-mno-relax', '-msmall-data-limit=0', '-Os',
    '-ffreestanding', '-fno-builtin', '-fno-pic', '-fno-stack-protector',
    '-Wall', '-Wextra', '-Werror', '-nostdlib', '-nostartfiles',
    '-Wl,--no-relax', '-Wl,--build-id=none', '-Wl,-Map=' + str(out / 'control.map'),
    '-T', str(root / 'firmware/ae350/app/control_link.ld'),
]
# Keep the original order and compiler options when the feature is off. The
# explicit macro also controls VM/compiler struct layouts and native packets.
cmd = flags + [str(root / source_names[0]),
    '-I' + str(root / 'firmware/ae350/midi'),
    '-I' + str(root / 'firmware/ae350/include'), '-I' + str(root / 'firmware/ae350/app'),
    '-fstack-usage', '-DENABLE_AE350_FDC=' + str(a.enable_fdc)]
libraries = {}
if a.enable_fdc:
    cmd += ['-I' + str(root / 'firmware/ae350/fdc'),
            '-I' + str(root / 'firmware/ae350/fdc/freestanding'),
            '-I' + str(root / 'firmware/ae350/fdc_integration')]
    source_names += [
        'firmware/ae350/app/fdc_application.c',
        'firmware/ae350/fdc/fdc_ae350.c',
        'firmware/ae350/fdc/theremin_control.c',
        'firmware/ae350/fdc/freestanding/runtime.c',
        'firmware/ae350/fdc_integration/fdc_native_owner.c',
    ]
    libgcc = Path(subprocess.check_output(
        [gcc, *isa, '-print-libgcc-file-name'], text=True).strip()).resolve()
    if not libgcc.is_file() or '/rv32im/ilp32/' not in libgcc.as_posix():
        raise SystemExit('Expected the actual RV32IM/ILP32 soft-float libgcc: ' + str(libgcc))
    libraries[str(libgcc)] = sha(libgcc)
cmd += [str(root / name) for name in source_names[1:]]
if a.enable_fdc:
    cmd += ['-lgcc']
cmd += ['-o', str(out / 'control.elf')]
subprocess.run(cmd, cwd=out, check=True)
undefined = subprocess.check_output([nm, '-u', str(out / 'control.elf')], text=True)
if undefined.strip():
    raise SystemExit('Unresolved production firmware symbols: ' + undefined)
subprocess.run([objcopy, '-O', 'binary', str(out / 'control.elf'),
                str(out / 'control.bin')], check=True)
data = (out / 'control.bin').read_bytes()
if not 0 < len(data) <= 65536:
    raise SystemExit('Firmware exceeds the 64KiB ITCM image')
elf = (out / 'control.elf').read_bytes()
if elf[4:6] != bytes([1, 1]) or int.from_bytes(elf[36:40], 'little') & 6:
    raise SystemExit('Expected RV32 little-endian soft-float ELF')
symbols = subprocess.check_output([nm, '-n', str(out / 'control.elf')], text=True)
values = {name: int(value, 16) for value, kind, name in
          re.findall(r'^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$', symbols, re.M)}
if values['__stack_top'] != 65536 or values['__bss_end'] >= 65536 - 4096:
    raise SystemExit('The original DTCM 4KiB stack reserve was not preserved')

lane_records = {}
if not a.no_install_itcm:
    init = root / 'ip/ae350/itcm_initial'
    init.mkdir(parents=True, exist_ok=True)
    image = data.ljust(65536, b'\0')
    # Vendor make_hex layout: one byte per line, four interleaved byte lanes.
    for lane in range(4):
        target = init / ('itcm' + str(lane))
        target.write_text(''.join('%02x\n' % byte for byte in image[lane::4]))
        lane_records[str(target.relative_to(root))] = sha(target)

sources = (list((root / 'firmware/ae350/app').glob('*.[ch]')) +
           [root / 'firmware/ae350/app/control_link.ld', root / 'firmware/ae350/app/start.S',
            Path(__file__).resolve()] +
           list((root / 'firmware/ae350/app/control_data').glob('*.h')) +
           list((root / 'firmware/ae350/midi').glob('*.[ch]')) +
           list((root / 'firmware/ae350/include').glob('*.h')))
if a.enable_fdc:
    sources += list((root / 'firmware/ae350/fdc').rglob('*.[ch]'))
    sources += [root / 'firmware/ae350/fdc_integration' / name
                for name in ('fdc_native_owner.c', 'fdc_native_owner.h')]
record = {
    'command': cmd,
    'compiler': subprocess.check_output([gcc, '--version'], text=True).splitlines()[0],
    'binary_bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
    'enable_ae350_fdc': a.enable_fdc, 'compiled_sources': source_names,
    'libraries': libraries, 'undefined_symbols': [],
    'itcm_base': '0x80000000', 'dtcm_base': '0x00000000',
    'software_ram_origin': '0x00000100', 'stack_top': '0x00010000',
    'bss_end': values['__bss_end'],
    'bss_to_stack_top_bytes': 65536 - values['__bss_end'],
    'stack_reserve_bytes': 4096,
    'stack_limit': 'Linker reserve and individual .su frames do not prove complete dynamic stack/WCET',
    'cache': 'reset default disabled', 'itcm_installed': not a.no_install_itcm,
    'itcm_sha256': lane_records,
    'source_sha256': {str(f.relative_to(root)): sha(f) for f in sorted(set(sources))},
}
(out / 'control-build.json').write_text(json.dumps(record, indent=2) + '\n')
subprocess.run([size, str(out / 'control.elf')], check=True)
print(json.dumps({key: record[key] for key in
                 ('binary_bytes', 'sha256', 'enable_ae350_fdc',
                  'bss_to_stack_top_bytes', 'itcm_installed')}))
print('Build identity:', out / 'control-build.json')
