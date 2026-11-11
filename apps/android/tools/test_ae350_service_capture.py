#!/usr/bin/env python3
"""Recompile the frozen AE350 common C API and compare its exact host fixtures.

Only system Python3/cc/libc/libm/zlib/pthreads are required. No ESP-IDF, network,
device, source tree outside the bundle, or Java/Android build is used. The owner
observations are an explicit host model, not physical adoption or audio proof.
"""
import argparse
import csv
import datetime
import hashlib
import json
import os
import pathlib
import resource
import subprocess
import sys
import tempfile

APP_ROOT = pathlib.Path(__file__).resolve().parents[1]
BUNDLE = APP_ROOT / 'contracts/ae350-service-20261102'
BUILD = APP_ROOT / 'build/ae350-service-capture'
DEPS = (
    'main/control_source.c', 'main/web_control.c', 'main/patch.c',
    'main/web_raw_queue.c', 'main/web_raw_midi.c', 'main/fdc_control.c',
    'main/ks_timbre_control.c', 'main/ks_timbre_params.c',
    'main/ks_timbre_document.c', 'main/vendor/tomlc17/tomlc17.c',
    'tests/host/fake_queue.c', 'tests/host/fake_dds_presets.c',
)
FLAGS = (
    '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
    '-ffunction-sections', '-fdata-sections', '-DCONFIG_FPGA_AE350_CONTROL=1',
    '-Itests/host/http_stubs', '-Itests/host/stubs', '-Imain', '-Itests/host',
)
HTTP_FIXTURES = (
    'v2-capabilities', 'v2-state', 'v2-catalog', 'v2-catalog-page-8',
    'v2-profile', 'v2-presets', 'v2-live-capabilities', 'v2-live-state',
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(value):
    path = pathlib.PurePosixPath(value)
    if not value or path.is_absolute() or '..' in path.parts or path.as_posix() != value:
        raise ValueError('unsafe manifest path: ' + repr(value))
    return path


def source_identity(provenance):
    values = [(entry['origin_path'], entry['sha256_before'])
              for entry in provenance['files']]
    return digest(''.join(path + '\0' + sha + '\n'
                          for path, sha in sorted(values)).encode('utf-8'))


def check_inputs(verify_origin=False):
    """Read-only: a missing, extra, linked, or changed bundle is an error."""
    if not BUNDLE.is_dir() or BUNDLE.is_symlink():
        raise ValueError('missing or linked frozen bundle: ' + str(BUNDLE))
    manifest_path = BUNDLE / 'manifest.json'
    if manifest_path.is_symlink():
        raise ValueError('linked manifest')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest['format'] != 1:
        raise ValueError('unsupported AE350 capture manifest')
    expected = {str(safe_relative(entry['path'])) for entry in manifest['files']}
    if len(expected) != len(manifest['files']) or 'manifest.json' in expected:
        raise ValueError('duplicate manifest paths')
    expected.add('manifest.json')
    actual = set()
    for path in BUNDLE.rglob('*'):
        if path.is_symlink():
            raise ValueError('linked frozen input: ' + str(path))
        if path.is_file():
            actual.add(path.relative_to(BUNDLE).as_posix())
        elif not path.is_dir():
            raise ValueError('nonregular frozen input: ' + str(path))
    if actual != expected:
        raise ValueError('frozen file set changed: missing=' + repr(sorted(expected-actual))
                         + '; extra=' + repr(sorted(actual-expected)))
    for entry in manifest['files']:
        raw = (BUNDLE / entry['path']).read_bytes()
        if len(raw) != entry['bytes'] or digest(raw) != entry['sha256']:
            raise ValueError('frozen SHA256/length drift: ' + entry['path'])
    script = pathlib.Path(__file__).resolve()
    if digest(script.read_bytes()) != manifest['capture_script_sha256']:
        raise ValueError('capture script differs from its frozen binding')
    provenance = json.loads((BUNDLE/'provenance.json').read_text(encoding='utf-8'))
    if not (provenance['source_freeze_before_after_equal']
            and provenance['dependency_sets_before_after_equal']):
        raise ValueError('source did not freeze at a stable boundary')
    for entry in provenance['files']:
        raw = (BUNDLE/str(safe_relative(entry['bundle_path']))).read_bytes()
        if digest(raw) != entry['sha256_before'] or entry['sha256_before'] != entry['sha256_after']:
            raise ValueError('production source provenance drift: ' + entry['origin_path'])
        if verify_origin:
            origin = pathlib.Path(provenance['origin_root'])/str(safe_relative(entry['origin_path']))
            if origin.is_symlink() or not origin.is_file() or digest(origin.read_bytes()) != entry['sha256_before']:
                raise ValueError('live origin drift/missing: ' + str(origin))
    if source_identity(provenance) != manifest['source_set_sha256']:
        raise ValueError('source set identity differs from manifest')
    return manifest, provenance


def make_trace(directory):
    entries = []
    with (directory/'requests.tsv').open(encoding='utf-8', newline='') as stream:
        for row in csv.DictReader(stream, delimiter='\t'):
            if row['body_kind'] not in ('null', 'string'):
                raise ValueError('unknown body kind')
            body = None if row['body_kind'] == 'null' else row['body']
            raw = (directory/(row['name']+'.json')).read_bytes()
            entry = dict(name=row['name'], request=dict(path=row['path'], body=body),
                         response=dict(code=int(row['code']), fixture='fixtures/'+row['name']+'.json',
                                       body_bytes=len(raw), sha256=digest(raw)),
                         observation=row['observation'], published_events=int(row['publications']))
            if row['path'] == '/api/v2/command':
                words = [int(word) for word in body.split(' ')]
                if len(words) != 7 or any(not 0 <= word <= 0xffffffff for word in words):
                    raise ValueError('v2 command is not exactly seven uint32 words')
                entry['command_words'] = words
            entries.append(entry)
    return dict(format=1, boundary='Production core prepare/emit; all owner observations are explicit '
                'HOST OWNER MODEL. No physical FPGA adoption, device discovery, or audio claim.',
                entries=entries)


def check_semantics(directory):
    load = lambda name: json.loads((directory/(name+'.json')).read_text(encoding='utf-8'))
    caps, session = load('capabilities'), load('session')
    client, token, context = session['client'], session['token'], session['context_epoch']
    assert caps['cpu_mode'] == dict(version=1, route_only=True, modes=[0, 1, 2], channel_routes=True)
    assert client == 0 and token == 1001 and context == 9
    assert not next(engine for engine in caps['engines'] if engine['id'] == 2)['available']
    assert not any(profile['runtime_available'] for profile in caps['catalog_profiles'])
    for path in directory.glob('*.json'):
        state = json.loads(path.read_text(encoding='utf-8'))
        if 'clients' not in state:
            continue
        assert state['mode']['requested'] == state['mode']['applied'] == 0, path.name
        assert state['cpu_mode']['desired'] == state['cpu_mode']['applied'] == 0, path.name
        assert not state['fm']['available'], path.name
        assert state['clients'][client]['token'] == token, path.name
    initial = load('state-initial-client-ks')['clients'][client]
    assert initial['midi_channel'] == 1 and initial['parameter_processed'] == 0
    for label, sequence, previous, target in (
            ('select-sub', 2, 1, 0), ('select-ks', 6, 0, 1), ('select-fm', 8, 1, 2)):
        pending = load(label+'-pending')['clients'][client]
        adopted = load(label+'-adopted')['clients'][client]
        assert pending['received'] == sequence and pending['processed'] < sequence
        assert pending['midi_channel'] == previous
        assert adopted['processed'] == adopted['parameter_processed'] == adopted['result_sequence'] == sequence
        assert adopted['midi_channel'] == target and adopted['result_context_epoch'] == context
        assert adopted['result_code'] == 0
    assert load('keys-on-pending')['clients'][client]['keys'] == 0
    assert load('keys-on-adopted')['clients'][client]['keys'] == 1
    assert load('keys-off-adopted')['clients'][client]['keys'] == 0
    assert load('keys-on-adopted')['clients'][client]['parameter_processed'] == 2
    assert load('cc7-adopted')['clients'][client]['parameter_processed'] == 5
    assert load('cc7-adopted')['legacy']['volume'] == 192
    assert load('ks-decay-adopted')['clients'][client]['parameter_processed'] == 7
    assert load('ks-decay-adopted')['legacy']['ks_decay_ms'] == 4200
    assert load('state-cpu-not-ready')['cpu_mode']['ready'] is False
    assert load('state-cpu-not-ready')['cpu_mode']['busy'] is True
    assert load('cpu-not-ready-select-adopted')['clients'][client]['midi_channel'] == 1
    assert load('state-cpu-fault')['cpu_mode']['fault'] is True
    assert load('state-cpu-invalid')['cpu_mode']['valid'] is False
    assert load('state-input-stop-pending')['legacy']['inputs']['stop_pending'] is True
    final = load('final-clear-adopted')['clients'][client]
    assert final['received'] == final['processed'] == final['result_sequence'] == 10
    assert final['parameter_processed'] == 9 and final['keys'] == 0
    trace = make_trace(directory)
    by_name = {entry['name']:entry for entry in trace['entries']}
    assert by_name['session-get-rejected']['request']['body'] is None
    assert by_name['state-post-rejected']['request']['body'] == ''
    assert by_name['session']['request']['body'] == ''
    assert by_name['cpu-not-ready-select-received']['response']['code'] == 202
    for name in ('fm-native-parameter-rejected', 'cpu-fault-select-rejected',
                 'cpu-invalid-select-rejected', 'cc7-sub-patch-invalid-rejected',
                 'owner-unavailable-keys-rejected'):
        assert by_name[name]['response']['code'] == 503 and by_name[name]['published_events'] == 0
    return trace


def run_capture(manifest, verify_origin=False):
    BUILD.mkdir(parents=True, exist_ok=True)
    run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=BUILD))
    temporary = run/'tmp'; temporary.mkdir()
    env = dict(os.environ, TMPDIR=str(temporary), PYTHONDONTWRITEBYTECODE='1')
    source = BUNDLE/'source'
    summary = dict(boundary=manifest['boundary'], source_set_sha256=manifest['source_set_sha256'],
                   started_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   result='FAIL', runs=[])
    executables = []
    try:
        for group, entry in (
                ('original-core', 'tests/host/capture_control_api.c'),
                ('http-v2', 'tests/host/test_http_v2.c'),
                ('fixtures', '../capture/capture_service.c')):
            output = run/group; output.mkdir()
            executable = run/(group+'.host'); executables.append(executable)
            command = ['cc', *FLAGS, entry, *DEPS, '-Wl,--gc-sections', '-lm', '-lz',
                       '-pthread', '-o', str(executable)]
            (run/(group+'.command.json')).write_text(json.dumps(command, ensure_ascii=False, indent=2)+'\n')
            with (run/(group+'.compile.log')).open('wb') as log:
                subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            arguments = [str(output/(name+'.json')) for name in HTTP_FIXTURES] if group == 'http-v2' else [str(output)]
            with (run/(group+'.log')).open('wb') as log:
                subprocess.run([str(executable), *arguments], cwd=source, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            expected = {entry['path'].split('/', 1)[1] for entry in manifest['files']
                        if entry['path'].startswith(group+'/')}
            actual = {path.name for path in output.iterdir() if path.is_file()}
            if expected != actual:
                raise ValueError(group+' generated a different fixture set')
            for name in sorted(expected):
                if (output/name).read_bytes() != (BUNDLE/group/name).read_bytes():
                    raise ValueError('C output differs byte-for-byte: '+group+'/'+name)
            summary['runs'].append(dict(group=group, exact_files=len(expected), exit_code=0))
        trace = check_semantics(run/'fixtures')
        if trace != json.loads((BUNDLE/'trace.json').read_text(encoding='utf-8')):
            raise ValueError('request trace differs from its frozen identity')
        (run/'trace.json').write_text(json.dumps(trace, ensure_ascii=False, indent=2)+'\n')
        check_inputs(verify_origin=verify_origin)
        summary.update(result='PASS', service_requests=len(trace['entries']),
                       origin_verified=verify_origin, semantics='PASS')
    finally:
        # Only known executable outputs and this run's empty compiler temp directory.
        removed = []
        for executable in executables:
            if executable.is_file():
                executable.unlink(); removed.append(executable.name)
        if not any(temporary.iterdir()):
            temporary.rmdir(); removed.append('tmp/')
        summary['cleanup_removed'] = removed
        (run/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n')
        print('Summary: '+str(run/'summary.json'), flush=True)
    print('AE350 frozen C service PASS: '+str(sum(item['exact_files'] for item in summary['runs']))
          +' exact files; '+str(summary['service_requests'])+' requests; explicit HOST OWNER MODEL', flush=True)
    return run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only', action='store_true', help='read-only hash/set/provenance check')
    parser.add_argument('--verify-origin', action='store_true', help='also require the live source to match')
    args = parser.parse_args()
    manifest, _ = check_inputs(verify_origin=args.verify_origin)
    print('Frozen source_set_sha256: '+manifest['source_set_sha256'], flush=True)
    if args.check_only:
        print('AE350 bundle/hash/provenance check PASS (read-only)', flush=True)
    else:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        run_capture(manifest, verify_origin=args.verify_origin)


if __name__ == '__main__':
    main()
