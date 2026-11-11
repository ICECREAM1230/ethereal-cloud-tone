#!/usr/bin/env python3
"""Check actual plaintext GW5AST AE350 ITCM initialization, not input files.

Supported mapping: the official 64KiB ITCM has 32 SP bitplanes, A..D byte
lanes and bits0..7. Each INIT_RAM_00..3F stores256 successive bit addresses,
least significant bit first. Omitted blocks use the SP primitive's zero
default. Other memory organizations are rejected instead of guessed.
Generated protected HDL is neither decoded nor modified by this checker.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


class MappingError(ValueError):
    pass


INSTANCE = (r"(?P<instance>\\\S*/u_vc_ilm_ram/"
            r"mem(?P<lane>[A-D])_mem(?P=lane)_0_(?P<bit>[0-7])_s)")
CELL = re.compile(r"^\s*SP\s+" + INSTANCE + r"\s*\(")
PARAM = re.compile(r"^\s*defparam\s+" + INSTANCE +
                   r"\s*\.(?P<parameter>BIT_WIDTH|INIT_RAM_[0-9A-Fa-f]{2})"
                   r"\s*=\s*(?P<value>[^;]+);\s*$")
HEX256 = re.compile(r"256'[hH]([0-9A-Fa-f_]+)")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def reconstruct(netlist):
    cells, parameters = {}, {}
    with Path(netlist).open(errors='strict') as stream:
        for line in stream:
            if 'u_gw_itcm' not in line:
                continue
            match = CELL.match(line)
            if match:
                key = (ord(match['lane']) - ord('A'), int(match['bit']))
                if key in cells:
                    raise MappingError('Duplicate ITCM bitplane: ' + str(key))
                cells[key] = match['instance']
            match = PARAM.match(line)
            if match:
                key = (ord(match['lane']) - ord('A'), int(match['bit']))
                name = match['parameter'].upper()
                if (key, name) in parameters:
                    raise MappingError('Duplicate ITCM parameter: ' + str((key, name)))
                parameters[key, name] = (match['instance'], match['value'].strip())
    expected = {(lane, bit) for lane in range(4) for bit in range(8)}
    if set(cells) != expected:
        raise MappingError('Expected exactly32 official SP ITCM bitplanes; found ' + str(len(cells)))
    image = bytearray(65536)
    blocks = 0
    for key, instance in cells.items():
        if parameters.get((key, 'BIT_WIDTH')) != (instance, '1'):
            raise MappingError('ITCM SP BIT_WIDTH must be1: ' + instance)
    for (key, name), (instance, value) in parameters.items():
        if cells.get(key) != instance:
            raise MappingError('ITCM parameter does not belong to its SP: ' + instance)
        if not name.startswith('INIT_RAM_'):
            continue
        block = int(name[-2:], 16)
        match = HEX256.fullmatch(value)
        if block >= 64 or not match:
            raise MappingError('Unsupported initialization parameter: ' + name + '=' + value)
        word = int(match[1].replace('_', ''), 16)
        if word.bit_length() > 256:
            raise MappingError('Initialization exceeds256bits: ' + instance)
        lane, bit = key
        for offset in range(256):
            if (word >> offset) & 1:
                image[4 * (block * 256 + offset) + lane] |= 1 << bit
        blocks += 1
    return bytes(image), {'SP_bitplanes': len(cells), 'explicit_INIT_blocks': blocks,
                          'mapping': 'GW5AST64KiB SP BIT_WIDTH1 byte-lane/bitplane',
                          'omitted_INIT_blocks': 'official SP default zero'}


def verify(binary, netlist):
    binary, netlist = Path(binary), Path(netlist)
    data = binary.read_bytes()
    if not 0 < len(data) <= 65536:
        raise MappingError('Expected a nonempty firmware binary of at most64KiB')
    actual, mapping = reconstruct(netlist)
    expected = data.ljust(65536, b'\0')
    differences = [i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1]]
    return {'status': 'PASS' if not differences else 'FAIL', 'binary': str(binary.resolve()),
            'netlist': str(netlist.resolve()), 'binary_bytes': len(data),
            'binary_SHA256': sha(data), 'netlist_SHA256': sha(netlist.read_bytes()),
            'expected_padded64K_SHA256': sha(expected), 'actual_padded64K_SHA256': sha(actual),
            'actual_prefix_SHA256': sha(actual[:len(data)]), 'tail_zero': not any(actual[len(data):]),
            'different_bytes': len(differences), 'first_difference_offsets': differences[:16],
            'actual_embedded_image_matches_binary': not differences, **mapping}


def verify_project(project, stage):
    root = Path(project).resolve()
    if root.suffix == '.gprj':
        root = root.parent
    binary = root / 'firmware/ae350/images/control.bin'
    generated = root / 'ip/ae350'
    receipt = json.loads((generated / 'embedded-firmware.json').read_text())
    if receipt.get('ipc_SHA256') != sha((generated / 'riscv_ae350_soc.ipc').read_bytes()):
        raise MappingError('AE350 configuration changed after official IP generation')
    if receipt.get('binary_SHA256') != sha(binary.read_bytes()):
        raise MappingError('Firmware changed after official IP generation; regenerate AE350 IP')
    for name in ('RiscV_AE350_SOC_Top.v', 'RiscV_AE350_SOC_Top.vo'):
        if receipt.get('outputs', {}).get(name) != sha((generated / name).read_bytes()):
            raise MappingError('Generated IP differs from its official generation receipt: ' + name)
    reports = {'generated': verify(binary, generated / 'RiscV_AE350_SOC_Top.vo')}
    if stage == 'pnr':
        reports['routed'] = verify(binary, root / 'impl/pnr/ethereal_cloud_tone.vo')
    return {'status': 'PASS' if all(x['status'] == 'PASS' for x in reports.values()) else 'FAIL',
            'stage': stage, 'receipt': str(generated / 'embedded-firmware.json'), **reports}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--netlist', type=Path)
    parser.add_argument('--project', type=Path)
    parser.add_argument('--stage', choices=('syn', 'pnr'), default='syn')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.project:
        if args.binary or args.netlist:
            parser.error('--project cannot be combined with --binary/--netlist')
    elif not args.binary or not args.netlist:
        parser.error('supply --project or both --binary and --netlist')
    try:
        result = verify_project(args.project, args.stage) if args.project else verify(args.binary, args.netlist)
        code = 0 if result['status'] == 'PASS' else 1
    except (OSError, ValueError) as error:
        result, code = {'status': 'BLOCKED', 'error': str(error)}, 2
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
    print(text, end='')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
