#!/usr/bin/env python3
"""Copy only KS/FM data, tools and related source snapshots; no device access."""
from pathlib import Path
import argparse
import csv
import datetime
import hashlib
import json
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[2]
PC = Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
DATASETS = ('board_20261102Tksfm01', 'board_poly_report_20261102')
ENTRY_SCRIPTS = ('capture_ks_fm_pcm_20261102.py', 'analyze_ks_fm_pcm_20261102.py',
                 'analyze_ks_fm_poly_report_20261102.py', 'export_ks_fm_report_figures_20261102.py',
                 'package_ks_fm_report_20261102.py')
COMMON_SCRIPTS = ('analyze_upstream_pcm_recheck.py', 'capture_sub_supersaw_reset_20261101.py',
                  'run_upstream_pcm_recheck_board.py')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False);out.chmod(0o700)
    sources = {}
    code = out/'02_代码/experiment_project'
    raw = out/'01_原始数据/sim/results/ks_fm_pcm_check_20261102'
    analysis = out/'03_分析结果'
    figures = out/'04_报告图稿'
    versions = out/'05_版本与校验'
    versions.mkdir(parents=True)

    def copy(src, dest, role):
        src = Path(src).resolve(strict=True)
        assert src.is_file() and not src.is_symlink()
        digest = sha(src)
        dest = Path(dest)
        if dest.exists():
            assert sha(dest) == digest
            sources[str(dest.relative_to(out))]['roles'].append(role)
            return
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        assert sha(dest) == digest == sha(src), 'Input changed while copying: '+str(src)
        sources[str(dest.relative_to(out))] = {'source': str(src), 'SHA256': digest,
                                               'bytes': dest.stat().st_size, 'roles': [role]}

    def tree(src, dest, role, exclude=()):
        for p in sorted(Path(src).rglob('*')):
            if not p.is_file() or '__pycache__' in p.parts or p.suffix == '.pyc':
                continue
            rel = p.relative_to(src)
            if p.name in exclude:
                continue
            copy(p, Path(dest)/rel, role)

    rows = []
    counts = {'cases': 0, 'raw_bytes': 0, 'valid_frames': 0, 'stereo_sample_pairs': 0}
    data_root = PROJECT/'sim/results/ks_fm_pcm_check_20261102'
    for dataset in DATASETS:
        src = data_root/dataset
        session = json.loads((src/'session.json').read_text())
        assert session['status'] == 'CAPTURED_PENDING_ANALYSIS'
        assert session['shared_lock_released'] and session['production_tools_unchanged']
        for name in sorted(session['scenarios']):
            assert name.startswith(('ks_', 'fm_'))
            folder = src/name
            required = ('events.json', 'midi.json', 'run.json', 'selected-state.json',
                        'capture/analysis.json', 'capture/audio.wav', 'capture/payload.pcm',
                        'capture/raw.bin', 'capture/stats.json')
            assert all((folder/p).is_file() for p in required)
            tree(folder, raw/dataset/name, 'KS/FM原始数据及原样元数据')
            run = json.loads((folder/'run.json').read_text())
            stats = run['final_capture_stats']
            assert stats['status'] == 'PASS' and run['source_released']
            midi = json.loads((folder/'midi.json').read_text())
            assert all(e['stage'] == 'fpga_byte_ack' for e in midi['events'])
            rounds = len(midi['events'])//2
            row = {'合成器': 'KS' if name.startswith('ks_') else 'FM', '场景': name,
                   '数据集': dataset, '轮数': rounds, '力度': 100,
                   'MIDI音符': ' '.join(map(str, run['notes'])),
                   '左右样本对': stats['sample_frames'], '有效帧': stats['frames_valid'],
                   'RAW字节': stats['raw_bytes'],
                   '归档相对路径': str((raw/dataset/name).relative_to(out))}
            rows.append(row)
            counts['cases'] += 1;counts['raw_bytes'] += stats['raw_bytes']
            counts['valid_frames'] += stats['frames_valid'];counts['stereo_sample_pairs'] += stats['sample_frames']
        for name in ('session.json', 'tool-hashes-before.json', 'delivery.json', 'independent-pitch-check.json'):
            if (src/name).is_file():
                copy(src/name, raw/dataset/name, '数据集原始控制/校验记录')
    assert counts == {'cases': 10, 'raw_bytes': 70377472, 'valid_frames': 120274,
                      'stereo_sample_pairs': 7697536}
    with (out/'数据集索引.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    for name in ENTRY_SCRIPTS+COMMON_SCRIPTS:
        copy(PROJECT/'sim/config'/name, code/'sim/config'/name,
             'KS/FM入口脚本' if name in ENTRY_SCRIPTS else '原样共享工具依赖')
    copy(PROJECT/'tools/ch569_pcm_capture/pcm_capture/protocol.py',
         code/'tools/ch569_pcm_capture/pcm_capture/protocol.py', '共享只读封包解析器')
    for p in sorted((PC/'pcm_studio').glob('*.py')):
        copy(p, out/'02_代码/pc/pcm_studio'/p.name, 'PC接收/分析包源码')
    if (PC/'requirements.txt').exists():
        copy(PC/'requirements.txt', out/'02_代码/pc/requirements-original.txt', '原PC依赖清单')
    report = PROJECT/'sim/results/ks_fm_report_20261102'
    old_capture = report/'capture-script-before-poly.py'
    expected = json.loads((data_root/DATASETS[0]/'delivery.json').read_text())['script_SHA256']['capture_ks_fm_pcm_20261102.py']
    assert sha(old_capture) == expected
    copy(old_capture, out/'02_代码/历史版本/capture_ks_fm_pcm_基础8段实际版本.py', '基础8段采集时的精确脚本版本')
    for dataset in DATASETS:
        if (data_root/dataset/'analysis').is_dir():
            tree(data_root/dataset/'analysis', analysis/'基础8段', '已发布KS/FM分析结果',
                 exclude=('board-state-after-ks-fm.json',))
    tree(report/'poly_analysis', analysis/'满复音重复触发', '已发布满复音分析结果')
    tree(report/'figures/final', figures/'单张PNG与矢量PDF', '最终中文报告图稿')
    for name in ('KS与FM_中文报告图集_12页.pdf', '图注与方法.md', '测试参数.csv', 'delivery-manifest.json'):
        copy(report/name, figures/name, '最终报告图稿说明/合并图集')
    for name in ('qa.json', 'report.md'):
        copy(report/'qa'/name, figures/'质量检查'/name, '最终图稿QA记录')
    copy(report/'合并图集校验/qa.json', figures/'质量检查/合并图集QA.json', '合并图集QA记录')
    copy(report/'hardware-released.json', versions/'hardware-released.json', '采集结束释放硬件记录')
    # Implementation snapshots are references, not a purported standalone EDA project.
    for rel in ('src/synthesizers/karplus_strong', 'src/synthesizers/fm', 'src/vendor/jt51'):
        tree(PROJECT/rel, code/rel, 'KS/FM实现参考源码')
    for rel in ('control_migration/rtl/ae350_voice_backend_ks.sv',
                'control_migration/rtl/ae350_voice_backend_fm.sv',
                'control_migration/rtl/ae350_voice_backend_fm_acc.sv',
                'control_migration/rtl/ae350_voice_backend.sv',
                'firmware/control_compile.c', 'firmware/control_compile.h',
                'firmware/control_service.c', 'firmware/control_service.h', 'firmware/control_main.c',
                'firmware/control_data/control_boot_tables.h', 'firmware/control_data/sub_boot_supersaw.h',
                'rtl/control_lab_top.sv', 'src/board/synth_system.sv',
                'src/pcm_post/pipeline/pcm_request_audio.sv', 'LICENSE'):
        copy(PROJECT/rel, code/rel, '相关实现/共享编译依赖参考快照')
    for rel in ('control_migration/firmware', 'control_migration/include'):
        tree(PROJECT/rel, code/rel, 'AE350共享解析/声部分配/协议源码参考')
    manifest_path = PROJECT/'control_migration/esp32/results/upstream_esp32_20261101/programming-manifest.json'
    esp = json.loads(manifest_path.read_text())
    for src, digest in esp['inputs'].items():
        p = Path(src);assert p.is_relative_to(PROJECT) and sha(p) == digest
        copy(p, code/p.relative_to(PROJECT), '采集配套ESP32控制固件源码（原333项输入）')
    for rel in ('firmware/build/control.bin', 'firmware/build/control-build.json'):
        copy(PROJECT/rel, versions/Path(rel).name, '共用AE350固件/编译版本依据')
    git = subprocess.run(['git', 'status', '--short'], cwd=PROJECT, capture_output=True, text=True)
    assert git.returncode == 0
    (versions/'整理前Git状态.txt').write_text(git.stdout)
    version = {'archive_created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'original_project': str(PROJECT), 'scope': 'KS/FM only; no subtractive capture/results/simulation',
               'board_image_SHA256': json.loads((data_root/DATASETS[0]/'session.json').read_text())['board_image_SHA256'],
               'board_user_code': json.loads((data_root/DATASETS[0]/'session.json').read_text())['board_user_code'],
               'ESP32_app_SHA256_recorded': esp['app']['SHA256'], 'ESP32_source_inputs': len(esp['inputs']),
               'raw_totals': counts, 'source_inputs_unchanged_during_copy': True,
               'hardware_access': False, 'original_files_moved_or_deleted': False,
               'implementation_snapshot_is_complete_independent_FPGA_project': False}
    (versions/'版本与范围.json').write_text(json.dumps(version, ensure_ascii=False, indent=2)+'\n')
    for rel, item in sources.items():
        assert sha(Path(item['source'])) == item['SHA256'] == sha(out/rel)
    (versions/'原文件到归档映射.json').write_text(json.dumps(sources, ensure_ascii=False, indent=2)+'\n')
    with (versions/'代码来源.csv').open('w', newline='') as stream:
        writer = csv.writer(stream);writer.writerow(['归档相对路径', '角色', '原路径', '字节数', 'SHA256'])
        for rel, item in sorted(sources.items()):
            if rel.startswith('02_代码/'):
                writer.writerow([rel, '；'.join(item['roles']), item['source'], item['bytes'], item['SHA256']])
    print(json.dumps({'status': 'COPIED_NOT_YET_FINALIZED', 'archive_root': str(out),
                      'copied_files': len(sources), 'copied_bytes': sum(v['bytes'] for v in sources.values()),
                      'raw_totals': counts, 'hardware_access': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
