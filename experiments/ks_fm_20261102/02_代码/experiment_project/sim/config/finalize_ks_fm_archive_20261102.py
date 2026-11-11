#!/usr/bin/env python3
"""Validate the offline KS/FM archive, freeze a manifest and create one ZIP."""
from pathlib import Path
import argparse
import ast
import csv
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

PROJECT = Path(__file__).resolve().parents[2]
VALIDATION = PROJECT / 'sim/results/ks_fm_archive_validation_20261102'


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def compare_cases(original, repeated):
    """Every result must match; only the recorded input location may change."""
    count = 0

    def walk(a, b, key=''):
        nonlocal count
        if isinstance(a, dict):
            assert isinstance(b, dict) and a.keys() == b.keys(), key
            for name in a:
                walk(a[name], b[name], key + '/' + name)
        elif isinstance(a, list):
            assert isinstance(b, list) and len(a) == len(b), key
            for i, (left, right) in enumerate(zip(a, b)):
                walk(left, right, key + '/' + str(i))
        elif not key.endswith('/capture/path'):
            assert a == b, key
            if isinstance(a, (int, float, bool)):
                count += 1

    walk(original, repeated)
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive', required=True, type=Path)
    args = ap.parse_args()
    archive = args.archive.resolve(strict=True)
    versions = archive / '05_版本与校验'
    zip_path = archive.parent / (archive.name + '.zip')
    assert not zip_path.exists(), 'Preserve an existing delivery ZIP'
    manifest = versions / '文件清单_SHA256.csv'
    assert not manifest.exists(), 'Archive already frozen'
    scope = json.loads((versions / '版本与范围.json').read_text())
    assert scope['raw_totals'] == {
        'cases': 10, 'raw_bytes': 70377472, 'valid_frames': 120274,
        'stereo_sample_pairs': 7697536}

    comparisons = []
    for mode, original in (
        ('basic', PROJECT / 'sim/results/ks_fm_pcm_check_20261102/board_20261102Tksfm01/analysis/summary.json'),
        ('poly', PROJECT / 'sim/results/ks_fm_report_20261102/poly_analysis/summary.json'),
    ):
        old = json.loads(original.read_text())
        new_path = VALIDATION / mode / 'summary.json'
        new = json.loads(new_path.read_text())
        assert old['totals'] == new['totals'] and new['input_hashes_unchanged']
        fields = compare_cases(old['cases'], new['cases'])
        comparisons.append({'suite': mode, 'totals': new['totals'],
                            'all_case_results_equal_except_capture_path': True,
                            'numeric_and_boolean_fields_compared': fields,
                            'summary_SHA256': sha(new_path),
                            'archived_input_hashes_unchanged': True})
    figure_dir = VALIDATION / 'figures'
    figures = json.loads((figure_dir / 'figure-manifest.json').read_text())
    assert figures['figure_count'] == 12 and not figures['hardware_access']
    pngs = sorted(figure_dir.glob('*.png'))
    assert len(pngs) == len(list(figure_dir.glob('*.pdf'))) == 12
    published = archive / '04_报告图稿/单张PNG与矢量PDF'
    assert all(sha(png) == sha(published / png.name) for png in pngs)
    offline = {'status': 'PASS', 'analysis': comparisons,
               'figure_export': {'PNG_count': 12, 'PDF_count': 12,
                                 'all_12_PNGs_byte_identical_to_published': True},
               'hardware_access': False, 'network_access': False,
               'normalization': False, 'gain_applied': 1, 'resampling': False,
               'validation_output_directory': str(VALIDATION),
               'matching_RTL_reference_available': False,
               'bit_exact_against_RTL_claimed': False}
    dump(versions / '离线复现验证.json', offline)

    mapping_path = versions / '原文件到归档映射.json'
    mapping = json.loads(mapping_path.read_text())
    extra_sources = [
        (PROJECT / 'sim/config/archive_ks_fm_data_code_20261102.py',
         archive / '02_代码/experiment_project/sim/config/archive_ks_fm_data_code_20261102.py',
         '本次离线归档的基础复制工具；不包含后续说明及封包步骤'),
        (Path(__file__).resolve(),
         archive / '02_代码/experiment_project/sim/config' / Path(__file__).name,
         '本次离线归档核验与封包工具'),
        (PROJECT / 'control_migration/esp32/results/upstream_esp32_20261101/programming-manifest.json',
         versions / 'ESP32编译来源.json', '原333项ESP32源码编译依据；未执行编程'),
    ]
    for source, target, role in extra_sources:
        if target.exists():
            assert sha(target) == sha(source)
        else:
            shutil.copy2(source, target)
        mapping[str(target.relative_to(archive))] = {
            'source': str(source), 'SHA256': sha(source),
            'bytes': source.stat().st_size, 'roles': [role]}
    for relative, entry in mapping.items():
        assert sha(archive / relative) == entry['SHA256'], relative
        assert sha(Path(entry['source'])) == entry['SHA256'], entry['source']
    dump(mapping_path, mapping)
    with (versions / '代码来源.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['归档相对路径', '角色', '原路径', '字节数', 'SHA256'])
        for relative, entry in sorted(mapping.items()):
            if relative.startswith('02_代码/'):
                writer.writerow([relative, '；'.join(entry['roles']), entry['source'],
                                 entry['bytes'], entry['SHA256']])

    (archive / '04_报告图稿/阅读说明.md').write_text(
        '# 本次归档中的报告文件\n\n'
        '本目录保留原报告图稿、图注与方法和原交付清单，内容与来源逐字一致。'
        '原图注中的“6段WAV副本”和原delivery-manifest描述的是此前报告包，'
        '不是本次归档的目录清单。\n\n'
        '本次共保存10段原始WAV，统一位于01_原始数据，并与各自RAW、PCM和演奏日志放在一起。'
        '请以根目录的数据集索引.csv和05_版本与校验/文件清单_SHA256.csv定位文件；'
        '没有再复制旧报告包中的6段WAV。\n')
    readme = archive / 'README.md'
    readme.write_text(readme.read_text() + '\n'
        '原报告的图注及delivery-manifest保留当时交付描述，其中6段WAV副本属于旧报告包。'
        '本归档统一保存10段原录音，位置以根数据集索引为准，详见04_报告图稿/阅读说明.md。'
        '归档副本已实际完成8段基础、2段满复音的重新分析和12张图稿重新导出，'
        '结果见05_版本与校验/离线复现验证.json。\n')
    for path in (archive / '06_离线复现/离线入口.py',
                 PROJECT / 'sim/config/archive_ks_fm_data_code_20261102.py',
                 Path(__file__).resolve()):
        ast.parse(path.read_text())
    dump(versions / '归档状态.json', {
        'status': 'OFFLINE_VALIDATED_AND_FROZEN', 'scope': 'KS/FM',
        'raw_totals': scope['raw_totals'], 'mapped_original_files': len(mapping),
        'original_source_hashes_unchanged_at_finalization': True,
        'offline_analysis_and_figure_export': 'PASS', 'hardware_access': False,
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'manifest_excludes_itself': True})
    files = sorted(p for p in archive.rglob('*') if p.is_file())
    assert not any(p.is_symlink() or p.suffix in ('.pyc', '.zip') for p in files)
    rows = [{'relative_path': str(p.relative_to(archive)), 'bytes': p.stat().st_size,
             'sha256': sha(p)} for p in files]
    with manifest.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=('relative_path', 'bytes', 'sha256'))
        writer.writeheader()
        writer.writerows(rows)
    verified = subprocess.run(
        [sys.executable, '-B', str(archive / '06_离线复现/离线入口.py'), 'verify'],
        text=True, capture_output=True, check=True)
    verification = json.loads(verified.stdout)
    assert verification['status'] == 'PASS' and verification['verified_files'] == len(rows)
    all_files = sorted(p for p in archive.rglob('*') if p.is_file())
    with zipfile.ZipFile(zip_path, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in all_files:
            z.write(path, str(Path(archive.name) / path.relative_to(archive)))
    zip_path.chmod(0o600)
    with zipfile.ZipFile(zip_path) as z:
        assert z.testzip() is None
        assert len(z.infolist()) == len(all_files)
        for path in all_files:
            h = hashlib.sha256()
            with z.open(str(Path(archive.name) / path.relative_to(archive))) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    h.update(block)
            assert h.hexdigest() == sha(path), str(path)
    receipt = {'status': 'PASS', 'archive_root': str(archive), 'ZIP': str(zip_path),
               'ZIP_SHA256': sha(zip_path), 'ZIP_bytes': zip_path.stat().st_size,
               'payload_manifest_verified_files': len(rows),
               'ZIP_members_verified': len(all_files), 'ZIP_CRC': 'PASS',
               'ZIP_member_SHA256': 'PASS', 'mapped_original_files': len(mapping),
               'raw_totals': scope['raw_totals'], 'offline_validation': offline,
               'original_files_moved_or_deleted': False, 'hardware_access': False}
    dump(VALIDATION / 'archive_delivery.json', receipt)
    zip_path.with_suffix('.zip.sha256').write_text(sha(zip_path) + '  ' + zip_path.name + '\n')
    print(json.dumps({key: value for key, value in receipt.items()
                      if key != 'offline_validation'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
