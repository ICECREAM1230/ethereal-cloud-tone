#!/usr/bin/env python3
"""Package frozen Chinese figures and unchanged recordings; offline only."""
from pathlib import Path
import argparse
import csv
import hashlib
import io
import json
import zipfile
from PIL import Image
from pypdf import PdfReader, PdfWriter


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', required=True, type=Path)
    args = ap.parse_args()
    root = args.root.resolve(strict=True)
    figures = root/'figures/final'
    pngs, pdfs = sorted(figures.glob('*.png')), sorted(figures.glob('*.pdf'))
    assert len(pngs) == len(pdfs) == 12
    assert [p.stem for p in pngs] == [p.stem for p in pdfs]
    manifests = list(figures.glob('*manifest*.json'))
    assert manifests, 'Renderer must freeze its manifest before packaging'
    audio = json.loads((root/'audio-manifest.json').read_text())
    assert len(audio) == 6
    for item in audio:
        assert item['unchanged_copy'] and sha(item['file']) == item['SHA256']
    inputs = pngs+pdfs+[Path(item['file']) for item in audio]+manifests
    hashes = {str(p): sha(p) for p in inputs}
    images = []
    for p in pngs:
        with Image.open(p) as image:
            assert image.size == (4800, 3000)
            dpi = image.info.get('dpi')
            assert dpi and all(abs(float(x)-600) < 1 for x in dpi)
            images.append({'path': str(p), 'pixels': list(image.size), 'dpi': dpi, 'SHA256': sha(p)})
    combined = root/'KS与FM_中文报告图集_12页.pdf'
    archive = root/'KS与FM_12张中文图稿及录音.zip'
    assert not combined.exists() and not archive.exists(), 'Preserve an existing delivery'
    writer = PdfWriter()
    for p in pdfs:
        reader = PdfReader(str(p));assert len(reader.pages) == 1
        writer.append(str(p), outline_item=p.stem.replace('_', ' '))
    writer.add_metadata({'/Title': 'KS与FM合成器中文实测图集',
                         '/Subject': '单音、CEG和弦及16音/8音三轮重复触发，原始幅度',
                         '/Keywords': 'KS,FM,时域,频域,实测PCM,2026-11-02'})
    with combined.open('wb') as stream:
        writer.write(stream)
    assert len(PdfReader(str(combined)).pages) == 12
    members = []
    for p in pngs:
        members.append((p, '中文图稿/'+p.name))
    for p in pdfs:
        members.append((p, '矢量PDF/'+p.name))
    members.append((combined, combined.name))
    for item in audio:
        p = Path(item['file']);members.append((p, '原始录音/'+p.name))
    for name in ['图注与方法.md', '测试参数.csv', 'audio-manifest.json']:
        members.append((root/name, name))
    for p in manifests:
        members.append((p, p.name))
    index = ['# 图稿索引', '', '12张图均为中文，保留原始幅度；每个工况各含一张时域图和一张频域图。', '']
    for p in pngs:
        index.append(f'- [{p.stem}](中文图稿/{p.name})')
    index += ['', '对应单页矢量图位于“矢量PDF”，合并PDF共12页；6段原始WAV位于“原始录音”。',
              '具体音符、窗长、FFT与数据边界见图注与方法说明及图稿清单。']
    index_path = root/'图稿索引.md';index_path.write_text('\n'.join(index)+'\n')
    members.append((index_path, index_path.name))
    listing = root/'文件清单_SHA256.csv'
    with listing.open('w', newline='') as stream:
        csvwriter = csv.writer(stream);csvwriter.writerow(['压缩包内路径', '字节数', 'SHA256'])
        for p, name in members:
            csvwriter.writerow([name, p.stat().st_size, sha(p)])
    members.append((listing, listing.name))
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as pack:
        for p, name in members:
            pack.write(p, name)
    with zipfile.ZipFile(archive) as pack:
        assert pack.testzip() is None
        assert len(pack.namelist()) == len(members)
        for p, name in members:
            assert hashlib.sha256(pack.read(name)).hexdigest() == sha(p)
    assert all(sha(Path(p)) == value for p, value in hashes.items())
    result = {'status': 'PACKAGED', 'PNG_count': 12, 'single_page_PDF_count': 12,
              'combined_PDF': {'path': str(combined), 'pages': 12, 'SHA256': sha(combined)},
              'ZIP': {'path': str(archive), 'bytes': archive.stat().st_size,
                      'SHA256': sha(archive), 'members': len(members), 'CRC_and_member_SHA_verified': True},
              'audio_count': 6, 'images': images, 'input_hashes_unchanged': True,
              'hardware_access': False, 'package_script_SHA256': sha(__file__)}
    (root/'delivery-manifest.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({k: result[k] for k in ['status', 'PNG_count', 'audio_count', 'ZIP', 'combined_PDF']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
