#!/usr/bin/env python3
"""Check and recapture frozen current C JSON, then exercise the actual App parser/codec."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
from toolchain import ROOT, JDK_BIN
from fm_contract import check_fm_contract

BUNDLE_DIRECTORY = 'contracts/s3-control-ports-20261026'
MANIFEST_SHA256 = '180178ab639a79d5136378b259769ad5c590ae6a4fd0a49c26840bfc0f3f15cb'
SCRATCH = ROOT / 'build/fm-current'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_bundle(bundle):
    if bundle.is_symlink() or not bundle.is_dir():
        raise ValueError('missing or linked current C fixture bundle')
    if sha256(bundle / 'manifest.json') != MANIFEST_SHA256:
        raise ValueError('current C fixture manifest identity changed')
    manifest = json.loads((bundle / 'manifest.json').read_text(encoding='utf-8'))
    expected = {entry['path'] for entry in manifest['files']}
    actual = set()
    for path in bundle.rglob('*'):
        if path.is_symlink():
            raise ValueError('linked current C fixture input: ' + str(path))
        if path.is_file():
            actual.add(path.relative_to(bundle).as_posix())
    if actual != expected | {'manifest.json'} or len(expected) != len(manifest['files']):
        raise ValueError('current C fixture file set changed')
    for entry in manifest['files']:
        path = pathlib.PurePosixPath(entry['path'])
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('invalid fixture manifest path')
        source = bundle / path
        if source.stat().st_size != entry['bytes'] or sha256(source) != entry['sha256']:
            raise ValueError('current C fixture SHA drift: ' + str(path))
    if len(manifest['fixtures']) != 12 or len(manifest['handler_capture']['fixtures']) != 8:
        raise ValueError('current C fixture coverage changed')
    print('Current C fixture manifest/SHA/file-set passed: 12 control API + 8 HTTP handler fixtures', flush=True)
    return manifest


def recapture(bundle, manifest):
    out = pathlib.Path(tempfile.mkdtemp(prefix='recapture-', dir=SCRATCH))
    source = bundle / 's3'
    objects = []
    for index, path in enumerate(manifest['capture_sources']):
        obj = out / (str(index) + '.o')
        subprocess.run(['cc', *manifest['compile_flags'], '-c', path, '-o', str(obj)], cwd=source, check=True)
        objects.append(obj)
    capture = out / 'capture'
    subprocess.run(['cc', *map(str, objects), *manifest['link_flags'], '-o', str(capture)], cwd=source, check=True)
    target = out / 'fixtures'
    target.mkdir()
    subprocess.run([str(capture), str(target)], cwd=source, check=True)
    handler_obj = out / 'handler.o'
    subprocess.run(['cc', *manifest['compile_flags'], '-c', manifest['handler_capture']['source'],
                    '-o', str(handler_obj)], cwd=source, check=True)
    handler = out / 'handler'
    subprocess.run(['cc', str(handler_obj), *map(str, objects[1:]), *manifest['link_flags'], '-o', str(handler)], cwd=source, check=True)
    http_target = out / 'http-v2-fixtures'
    http_target.mkdir()
    subprocess.run([str(handler), *[str(http_target / name) for name in manifest['handler_capture']['fixtures']]], cwd=source, check=True)
    for directory, names in [('fixtures', manifest['fixtures']), ('http-v2-fixtures', manifest['handler_capture']['fixtures'])]:
        for name in names:
            if (out / directory / name).read_bytes() != (bundle / directory / name).read_bytes():
                raise AssertionError('current C serializer byte drift: ' + directory + '/' + name)
    print('Current frozen C sources recaptured 20/20 byte-exact JSON fixtures', flush=True)


def guard_regression(bundle):
    for mutation in ('fixture', 'source', 'manifest', 'extra'):
        target = pathlib.Path(tempfile.mkdtemp(prefix='guard-' + mutation + '-', dir=SCRATCH)) / 'bundle'
        shutil.copytree(bundle, target)
        if mutation == 'extra':
            (target / 'unexpected.json').write_text('{}\n', encoding='utf-8')
        else:
            path = {'fixture': 'fixtures/fm-presets.json', 'source': 's3/main/control_api.c',
                    'manifest': 'manifest.json'}[mutation]
            with (target / path).open('ab') as stream:
                stream.write(b' ')
        try:
            check_bundle(target)
        except ValueError:
            continue
        raise AssertionError('accepted current fixture drift: ' + mutation)
    print('Current fixture guard rejected changed source/fixture/manifest/extra file', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-recapture', action='store_true')
    args = parser.parse_args()
    check_fm_contract()
    bundle = ROOT / BUNDLE_DIRECTORY
    manifest = check_bundle(bundle)
    SCRATCH.mkdir(parents=True, exist_ok=True)
    guard_regression(bundle)
    if not args.skip_recapture:
        recapture(bundle, manifest)
    out = pathlib.Path(tempfile.mkdtemp(prefix='classes-', dir=SCRATCH))
    package = ROOT / 'core/src/main/java/cn/fpgasynth/control/core'
    sources = [package / name for name in ('Json.java', 'Protocol.java', 'DeviceState.java', 'Transport.java', 'FmApi.java')]
    sources += [ROOT / 'core/src/generated/java/cn/fpgasynth/control/generated/FmContract.java',
                ROOT / 'core/src/test/java/cn/fpgasynth/control/core/FmCurrentTest.java']
    subprocess.run([str(JDK_BIN / 'javac'), '-encoding', 'UTF-8', '--release', '8', '-Xlint:all',
                    '-d', str(out), *map(str, sources)], check=True)
    historical = ROOT / 'contracts/s3-http-v2-20261011/s3/docs/fm/http-v2-fixtures'
    subprocess.run([str(JDK_BIN / 'java'), '-ea', '-cp', str(out),
                    'cn.fpgasynth.control.core.FmCurrentTest', str(bundle / 'fixtures'), str(historical)], check=True)
    print('Current C/App FM contract, Catalog.load, encode and runtime rejection gates PASS; no hardware/audio claim', flush=True)


if __name__ == '__main__':
    main()
