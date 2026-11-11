#!/usr/bin/python3
"""Bind native GUI report paths to one completed build and its actual inputs."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, data):
    fd, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as out:
            json.dump(data, out, ensure_ascii=False, indent=2)
            out.write('\n')
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def config_digest(path):
    value = json.dumps(json.loads(path.read_text()), sort_keys=True, ensure_ascii=False)
    return hashlib.sha256(value.encode()).hexdigest()


def inputs(project, script):
    root = project.parent
    paths = {project.resolve(), script.resolve()}
    for name in ['run_gowin_synced.py', 'gowin_gui_sync.tcl', 'gowin_results.py']:
        helper = script.parent / name
        if helper.is_file():
            paths.add(helper.resolve())
    for entry in ET.parse(project).iter('File'):
        if entry.get('enable') == '1':
            paths.add((root / entry.attrib['path']).resolve())
    # The AE350 IP loads these memory images/includes without GPRJ File entries.
    for folder in [root / 'rtl', root / 'ip', root / 'firmware/ae350/images']:
        if folder.exists():
            for pattern in ['*.hex', '*.mem', '*.mi', '*.vh', '*.svh', '*.bin', '*.ipc', 'itcm*']:
                paths.update(p.resolve() for p in folder.rglob(pattern) if p.is_file())
    # Include the true processor sources, not only their generated ITCM bytes.
    for folder in [root/'firmware/ae350/app', root/'firmware/ae350/midi',
                   root/'firmware/ae350/include', root/'firmware/ae350/fdc']:
        if folder.exists():
            paths.update(p.resolve() for pattern in ['*.c','*.h','*.S','*.ld']
                         for p in folder.rglob(pattern) if p.is_file())
    paths.update(p.resolve() for p in (root/'firmware/ae350/fdc_integration').glob('*.[ch]'))
    for name in ['build_control_firmware.py', 'manage_control_build.py', 'sync_control_uart_sdc.py']:
        paths.add((root/'scripts'/name).resolve())
    record=root/'firmware/ae350/images/control-build.json'
    if record.is_file():
        paths.add(record.resolve())
        paths.update((root / name).resolve() for name in json.loads(record.read_text()).get('libraries', {}))
    return {os.path.relpath(p, root): digest(p) for p in sorted(paths)}


def artifact_paths(base, stage):
    if stage == 'syn':
        return {'RES.netlist': f'impl/gwsynthesis/{base}.vg',
                'RES.syn.report': f'impl/gwsynthesis/{base}_syn.rpt.html',
                'RES.syn.resource': f'impl/gwsynthesis/{base}_syn_rsc.xml'}
    return {'RES.pnr.bitstream': f'impl/pnr/{base}.fs',
            'RES.pnr.report': f'impl/pnr/{base}.rpt.html',
            'RES.pnr.timing.rpt': f'impl/pnr/{base}.tr.html'}


def file_state(path):
    if not path.is_file():
        return None
    stat = path.stat()
    return {'mtime_ns': stat.st_mtime_ns, 'ctime_ns': stat.st_ctime_ns,
            'size': stat.st_size, 'sha256': digest(path)}


def before_stage(session, stage):
    if stage not in ('syn', 'pnr'):
        raise RuntimeError('unsupported result stage: ' + stage)
    manifest = json.loads((session / 'manifest.json').read_text())
    project = Path(manifest['project']).resolve()
    config = json.loads((session / 'desired.json').read_text())
    base = config.get('OUTPUT_BASE_NAME') or project.stem
    if not re.fullmatch(r'[\w.-]+', base) or base in ('.', '..'):
        raise RuntimeError('output base must be a filename, not a path')
    paths = list(artifact_paths(base, stage).values())
    if stage == 'pnr':
        paths += [f'impl/pnr/{base}.tr', f'impl/pnr/{base}_tr_content.html',
                  f'impl/pnr/{base}_tr_cata.html']
    synthesis = None
    if stage == 'pnr':
        try:
            synthesis = check_results(project, 'syn', session / 'desired.json')
        except (OSError, KeyError, ValueError, RuntimeError) as error:
            raise RuntimeError('synthesis result is not current: ' + str(error)) from error
    record = {'project': str(project), 'script': manifest['script'], 'stage': stage,
              'output_base': base, 'inputs': inputs(project, Path(manifest['script'])),
              'config_sha256': config_digest(session / 'desired.json'),
              'synthesis': synthesis,
              'before': {p: file_state(project.parent / p) for p in paths}}
    write_json(session / 'stage-before.json', record)


def finish_stage(session):
    record = json.loads((session / 'stage-before.json').read_text())
    project = Path(record['project'])
    root = project.parent
    if inputs(project, Path(record['script'])) != record['inputs']:
        raise RuntimeError('build inputs changed; reports not accepted')
    config = root / 'impl' / (project.stem + '_process_config.json')
    if config_digest(config) != record['config_sha256']:
        raise RuntimeError('build configuration changed; reports not accepted')
    if record['stage'] == 'pnr':
        if check_results(project, 'syn') != record['synthesis']:
            raise RuntimeError('synthesis result changed during place/route')
    # Only accept the native IDE result mapping; never forge successful flow states.
    mapping = {e.get('ResultFileType'): e.get('ResultFilePath') for e in
               ET.parse(str(project) + '.user').iter('ResultFile')}
    for kind, expected in artifact_paths(record['output_base'], record['stage']).items():
        actual = mapping.get(kind)
        if actual is None or (root / actual).resolve() != (root / expected).resolve():
            raise RuntimeError(f'GUI result path mismatch for {kind}: {actual}; expected {expected}')
    artifacts = {}
    for name, before in record['before'].items():
        path = root / name
        if not path.resolve().is_relative_to(root.resolve()):
            raise RuntimeError('report escaped selected project: ' + str(path))
        state = file_state(path)
        if state is None or state['size'] == 0 or state == before:
            raise RuntimeError('output not regenerated by this stage: ' + name)
        artifacts[name] = state
    if record['stage'] == 'pnr':
        frame = root / f"impl/pnr/{record['output_base']}.tr.html"
        links = re.findall(r'<frame\b[^>]*\bsrc=[\"\x27]([^\"\x27]+)', frame.read_text(), re.I)
        expected = {f"{record['output_base']}_tr_cata.html", f"{record['output_base']}_tr_content.html"}
        if set(links) != expected:
            raise RuntimeError('timing HTML frames do not belong to this build')
    timing = None
    if record['stage'] == 'pnr':
        folder = root / 'impl/pnr'
        with (folder / (record['output_base'] + '.tr')).open(errors='replace') as source:
            native = source.read(65536)
        with (folder / (record['output_base'] + '_tr_content.html')).open(errors='replace') as source:
            html = source.read(65536)
        timing = {}
        for analysis in ['Setup', 'Hold']:
            label = 'Numbers of ' + analysis + ' Violated Endpoints'
            text_value = re.search('<' + label + r'>:\s*(\d+)', native)
            html_value = re.search(label + r'\s*</td>\s*<td[^>]*>\s*(\d+)', html)
            if not text_value or not html_value or text_value[1] != html_value[1]:
                raise RuntimeError('HTML/native timing summary mismatch: ' + analysis)
            timing[analysis.lower() + '_violated_endpoints'] = int(text_value[1])
    result = {k: record[k] for k in ['project', 'script', 'stage', 'output_base', 'inputs']}
    result.update(synthesis=record['synthesis'], timing=timing, artifacts=artifacts, config_sha256=record['config_sha256'],
                  completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    latest = root / 'impl' / (project.stem + '_build_results.json')
    data = json.loads(latest.read_text()) if latest.exists() else {}
    data[record['stage']] = result
    write_json(latest, data)
    write_json(session / 'accepted-results.json', result)
    print('GUI_RESULTS_READY: ' + str(project), flush=True)
    for kind, name in artifact_paths(record['output_base'], record['stage']).items():
        print(f'GUI_RESULT {kind}: {root / name}', flush=True)
    return result


def check_results(project, stage, config_override=None):
    project = project.resolve()
    result = json.loads((project.parent / 'impl' / (project.stem + '_build_results.json')).read_text())[stage]
    if result['project'] != str(project):
        raise RuntimeError('result belongs to another project')
    if inputs(project, Path(result['script'])) != result['inputs']:
        raise RuntimeError('build inputs changed; result is stale')
    config = config_override or project.parent / 'impl' / (project.stem + '_process_config.json')
    if not config.is_file() or config_digest(config) != result['config_sha256']:
        raise RuntimeError('build configuration changed; result is stale')
    for name, state in result['artifacts'].items():
        if not (project.parent / name).is_file() or digest(project.parent / name) != state['sha256']:
            raise RuntimeError('artifact changed: ' + name)
    if stage == 'pnr' and check_results(project, 'syn', config_override) != result['synthesis']:
        raise RuntimeError('synthesis result changed; PnR result is stale')
    return result
