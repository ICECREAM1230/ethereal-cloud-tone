#!/usr/bin/env python3
"""Archive the actual owned working tree with per-file hashes and isolated recovery."""
import argparse
import datetime
import gzip
import hashlib
import json
import pathlib
import subprocess
import tarfile
import zoneinfo

ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('name');p.add_argument('--validation',required=True);p.add_argument('--offline',action='store_true')
    args=p.parse_args()
    if '/' in args.name or '..' in args.name:raise SystemExit('invalid name')
    dest=ROOT/'snapshots'/args.name
    if dest.exists():raise SystemExit('snapshot already exists; choose a new milestone name')
    dest.mkdir(parents=True)
    files=[]
    archive_root=ROOT.name
    for f in sorted(ROOT.rglob('*')):
        if not f.is_file():continue
        parts=f.relative_to(ROOT).parts
        if parts[0] in ('snapshots','build') or '__pycache__' in parts:continue
        if parts[0]=='.toolchain' and (not args.offline or parts[1] not in ('jdk','platform','build-tools')):continue
        files.append(f)
    manifest=[{'path':archive_root+'/'+str(f.relative_to(ROOT)),'bytes':f.stat().st_size,
        'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in files]
    archive=dest/'mobile-app.tar.gz'
    with archive.open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',mtime=0) as stream,tarfile.open(fileobj=stream,mode='w') as tar:
        for f,item in zip(files,manifest):
            info=tar.gettarinfo(str(f),arcname=item['path']);info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
            with f.open('rb') as content:tar.addfile(info,content)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    (dest/'SHA256SUMS').write_text(digest+'  mobile-app.tar.gz\n')
    (dest/'SHA256FILES').write_text(''.join(x['sha256']+'  '+x['path']+'\n' for x in manifest))
    now=datetime.datetime.now(zoneinfo.ZoneInfo('Asia/Tokyo')).isoformat()
    def git_value(*args):
        r=subprocess.run(['git',*args],cwd=ROOT,text=True,capture_output=True)
        return r.stdout.strip() if r.returncode==0 else None
    meta={'created_at':now,'milestone':args.name,'scope':str(ROOT)+'/** actual source, config, untracked inputs and delivery artifacts',
        'archive_root':archive_root,'head':git_value('rev-parse','HEAD'),'branch':git_value('branch','--show-current'),
        'git_status':git_value('status','--short','--branch'),
        'validation':args.validation,'offline_build_toolchain_included':args.offline,'archive_sha256':digest,'files':manifest}
    (dest/'manifest.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    (dest/'恢复说明.md').write_text(f'''# {args.name} 功能快照

创建时间：{now}。共 {len(files)} 个文件；归档 {archive.stat().st_size:,} 字节。

范围：实际 `{ROOT}/` 工作内容，含必要未跟踪源码、配置、工具版本锁定、开发签名与 APK（如已构建）。本角色目录外的工程未纳入或修改。HEAD `{meta['head']}`；实际 dirty 文件以逐项 SHA 清单为准，不以分支或 diff 代替备份。

验证：{args.validation}

工具链：{'包含离线构建所需 JDK、Android SDK Platform 与 Build Tools，保留其原始许可文件。' if args.offline else '工具缓存未归档；恢复后运行 tools/setup.py 下载锁定且校验 SHA256 的工具链。'}

恢复步骤（始终恢复到新的空目录，再审核需要复制的文件）：

```bash
cd 快照所在目录
sha256sum -c SHA256SUMS
mkdir -p /tmp/fpga-app-restore-{args.name}
tar -xzf mobile-app.tar.gz -C /tmp/fpga-app-restore-{args.name}
cd /tmp/fpga-app-restore-{args.name}
sha256sum -c 原快照目录的绝对路径/SHA256FILES
cd {archive_root}
python3 tools/setup.py
python3 tools/test.py
python3 tools/test_http.py
python3 tools/build.py
```

不要直接把归档解压到共享工作树覆盖现有内容。逐文件 SHA256 清单与 manifest.json 一起保留；归档不包含自身及其他快照，避免递归。
''')
    index=ROOT/'snapshots/索引.md'
    if not index.exists():index.write_text('# 手机 App 功能快照索引\n\n快照覆盖本角色的实际工作文件，不依赖 Git 提交。各阶段的恢复说明、文件清单和 SHA256 放在对应子目录。\n\n')
    with index.open('a') as out:out.write(f'- [{args.name}]({args.name}/恢复说明.md)：{args.validation}；归档 SHA256 `{digest}`。\n')
    print(json.dumps({'snapshot':str(dest),'files':len(files),'bytes':archive.stat().st_size,'sha256':digest},ensure_ascii=False))
if __name__=='__main__':main()
