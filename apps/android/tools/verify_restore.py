#!/usr/bin/env python3
"""Restore a sealed owned snapshot into a fresh local directory, verify all hashes, rebuild offline."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import tarfile
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('name');args=p.parse_args()
    if '/' in args.name or '..' in args.name:raise SystemExit('invalid snapshot name')
    source=ROOT/'snapshots'/args.name
    meta=json.loads((source/'manifest.json').read_text())
    archive=source/'mobile-app.tar.gz'
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=meta['archive_sha256']:raise SystemExit('archive checksum failed')
    dest=ROOT/'build'/('restore-'+args.name)
    if dest.exists():raise SystemExit('restore destination already exists; preserve it and choose another milestone')
    dest.mkdir(parents=True)
    with tarfile.open(archive) as stream:stream.extractall(dest,filter='data')
    for item in meta['files']:
        f=dest/item['path']
        if hashlib.sha256(f.read_bytes()).hexdigest()!=item['sha256']:raise SystemExit('file mismatch: '+item['path'])
    app=dest/meta.get('archive_root','mobile-app');lines=[f"Archive SHA256 verified: {meta['archive_sha256']}",f"Restored file SHA256 verified: {len(meta['files'])}"]
    def payload(path):
        with zipfile.ZipFile(path) as stream:
            return {n:hashlib.sha256(stream.read(n)).hexdigest() for n in stream.namelist() if not n.startswith('META-INF/')}
    apks={}
    current_version=json.loads((app/'delivery/build-info.json').read_text())['version']
    for info in ('build-info.json','build-info-pads.json'):
        file=app/'delivery'/info
        if file.exists():
            build=json.loads(file.read_text())
            if build['version']!=current_version:continue
            name='fpga-synth-'+build['version']+('-pads' if build.get('variant')=='pads' else '')+'.apk'
            apks[name]=payload(app/'delivery'/name)
    # An explicit installed toolchain is reused read-only; setup always writes locally.
    import os
    scripts=[['test_fm_guard.py'],['test.py'],['test_http.py'],['test_abi.py'],['test_fm_api.py'],['test_fm_runtime.py'],['test_http_v2_handler.py'],['build.py'],['build_smoke.py']]
    if any('-pads.apk' in name for name in apks):scripts.append(['build.py','--variant','pads'])
    if not os.environ.get('FPGA_APP_TOOLCHAIN'):scripts.insert(0,['setup.py'])
    for arguments in scripts:
        script=arguments[0];command=['python3',str(app/'tools'/script),*arguments[1:]]
        if script=='test_http_v2_handler.py':
            vectors=app/'docs/console-20261028/fm-http-vectors-historical.json'
            if not vectors.exists():vectors=app/'docs/fm-runtime-20261011/app-http-vectors.json'
            command+=['--vectors',str(vectors)]
        r=subprocess.run(command,cwd=app,capture_output=True,text=True)
        lines.append('=== '+script+' ===\n'+r.stdout+r.stderr)
        if r.returncode:
            (source/'恢复验证.txt').write_text('\n'.join(lines)+'\n');raise SystemExit('restored validation failed: '+script)
        print('Restored',script,'PASS',flush=True)
    for name,expected in apks.items():
        if payload(app/'delivery'/name)!=expected:raise SystemExit('rebuilt APK payload mismatch: '+name)
        lines.append('Rebuilt APK entries identical (excluding META-INF signing entries): '+name)
    lines.append('RESULT restored isolated source and pinned offline build toolchain verified; no hardware operation')
    (source/'恢复验证.txt').write_text('\n'.join(lines)+'\n')
    print(lines[-1])
if __name__=='__main__':main()
