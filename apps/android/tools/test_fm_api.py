#!/usr/bin/env python3
"""Test current/historical frozen C HTTP JSON and isolated owner profile regeneration."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
from toolchain import ROOT, JDK_BIN
from fm_contract import (CONTRACT_DIRECTORY, HISTORICAL_CONTRACT_DIRECTORY,
                         CURRENT_HTTP_FIXTURES, HISTORICAL_HTTP_FIXTURES,
                         check_fm_contract, check_historical_fm_contract)

SCRATCH = ROOT / 'build/fm-current/api'
GENERATED_JAVA = pathlib.Path('java/cn/fpgasynth/control/generated/FmContract.java')
DEFAULT_FIXTURES = ROOT / CURRENT_HTTP_FIXTURES


def run_java(label, generated, fixtures, group='all'):
    out = pathlib.Path(tempfile.mkdtemp(prefix=label + '-classes-', dir=SCRATCH))
    package = ROOT / 'core/src/main/java/cn/fpgasynth/control/core'
    sources = [package / name for name in ('Json.java', 'Protocol.java', 'DeviceState.java', 'Transport.java', 'FmApi.java')]
    sources += [generated, ROOT / 'core/src/test/java/cn/fpgasynth/control/core/FmApiTest.java']
    print('Variant: ' + label + '; JSON is host/synthetic input, not hardware discovery', flush=True)
    subprocess.run([str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8', '-Xlint:all',
                    '-d', str(out), *map(str, sources)], check=True)
    subprocess.run([str(JDK_BIN / 'java'), '-ea', '-cp', str(out),
                    'cn.fpgasynth.control.core.FmApiTest', str(fixtures), group], check=True)


def synthetic_fixture(source, directory, schema, profiles, profile, identity, heterogeneous=False):
    directory.mkdir()
    for file in sorted(source.glob('*.json')):
        raw = json.loads(file.read_text(encoding='utf-8'))
        def change(value):
            if isinstance(value, dict):
                for key in ('profiles_sha256', 'profile_sha256'):
                    if key in value and value[key] is not None:
                        value[key] = identity['profiles']
                if 'contract_sha256' in value:
                    value['contract_sha256'] = identity['contract']
                if value.get('backend_id') == 1:
                    if 'profile_id' in value: value['profile_id'] = 99
                    if 'id' in value and 'key' in value: value['id'] = 99
                for item in value.values(): change(item)
            elif isinstance(value, list):
                for item in value: change(item)
        change(raw)
        if heterogeneous:
            common = dict(api_major=2, profiles_sha256=identity['profiles'], profile_id=profile['id'],
                          backend_id=profile['backend_id'], profile_version=profile['profile_version'], runtime_available=False)
            if file.name == 'v2-profile.json':
                raw = dict(common, key=profile['key'], voice_count=profile['voice_count'],
                           operators_per_voice=profile['operators_per_voice'], patch_operator_count=profile['patch_operator_count'],
                           operator_labels=profile['operator_labels'], parameter_count=1, preset_count=0)
            elif file.name in ('v2-catalog.json', 'v2-catalog-page-8.json'):
                p = profile['parameters'][0]
                item = {key: p[key] for key in ('id', 'name', 'minimum', 'maximum')}
                item.update(scope_id=schema['enums']['scope'][p['scope']], unit_id=schema['catalog']['units'][p['unit']],
                            apply_time_id=schema['catalog']['apply_times'][p['apply_time']],
                            value_type_id=schema['catalog']['value_types'][p['value_type']])
                raw = dict(common, section='parameters', offset=0, total=1, next_offset=None, items=[item])
            elif file.name == 'v2-presets.json':
                raw = dict(common, section='presets', offset=0, total=0, next_offset=None, items=[])
            elif file.name == 'v2-live-state.json':
                raw['fm'].update(profile_id=profile['id'], backend_id=profile['backend_id'], profile_version=profile['profile_version'],
                                 voice_count=profile['voice_count'], operators_per_voice=profile['operators_per_voice'],
                                 patch_operator_count=profile['patch_operator_count'], parameter_values=[])
        (directory / file.name).write_text(json.dumps(raw, separators=(',', ':')) + '\n', encoding='utf-8')
    (directory / 'SYNTHETIC.txt').write_text(
        'Synthetic fixture derived from frozen host JSON and isolated official generation. No device discovery or audio proof.\n',
        encoding='utf-8')


def regenerate(fixtures, heterogeneous=False):
    directory = pathlib.Path(tempfile.mkdtemp(prefix='heterogeneous-' if heterogeneous else 'profile99-', dir=SCRATCH))
    bundle = ROOT / CONTRACT_DIRECTORY
    schema_path = directory / 'fm-control.schema.json'
    profile_path = directory / 'backend-profiles.json'
    shutil.copy2(bundle / 'schema/fm-control.schema.json', schema_path)
    profiles = json.loads((bundle / 'schema/backend-profiles.json').read_text(encoding='utf-8'))
    profiles['profiles'][0]['id'] = 99
    if heterogeneous:
        profile = profiles['profiles'][1]
        profile['operators_per_voice'] = [2, 4, 1, 0, 4, 2, 3, 1]
        profile['operator_labels'] = ['op0', 'op1', 'op2', 'op3']
        profile['parameters'] = [dict(id=200, name='synthetic_operator_code', minimum=0, maximum=63,
                                      scope='VOICE_OPERATOR', unit='chip_code', apply_time='next_note', value_type='u32')]
    else:
        profile = profiles['profiles'][0]
    profile_path.write_text(json.dumps(profiles, indent=2) + '\n', encoding='utf-8')
    tools = directory / 'tools'
    shutil.copytree(bundle / 'tools', tools)
    out = directory / 'generated'
    command = [sys.executable, '-B', str(tools / 'gen_fm_contract.py'),
               '--schema', str(schema_path), '--profiles', str(profile_path), '--out', str(out)]
    subprocess.run(command, check=True)
    subprocess.run(command + ['--check'], check=True)
    schema = json.loads(schema_path.read_text(encoding='utf-8'))
    if {path.relative_to(out).as_posix() for path in out.rglob('*') if path.is_file()} != set(schema['codegen']['outputs']):
        raise AssertionError('official generator did not produce exactly five outputs')
    sd, pd = hashlib.sha256(schema_path.read_bytes()).digest(), hashlib.sha256(profile_path.read_bytes()).digest()
    identity = dict(schema=sd.hex(), profiles=pd.hex(),
                    contract=hashlib.sha256(b'FM_CONTROL_IDENTITY_V1\0' + sd + pd).hexdigest())
    print('Synthetic generated identity: ' + json.dumps(identity, separators=(',', ':')), flush=True)
    target = directory / 'fixtures'
    synthetic_fixture(fixtures, target, schema, profiles, profile, identity, heterogeneous)
    (directory / 'synthetic-identity.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    run_java('heterogeneous-VOICE_OPERATOR' if heterogeneous else 'profile99-backend1',
             out / GENERATED_JAVA, target, 'layout' if heterogeneous else 'all')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', type=pathlib.Path, default=DEFAULT_FIXTURES)
    parser.add_argument('--group', choices=('all', 'u32', 'caps', 'state', 'catalog', 'encode'), default='all')
    parser.add_argument('--skip-synthetic', action='store_true', help='run only actual frozen host fixtures')
    args = parser.parse_args()
    current = check_fm_contract()
    historical = check_historical_fm_contract()
    sys.stdout.flush()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    generated = ROOT / 'core/src/generated' / GENERATED_JAVA
    before = hashlib.sha256(generated.read_bytes()).digest()
    identity = json.loads((args.fixtures / 'v2-live-capabilities.json').read_text(encoding='utf-8'))
    def matches(manifest):
        return all(identity[key] == manifest[value] for key, value in (
            ('schema_sha256', 'schema_sha256'), ('profiles_sha256', 'profiles_sha256'),
            ('contract_sha256', 'combined_contract_sha256')))
    if matches(current):
        run_java('current-frozen-handler', generated, args.fixtures, args.group)
    elif matches(historical):
        run_java('explicit-historical-handler', ROOT / HISTORICAL_CONTRACT_DIRECTORY / 'generated' / GENERATED_JAVA,
                 args.fixtures, args.group)
    else:
        raise AssertionError('fixture identity is neither pinned current nor pinned historical contract')
    if args.fixtures.resolve() != (ROOT / HISTORICAL_HTTP_FIXTURES).resolve():
        run_java('historical-frozen-handler', ROOT / HISTORICAL_CONTRACT_DIRECTORY / 'generated' / GENERATED_JAVA,
                 ROOT / HISTORICAL_HTTP_FIXTURES, args.group)
    if args.group == 'all' and not args.skip_synthetic:
        regenerate(args.fixtures)
        regenerate(args.fixtures, heterogeneous=True)
    if before != hashlib.sha256(generated.read_bytes()).digest():
        raise AssertionError('production generated Java was modified')
    print('Production generated Java unchanged; all temporary inputs/outputs stayed in build/fm-current/api', flush=True)


if __name__ == '__main__':
    main()
