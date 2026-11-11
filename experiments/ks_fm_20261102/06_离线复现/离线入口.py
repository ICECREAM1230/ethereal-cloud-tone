#!/usr/bin/env python3
"""Only offline verification, analysis and figure export; never capture/program."""
from pathlib import Path
import argparse
import csv
import hashlib
import importlib.util
import json
import sys

ARCHIVE = Path(__file__).resolve().parents[1]
CODE = ARCHIVE/'02_代码/experiment_project'
DATA = ARCHIVE/'01_原始数据'
PC = ARCHIVE/'02_代码/pc'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('operation', choices=('verify', 'analyze-basic', 'analyze-poly', 'figures'))
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    if args.operation == 'verify':
        with (ARCHIVE/'05_版本与校验/文件清单_SHA256.csv').open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        for row in rows:
            path = ARCHIVE/row['relative_path']
            assert path.resolve().is_relative_to(ARCHIVE)
            assert path.is_file() and path.stat().st_size == int(row['bytes'])
            assert digest(path) == row['sha256'], 'Checksum mismatch: '+row['relative_path']
        print(json.dumps({'status': 'PASS', 'verified_files': len(rows), 'hardware_access': False}))
        return 0
    if args.out is None:
        ap.error('--out is required; choose a fresh output directory')
    target = args.out.resolve()
    assert not target.exists(), 'Preserve existing outputs; choose a new directory'
    assert not target.is_relative_to(ARCHIVE/'01_原始数据'), 'Raw data is read-only'
    scripts = {'analyze-basic': 'analyze_ks_fm_pcm_20261102.py',
               'analyze-poly': 'analyze_ks_fm_poly_report_20261102.py',
               'figures': 'export_ks_fm_report_figures_20261102.py'}
    path = CODE/'sim/config'/scripts[args.operation]
    spec = importlib.util.spec_from_file_location('archived_ks_fm_offline', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    sys.path.insert(0, str(PC))
    spec.loader.exec_module(module)
    module.PC = PC  # Explicit relocation shim; original sources remain byte-identical.
    if args.operation == 'figures':
        module.ROOT = DATA
        sys.argv = [str(path), '--output', str(target)]
    else:
        run = 'board_20261102Tksfm01' if args.operation == 'analyze-basic' else 'board_poly_report_20261102'
        source = DATA/'sim/results/ks_fm_pcm_check_20261102'/run
        sys.argv = [str(path), '--capture-root', str(source), '--out', str(target)]
    return module.main() or 0


if __name__ == '__main__':
    raise SystemExit(main())
