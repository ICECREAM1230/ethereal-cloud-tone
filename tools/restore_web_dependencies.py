#!/usr/bin/env python3
"""Restore the saved, pinned web test/build dependencies without network access."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import tarfile

root=Path(__file__).resolve().parents[1]
metadata=root/'third_party/web-dependencies.json'
if not metadata.is_file():
    raise SystemExit('This source repository has no offline Node archive; run npm ci in apps/web using package-lock.json.')
meta=json.loads(metadata.read_text())
archive=root/meta['archive']
if hashlib.sha256(archive.read_bytes()).hexdigest()!=meta['archive_sha256']:
    raise SystemExit('Saved dependency archive checksum mismatch')
dest=root/'apps/web/node_modules'
if dest.exists() or dest.is_symlink():
    raise SystemExit('node_modules already exists; no files overwritten')
with tarfile.open(archive,'r:gz') as t:
    for m in t.getmembers():
        p=PurePosixPath(m.name)
        if p.is_absolute() or '..' in p.parts or p.parts[0]!='node_modules':
            raise SystemExit('Unsafe archive member: '+m.name)
        if not (m.isdir() or m.isfile() or m.issym()):
            raise SystemExit('Unexpected archive member: '+m.name)
    t.extractall(root/'apps/web',filter='data')
for rel,state in meta['files'].items():
    p=dest/rel
    if 'symlink' in state:
        if not p.is_symlink() or str(p.readlink())!=state['symlink'] or not p.resolve().is_relative_to(dest.resolve()):
            raise SystemExit('Dependency link mismatch: '+rel)
    elif not p.is_file() or p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=state['sha256']:
        raise SystemExit('Dependency file mismatch: '+rel)
print('PASS restored',len(meta['files']),'pinned dependency entries; no download or install scripts')
