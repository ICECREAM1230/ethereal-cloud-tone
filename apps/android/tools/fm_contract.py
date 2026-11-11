"""Verify the frozen owner-generated contract without modifying any input."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
from toolchain import ROOT

CONTRACT_DIRECTORY = 'contracts/fm-control-v1.0.1-20261026'
CONTRACT_MANIFEST_SHA256 = '92136d13e8d4e6bde28b133b37b632422884bf30fe8a61a20922433e873d0708'
HISTORICAL_CONTRACT_DIRECTORY = 'contracts/fm-control-v1.0.1'
HISTORICAL_MANIFEST_SHA256 = 'ef14ff8db02263bb3caa1f5ae04165021e255abaf33ece19a91ee5719e694234'
CURRENT_HTTP_FIXTURES = 'contracts/s3-control-ports-20261026/http-v2-fixtures'
HISTORICAL_HTTP_FIXTURES = 'contracts/s3-http-v2-20261011/s3/docs/fm/http-v2-fixtures'
GENERATED_JAVA = 'generated/java/cn/fpgasynth/control/generated/FmContract.java'

class ContractError(ValueError):
    pass

def _relative(path):
    value = pathlib.PurePosixPath(path)
    if value.is_absolute() or '..' in value.parts or not value.parts:
        raise ContractError('invalid manifest path: ' + str(path))
    return value

def _files(directory):
    if not directory.is_dir() or directory.is_symlink():
        raise ContractError('missing or linked contract directory: ' + str(directory))
    files = set()
    for path in directory.rglob('*'):
        if path.is_symlink():
            raise ContractError('linked contract input: ' + str(path))
        if path.is_file():
            files.add(path.relative_to(directory).as_posix())
    return files

def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def _check_contract(root, directory, manifest_sha256, check_app_generated):
    root = pathlib.Path(root).resolve()
    bundle = root / directory
    if _sha(bundle / 'manifest.json') != manifest_sha256:
        raise ContractError('frozen manifest identity changed: ' + directory)
    manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
    entries = manifest['files']
    expected = {str(_relative(item['path'])) for item in entries}
    if len(expected) != len(entries) or _files(bundle) != expected | {'manifest.json'}:
        raise ContractError('frozen contract file set changed (missing or extra input)')
    for item in entries:
        source = bundle / _relative(item['path'])
        if source.stat().st_size != item['bytes'] or _sha(source) != item['sha256']:
            raise ContractError('frozen contract SHA drift: ' + item['path'])
    if (_sha(bundle / 'schema/fm-control.schema.json') != manifest['schema_sha256']
            or _sha(bundle / 'schema/backend-profiles.json') != manifest['profiles_sha256']):
        raise ContractError('frozen schema/profile identity changed')
    combined = hashlib.sha256(b'FM_CONTROL_IDENTITY_V1\0'
                              + bytes.fromhex(manifest['schema_sha256'])
                              + bytes.fromhex(manifest['profiles_sha256'])).hexdigest()
    if combined != manifest['combined_contract_sha256']:
        raise ContractError('combined frozen contract identity changed')

    products = manifest['app_generated_files']
    generated = root / 'core/src/generated/java'
    actual = {'core/src/generated/java/' + p for p in _files(generated)} if check_app_generated else None
    targets = {str(_relative(item['path'])) for item in products}
    if len(targets) != len(products) or (check_app_generated and actual != targets):
        raise ContractError('App generated file set changed (missing or extra output)')
    for item in products:
        source = bundle / _relative(item['source'])
        target = root / _relative(item['path'])
        if _sha(source) != item['sha256'] or (check_app_generated and _sha(target) != item['sha256']):
            raise ContractError('App generated Java SHA drift: ' + item['path'])

    command = [sys.executable, '-B', str(bundle / 'tools/gen_fm_contract.py'),
               '--schema', str(bundle / 'schema/fm-control.schema.json'),
               '--profiles', str(bundle / 'schema/backend-profiles.json'),
               '--out', str(bundle / 'generated'), '--check']
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    if result.returncode:
        raise ContractError('owner generator --check failed: ' + result.stdout + result.stderr)
    print('FM frozen contract manifest/SHA/file-set/owner --check passed: ' + directory
          + '; runtime/hardware gates are checked separately')
    return manifest


def check_fm_contract(root=ROOT):
    return _check_contract(root, CONTRACT_DIRECTORY, CONTRACT_MANIFEST_SHA256, True)


def check_historical_fm_contract(root=ROOT):
    """Only legacy regression compiles this pinned closure; it is never an active App fallback."""
    return _check_contract(root, HISTORICAL_CONTRACT_DIRECTORY, HISTORICAL_MANIFEST_SHA256, False)


def core_sources(root=ROOT, contract_directory=None):
    root = pathlib.Path(root)
    generated = ([root / contract_directory / GENERATED_JAVA] if contract_directory
                 else sorted((root / 'core/src/generated/java').rglob('*.java')))
    return sorted((root / 'core/src/main/java').rglob('*.java')) + generated

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    try:
        check_fm_contract(args.root)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print('FM contract check failed: ' + str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
