#!/usr/bin/python3
"""Persist only config fields changed by successful Gowin option commands.

No option mapping: compare the vendor JSON before/after each command, merge
the changed fields into the original config, and commit after CLI exit.
"""
import argparse
import fcntl
import hashlib
import json
import os
import runpy
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
# -I excludes script directories; load only our sibling helper explicitly.
import importlib.util
_spec = importlib.util.spec_from_file_location("gowin_results", HERE / "gowin_results.py")
results = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(results)


def validated(data):
    if not isinstance(json.loads(data), dict):
        raise ValueError('vendor process config must be a JSON object')
    return data


def atomic_write(path, data):
    fd, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        if path.exists():
            os.fchmod(fd, stat.S_IMODE(path.stat().st_mode))
        with os.fdopen(fd, 'wb') as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def patch_config(original, changes):
    """Replace only top-level value tokens; preserve all unrelated JSON text."""
    validated(original)
    text = original.decode('utf-8')
    decoder = json.JSONDecoder()
    def skip(pos):
        while pos < len(text) and text[pos].isspace():
            pos += 1
        return pos
    pos = skip(0)
    if text[pos] != '{':
        raise ValueError('config must begin with a JSON object')
    start = pos + 1
    pos = skip(start)
    edits, seen, last_end = [], set(), start
    while text[pos] != '}':
        key, end = decoder.raw_decode(text, pos)
        value_start = skip(skip(end) + 1)  # Skip the colon.
        value, end = decoder.raw_decode(text, value_start)
        seen.add(key)
        if key in changes and (type(value) is not type(changes[key]) or value != changes[key]):
            edits.append((value_start, end, json.dumps(changes[key], ensure_ascii=False)))
        last_end = end
        pos = skip(end)
        if text[pos] == ',':
            pos = skip(pos + 1)
    added = [key for key in changes if key not in seen]
    if added:
        insertion = (',' if seen else '') + '\n' + ',\n'.join(
            '    ' + json.dumps(key, ensure_ascii=False) + ': ' + json.dumps(changes[key], ensure_ascii=False)
            for key in added)
        edits.append((last_end, last_end, insertion))
    for begin, end, value in sorted(edits, reverse=True):
        text = text[:begin] + value + text[end:]
    return validated(text.encode('utf-8'))


def capture(session, before_option=False):
    manifest = json.loads((session / 'manifest.json').read_text(encoding='utf-8'))
    data = validated(Path(manifest['config']).read_bytes())
    if before_option:
        atomic_write(session / 'option-before.json', data)
        return
    previous = json.loads((session / 'option-before.json').read_bytes())
    current = json.loads(data)
    changes_path = session / 'changes.json'
    changes = json.loads(changes_path.read_bytes()) if changes_path.exists() else {}
    # Native serialization may discard unknown fields. Never propagate deletions.
    for key, value in current.items():
        if key not in previous or type(previous[key]) is not type(value) or previous[key] != value:
            changes[key] = value
    desired = patch_config((session / 'before.json').read_bytes(), changes)
    atomic_write(changes_path, json_bytes(changes))
    atomic_write(session / 'desired.json', desired)


def isolated_metadata(project, config, session):
    """Config-only operations cannot write the original .gprj/.gprj.user."""
    folder = session / 'project'
    (folder / 'impl').mkdir(parents=True)
    copied = folder / project.name
    tree = ET.parse(project)
    for entry in tree.iter('File'):
        path = entry.get('path')
        if path and not Path(path).is_absolute():
            entry.set('path', str((project.parent / path).resolve()))
    tree.write(copied, encoding='utf-8', xml_declaration=True)
    runtime_config = folder / 'impl' / config.name
    shutil.copy2(config, runtime_config)
    user_file = Path(str(project) + '.user')
    if user_file.exists():
        shutil.copy2(user_file, Path(str(copied) + '.user'))
    return copied, runtime_config


def assert_gui_closed(project):
    """Detect the GUI ownership patterns observed on this Ubuntu installation."""
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if Path(os.readlink(proc / 'exe')).name != 'gw_ide':
                continue
            cwd = Path(os.readlink(proc / 'cwd')).resolve()
            args = (proc / 'cmdline').read_bytes().split(b'\0')
            if cwd == project.parent or os.fsencode(project) in args:
                raise RuntimeError(f'close this project in Gowin GUI first (PID {proc.name}); no process was stopped')
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue


def run(project, script, script_args, launcher, config_only=False):
    project = project.resolve(strict=True)
    script = script.resolve(strict=True)
    if project.suffix != '.gprj':
        raise ValueError('--project must name the selected .gprj')
    config = project.parent / 'impl' / (project.stem + '_process_config.json')
    assert_gui_closed(project)
    # Cooperating AI invocations keep one writer for this project.
    with (config.parent / '.gowin-gui-sync.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('another synchronized build owns this project') from None
        before = validated(config.read_bytes())
        session = Path(tempfile.mkdtemp(prefix='.gui-sync-', dir=config.parent))
        (session / 'before.json').write_bytes(before)
        (session / 'before.gprj').write_bytes(project.read_bytes())
        atomic_write(session / 'desired.json', before)
        runtime_project, runtime_config = (isolated_metadata(project, config, session)
                                           if config_only else (project, config))
        manifest = {'project': str(project), 'config': str(runtime_config),
                    'target_config': str(config), 'config_only': config_only, 'script': str(script),
                    'config_sha256_before': hashlib.sha256(before).hexdigest(),
                    'script_args': script_args}
        (session / 'manifest.json').write_bytes(json_bytes(manifest))
        env = os.environ.copy()
        env.update(GOWIN_GUI_SYNC_SESSION=str(session), GOWIN_GUI_SYNC_PYTHON=sys.executable,
                   GOWIN_GUI_SYNC_PROJECT=str(project), GOWIN_GUI_SYNC_OPEN_PROJECT=str(runtime_project),
                   GOWIN_GUI_SYNC_CONFIG_ONLY='1' if config_only else '0')
        args = [str(launcher), str(HERE / 'gowin_gui_sync.tcl'), str(script), *script_args]
        process = subprocess.Popen(args, cwd=project.parent, env=env)
        interrupted = False
        try:
            rc = process.wait()
        except KeyboardInterrupt:
            interrupted = True
            process.terminate()  # Only this newly launched CLI; never an existing GUI.
            rc = process.wait()
        # run close / C++ shutdown can no longer reset the file after this point.
        synchronized = False
        if (session / 'opened').exists():
            assert_gui_closed(project)
            desired = session / 'desired.json'
            data = validated(desired.read_bytes()) if desired.exists() else before
            if config_only and config.read_bytes() != before:
                raise RuntimeError('target config changed during isolated operation; refusing overwrite')
            atomic_write(config, data)
            if config.read_bytes() != data:
                raise RuntimeError('GUI config readback failed; session backup retained')
            synchronized = desired.exists()
            print(f"GUI_CONFIG_{'SYNCED' if synchronized else 'RESTORED'}: {config}", flush=True)
        result = {'vendor_exit_code': rc, 'interrupted': interrupted,
                  'gui_config_synchronized': synchronized, 'session': str(session)}
        if rc == 0 and (session / 'stage-before.json').exists():
            try:
                results.finish_stage(session)
                result['reports_synchronized'] = True
            except (OSError, ValueError, RuntimeError, ET.ParseError) as error:
                result['reports_synchronized'] = False
                result['report_error'] = str(error)
                (session / 'result.json').write_bytes(json_bytes(result))
                raise RuntimeError('report synchronization rejected: ' + str(error)) from error
        (session / 'result.json').write_bytes(json_bytes(result))
        if rc == 0 and not synchronized:
            raise RuntimeError(f'no effective config captured; build not accepted: {session}')
        return 130 if interrupted else (128 - rc if rc < 0 else rc)


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-stage', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--stage', choices=['syn', 'pnr'])
    parser.add_argument('--check-results', type=Path, help='validate an accepted result for this exact project')
    parser.add_argument('--capture', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--before-option', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--config-only', action='store_true', help='isolate metadata and forbid builds')
    parser.add_argument('--project', type=Path)
    parser.add_argument('--script', type=Path)
    parser.add_argument('--launcher', type=Path, default=Path('gw_sh'))
    parser.add_argument('script_args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        if args.before_stage:
            results.before_stage(args.before_stage, args.stage)
            return 0
        if args.check_results:
            checked = results.check_results(args.check_results, args.stage)
            if args.check_results.name == 'ethereal_cloud_tone.gprj':
                if not args.stage:
                    raise RuntimeError('AE350 result checks require explicit --stage syn|pnr')
                project_root = args.check_results.resolve().parent
                api = runpy.run_path(str(project_root / 'scripts/verify_ae350_itcm.py'))
                embedded = api['verify_project'](project_root, args.stage)
                if embedded['status'] != 'PASS':
                    raise RuntimeError('Actual AE350 ITCM differs from the compiled firmware')
                checked['ae350_embedded_firmware'] = embedded
            print(json.dumps(checked, ensure_ascii=False))
            return 0
        if args.capture or args.before_option:
            capture(args.capture or args.before_option, bool(args.before_option))
            return 0
        if not args.project or not args.script:
            parser.error('--project and --script are required')
        tail = args.script_args[1:] if args.script_args[:1] == ['--'] else args.script_args
        return run(args.project, args.script, tail, args.launcher, args.config_only)
    except (OSError, ValueError, RuntimeError) as error:
        print(f'GUI_CONFIG_SYNC_ERROR: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
