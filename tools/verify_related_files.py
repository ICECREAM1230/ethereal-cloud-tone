#!/usr/bin/env python3
"""Read-only verification of published skills, artifacts and historical experiment files."""
from pathlib import Path
import hashlib,json,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while block:=stream.read(1024*1024):h.update(block)
    return h.hexdigest()

def run(relative,*arguments,cwd=None):
    subprocess.run([sys.executable,'-B',str(ROOT/relative),*arguments],cwd=cwd or ROOT,check=True)

run('tools/verify_project.py')
run('skills/_tools/verify_bundle.py')
experiment=ROOT/'experiments/ks_fm_20261102'
run('experiments/ks_fm_20261102/06_离线复现/离线入口.py','verify',cwd=experiment)
manifest=json.loads((ROOT/'artifacts/manifest.json').read_text())
for entry in manifest['files']:
    path=ROOT/entry['path']
    assert path.resolve().is_relative_to(ROOT),entry['path']
    assert path.is_file() and not path.is_symlink()
    assert path.stat().st_size==entry['bytes'] and sha(path)==entry['sha256'],entry['path']
technical_index=ROOT/'docs/technical/SHA256SUMS'
technical_status='DEFERRED_BY_USER'
if technical_index.is_file():
    for line in technical_index.read_text().splitlines():
        expected,name=line.split('  ',1)
        path=ROOT/'docs/technical'/name
        assert path.resolve().is_relative_to((ROOT/'docs/technical').resolve()) and path.is_file()
        assert sha(path)==expected,name
    technical_status='PASS'
print(json.dumps({'status':'PASS','published_asset_entries':len(manifest['files']),'skills':'PASS','historical_ks_fm_files':'PASS','technical_documents':technical_status,'scope':'File integrity and selected project inputs only; no builds, installation, programming or hardware acceptance'},ensure_ascii=False,indent=2))
