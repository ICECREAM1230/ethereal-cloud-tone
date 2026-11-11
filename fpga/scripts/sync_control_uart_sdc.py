#!/usr/bin/env python3
"""Select plain Gowin SDC lines before freezing inputs; never evaluate SDC as Tcl."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OFF = '# CH569_UART_OFF '
CAPTURE_OFF = '# CH569_PCM_CAPTURE_OFF '
BLOCKS = {'LEGACY': 3, 'PCM': 4, 'CAPTURE': 5}
COMMANDS = {'create_clock', 'create_generated_clock', 'set_false_path',
            'set_max_delay', 'report_timing', 'report_min_pulse_width'}
MONITOR = 'u_system/g_ch569_pcm.u_ch569_monitor/'
CAPTURE_CLOCK = ('create_generated_clock -name ch569_hspi_clk -source [get_ports {sys_clk}] '
                 '-master_clock board_ref_clk -divide_by 1 -invert [get_ports {HSPI_HTCLK}]')
CAPTURE_COMMANDS = {CAPTURE_CLOCK}
for source, pointer in [('pcm_audio_clk', 'rq1_wptr'), ('board_ref_clk', 'wq1_rptr')]:
    endpoint = MONITOR + 'u_pcm_sample_async_fifo/fifo_inst/Equal.' + pointer + '_*_s0/D'
    CAPTURE_COMMANDS.add(f'set_max_delay -from [get_clocks {{{source}}}] -to [get_pins {{{endpoint}}}] 20.000')
    CAPTURE_COMMANDS.add(f'set_false_path -hold -from [get_clocks {{{source}}}] -to [get_pins {{{endpoint}}}]')


def literal_mode(top, parameter):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', top, flags=re.S)
    values = re.findall(r'\bparameter\s+bit\s+' + re.escape(parameter) + r'\s*=\s*([^,\n)]+)', source)
    if len(values) != 1 or not re.fullmatch(r"1\s*'\s*[bB]\s*[01]", values[0].strip()):
        raise ValueError(f"Expected one literal 1'b0 or 1'b1 top-level {parameter} parameter")
    return int(values[0].strip()[-1])


def selected_mode(top):
    return literal_mode(top, 'ENABLE_CH569_UART_DIAG')


def selected_capture_mode(top):
    return literal_mode(top, 'ENABLE_CH569_PCM_CAPTURE')


def render(sdc, mode, capture_mode):
    enabled = {'LEGACY': bool(mode), 'PCM': bool(mode and capture_mode), 'CAPTURE': bool(capture_mode)}
    lines, active, seen = [], None, set()
    counts = {name: 0 for name in BLOCKS}
    capture_commands = set()
    for number, line in enumerate(sdc.splitlines(keepends=True), 1):
        token = line.strip()
        marker = re.fullmatch(r'# (BEGIN|END)_CH569_UART_TIMING_(LEGACY|PCM)', token)
        capture_marker = re.fullmatch(r'# (BEGIN|END)_CH569_PCM_CAPTURE_TIMING', token)
        if marker or capture_marker:
            edge, name = marker.groups() if marker else (capture_marker.group(1), 'CAPTURE')
            if edge == 'BEGIN':
                if active is not None or name in seen:
                    raise ValueError(f'line {number}: repeated or nested optional timing block')
                active = name
                seen.add(name)
            else:
                if active != name:
                    raise ValueError(f'line {number}: unmatched optional timing block end')
                active = None
            lines.append(line)
            continue
        prefix = next((p for p in [OFF, CAPTURE_OFF] if line.startswith(p)), None)
        if prefix is not None:
            if active is None or prefix != (CAPTURE_OFF if active == 'CAPTURE' else OFF):
                raise ValueError(f'line {number}: disabled line outside its marked timing block')
        plain = line[len(prefix):] if prefix else line
        command = plain.strip()
        if command and not command.startswith('#'):
            if command.split()[0] not in COMMANDS or '$' in command:
                raise ValueError(f'line {number}: unsupported control syntax in plain SDC')
            if active:
                if active == 'CAPTURE':
                    if command not in CAPTURE_COMMANDS or command in capture_commands:
                        raise ValueError(f'line {number}: capture constraint escapes its exact five commands')
                    capture_commands.add(command)
                else:
                    if command.split()[0] not in {'set_false_path', 'set_max_delay', 'report_timing'}:
                        raise ValueError(f'line {number}: unexpected optional UART timing command')
                    anchor = 'u_debug_monitor/' if active == 'LEGACY' else MONITOR + 'g_diagnostics.'
                    if anchor not in command:
                        raise ValueError(f'line {number}: UART constraint escapes its observation block')
                counts[active] += 1
                off = CAPTURE_OFF if active == 'CAPTURE' else OFF
                line = plain if enabled[active] else off + plain
        lines.append(line)
    if active is not None or seen != set(BLOCKS) or counts != BLOCKS or capture_commands != CAPTURE_COMMANDS:
        raise ValueError(f'Incomplete optional timing blocks: seen={sorted(seen)}, counts={counts}, open={active}')
    return ''.join(lines)


def sync(root, check=False):
    top = (root / 'rtl/top/control_lab_top.sv').read_text()
    mode, capture_mode = selected_mode(top), selected_capture_mode(top)
    path = root / 'constraints/control_lab.sdc'
    original = path.read_text()
    desired = render(original, mode, capture_mode)
    changed = desired != original
    if check and changed:
        raise ValueError('Optional UART/capture SDC mode is stale; select before synthesis, then rebuild')
    if changed:
        fd, name = tempfile.mkstemp(prefix='.' + path.name + '.', dir=path.parent)
        try:
            with os.fdopen(fd, 'w') as stream:
                stream.write(desired)
            os.chmod(name, path.stat().st_mode & 0o777)
            os.replace(name, path)
        finally:
            if os.path.exists(name):
                os.unlink(name)
    selected = 3 * mode + 4 * bool(mode and capture_mode) + 5 * capture_mode
    return {'status': 'PASS', 'enabled': bool(mode), 'capture_enabled': bool(capture_mode),
            'optional_commands': selected, 'changed': changed, 'read_only': check,
            'scope': 'Build-time text selection only; vendor SDC parsing and STA require official PnR'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--check', action='store_true', help='Reject stale inputs without rewriting them')
    args = parser.parse_args()
    try:
        print(json.dumps(sync(args.root.resolve(), args.check)))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
