#!/usr/bin/env python3
"""Project-local pinned toolchain, no system installation or settings changes."""
import hashlib
import json
import pathlib
import tarfile
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = ROOT / '.toolchain'

def main():
    CACHE.mkdir(exist_ok=True)
    for package in json.loads((ROOT/'tools/toolchain-lock.json').read_text()):
        name = package['name']
        dest = CACHE/name
        marker = dest/'.ready'
        if marker.exists() and marker.read_text().strip() == package['sha256']:
            print('Available:', name)
            continue
        archive = CACHE/(name+('.tar.gz' if name=='jdk' else '.zip'))
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest()!=package['sha256']:
            print('Downloading:', name, flush=True)
            urllib.request.urlretrieve(package['url'], archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=package['sha256']:
            raise SystemExit('Checksum failed: '+name)
        dest.mkdir(exist_ok=True)
        if name=='jdk':
            with tarfile.open(archive) as stream:
                stream.extractall(dest, filter='data')
        else:
            with zipfile.ZipFile(archive) as stream:
                stream.extractall(dest)
            for f in dest.rglob('*'):
                if f.is_file() and f.suffix in ('','.sh'):
                    f.chmod(f.stat().st_mode | 0o111)
        marker.write_text(package['sha256']+'\n')
    print('Ready. Android SDK terms: https://developer.android.com/studio#terms')

if __name__=='__main__':
    main()
