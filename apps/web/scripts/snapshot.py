#!/usr/bin/env python3
"""角色目录恢复归档；从不 stage/commit，也不读取另一角色文件。"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import tarfile
from datetime import datetime
from zoneinfo import ZoneInfo

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--label', default='checkpoint')
parser.add_argument('--validation', action='append', default=[])
parser.add_argument('--include-dependencies', action='store_true')
parser.add_argument('--verify', type=pathlib.Path)
parser.add_argument('--root', type=pathlib.Path)
args = parser.parse_args()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

if args.verify:
    manifest = json.loads(args.verify.read_text())
    target = args.root or ROOT
    failed = []
    for record in manifest['files']:
        path = target / record['path']
        if record['kind'] == 'symlink':
            if not path.is_symlink() or str(path.readlink()) != record['target']:
                failed.append(record['path'])
        elif not path.is_file() or sha(path) != record['sha256']:
            failed.append(record['path'])
    print(json.dumps({'checked': len(manifest['files']), 'failed': failed}, ensure_ascii=False))
    raise SystemExit(bool(failed))

if not re.fullmatch(r'[a-z0-9-]{1,50}', args.label):
    raise SystemExit('label must be lowercase letters/digits/hyphens')
stamp = datetime.now(ZoneInfo('Asia/Tokyo'))
identity = stamp.strftime('%Y%m%d-%H%M%S') + '-' + args.label
folder = ROOT / 'snapshots' / identity
folder.mkdir(parents=True, exist_ok=False)
files = []
for path in sorted(ROOT.rglob('*')):
    relative = path.relative_to(ROOT)
    if relative.parts[0] in ('snapshots', '.cache'):
        continue
    if relative.parts[0] == 'node_modules' and not args.include_dependencies:
        continue
    if path.is_symlink():
        files.append({'path': str(relative), 'kind': 'symlink', 'target': str(path.readlink())})
    elif path.is_file():
        files.append({'path': str(relative), 'kind': 'file', 'bytes': path.stat().st_size, 'sha256': sha(path)})
archive = folder / 'web-ui.tar.gz'
with tarfile.open(archive, 'w:gz') as tar:
    for record in files:
        tar.add(ROOT / record['path'], arcname='web-ui/' + record['path'], recursive=False)
manifest = {
    'id': identity, 'recorded_at': stamp.isoformat(), 'scope': ROOT.name + '/**',
    'git_baseline': None,
    'archive': archive.name, 'archive_sha256': sha(archive),
    'includes_dependencies': args.include_dependencies,
    'excluded': ['历史 snapshots/ 自身（归档旁保留索引）', '.cache/'] + ([] if args.include_dependencies else ['原始 node_modules/；优先使用归档内已打包依赖及其清单，否则通过 package-lock.json 恢复']),
    'system_requirements': 'Node.js >=22，Python >=3.12；浏览器验收需要系统 Chrome 或 WEB_UI_BROWSER 指定的 Chromium',
    'validation': args.validation, 'files': files
}
def git_value(*arguments, working_directory=ROOT):
    result = subprocess.run(['git', '-C', str(working_directory), *arguments], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None

repository = git_value('rev-parse', '--show-toplevel')
if repository:
    module_path = ROOT.relative_to(pathlib.Path(repository)).as_posix()
    manifest['scope'] = module_path + '/**'
    manifest['git_baseline'] = {
        'repository': repository,
        'branch': git_value('branch', '--show-current'),
        'head': git_value('rev-parse', 'HEAD'),
        'module_status': git_value('-c', 'core.quotepath=false', 'status', '--porcelain=v1', '--untracked-files=all', '--', module_path, working_directory=repository),
        'snapshot_scope': manifest['scope'],
        'note': '归档包含本模块的实际文件；HEAD 和状态文字不代表完整源输入。其他模块、系统工具和本目录排除项不在归档中。',
    }
(folder / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
(folder / 'SHA256SUMS').write_text(f'{manifest["archive_sha256"]}  web-ui.tar.gz\n{sha(folder / "manifest.json")}  manifest.json\n')
text = f'''# 功能快照：{args.label}

时间：{stamp.isoformat()}。范围：`{manifest['scope']}`，包含 {len(files)} 个文件或符号链接。

验证范围：{'；'.join(args.validation) or '未声明验证，不能视为验收通过'}。

`manifest.json` 列出全部文件、逐文件 SHA256、原仓库基线、排除项和验证；`SHA256SUMS` 校验归档及清单。归档包含源码、依赖清单、配置、当前说明、测试、生成 HTML 和已有验收产物。{'包含本目录 node_modules/，可离线恢复当前浏览器测试依赖。' if args.include_dependencies else '不直接包含原始node_modules/；已有打包依赖时优先按其SHA清单离线恢复，否则通过npm ci恢复。核心构建、mock与协议测试无需下载依赖。'}

## 恢复到独立目录

不要覆盖原脏工作树。在此快照目录校验后，解包到新的恢复目录：

```bash
sha256sum -c SHA256SUMS
mkdir -p /tmp/fpga-web-recovery
tar -xzf web-ui.tar.gz -C /tmp/fpga-web-recovery
python3 /tmp/fpga-web-recovery/web-ui/scripts/snapshot.py --verify "$PWD/manifest.json" --root /tmp/fpga-web-recovery/web-ui
cd /tmp/fpga-web-recovery/web-ui
npm run build
npm test
```

本快照不包含原固件、App、系统 Node/Python/Chrome 或历史快照归档。未进行设备操作或 Git 提交。
'''
(folder / '快照说明.md').write_text(text)
entries_path = ROOT / 'snapshots' / 'entries.json'
entries = json.loads(entries_path.read_text()) if entries_path.exists() else []
entries.append({'id': identity, 'label': args.label, 'file_count': len(files), 'validation': args.validation, 'sha256': manifest['archive_sha256']})
entries_path.write_text(json.dumps(entries, ensure_ascii=False, indent=2) + '\n')
index = '# 网页功能快照索引\n\n最新快照：`' + identity + '`。旧快照保留当时事实；后续修正以最新快照及交付报告为准。\n\n'
index += '| 快照 | 包含文件 | 验证范围 |\n|---|---:|---|\n'
for entry in reversed(entries):
    index += f'| [{entry["id"]}]({entry["id"]}/快照说明.md) | {entry["file_count"]} | {"；".join(entry["validation"])} |\n'
(ROOT / 'snapshots' / 'index.md').write_text(index)
print(json.dumps({'id': identity, 'files': len(files), 'archive_bytes': archive.stat().st_size, 'sha256': manifest['archive_sha256']}, ensure_ascii=False))
