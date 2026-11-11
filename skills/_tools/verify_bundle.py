#!/usr/bin/env python3
"""Read-only public-package verification; no skill, vendor or hardware execution."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from urllib.parse import unquote


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def verify(root):
    require(root.is_dir(), f'package root is missing: {root}')
    exempt = '_metadata/resources.json'
    manifest = read_json(root / exempt)
    require(manifest['format_version'] == 1, 'unsupported manifest version')
    require(manifest['unhashed_manifest'] == exempt, 'unexpected manifest exemption')
    rows = manifest['files']
    seen = set()
    resource_bytes = 0
    for row in rows:
        relative = row['path']
        parts = PurePosixPath(relative)
        require(relative not in seen and not parts.is_absolute() and '..' not in parts.parts,
                f'duplicate or unsafe manifest path: {relative}')
        require(relative != exempt and parts.as_posix() == relative, f'invalid indexed path: {relative}')
        seen.add(relative)
        path = root / relative
        require(path.is_file() and not path.is_symlink(), f'missing/nonregular resource: {relative}')
        require(path.resolve().is_relative_to(root), f'resource escapes package: {relative}')
        require(path.stat().st_size == row['bytes'], f'byte count mismatch: {relative}')
        require(digest(path) == row['sha256'], f'SHA-256 mismatch: {relative}')
        executable = bool(stat.S_IMODE(path.stat().st_mode) & 0o111)
        require(executable == row['executable'], f'Git executable-bit mismatch: {relative}')
        resource_bytes += row['bytes']
    entries = list(root.rglob('*'))
    require(not any(path.is_symlink() for path in entries), 'symlinks are not package resources')
    actual = {path.relative_to(root).as_posix() for path in entries if path.is_file()}
    require(actual == seen | {exempt}, f'resource set mismatch: missing={sorted(seen - actual)}, '
            f'unindexed={sorted(actual - seen - {exempt})}')
    skills = read_json(root / '_metadata/current-skills.json')
    state = read_json(root / '_metadata/enable-state-snapshot.json')
    names = {skill['name'] for skill in skills}
    require(len(skills) == len(names) == 33, 'expected exactly 33 unique current skills')
    require({p.name for p in root.iterdir() if p.is_dir()} == names | {'_metadata', '_tools', 'host_support'},
            'unexpected top-level directories')
    disabled = {'gf-pcb', 'gf-release'}
    require(set(state['explicit_disabled']) == disabled, 'source disabled snapshot differs')
    require(set(state['available_not_disabled']) == names - disabled, 'source available snapshot differs')
    require(state['installation_performed'] is False and state['global_configuration_modified'] is False,
            'snapshot must not claim installation or configuration changes')
    for skill in skills:
        require(skill['path'] == skill['name'], f'non-sibling skill path: {skill["name"]}')
        entry = root / skill['path'] / 'SKILL.md'
        text = entry.read_text(encoding='utf-8')
        match = re.match(r'\A---\s*\n(.*?)\n---\s*(?:\n|$)', text, re.S)
        require(match is not None, f'missing frontmatter: {entry.relative_to(root)}')
        header = match.group(1)
        require(re.search(r'^name:\s*[\'\"]?' + re.escape(skill['name']) + r'[\'\"]?\s*$', header, re.M),
                f'frontmatter name differs: {entry.relative_to(root)}')
        require(re.search(r'^description:\s*\S', header, re.M), f'missing description: {entry.relative_to(root)}')
        require((skill['source_status'] == 'explicitly_disabled') == (skill['name'] in disabled),
                f'source state differs: {skill["name"]}')
    checked_links = cross_links = 0
    for path in root.rglob('*.md'):
        # Markdown inside fenced example templates describes generated outputs.
        text = re.sub(r'(?ms)^(```|~~~)[^\n]*\n.*?^\1\s*$', '', path.read_text(encoding='utf-8'))
        for target in re.findall(r'\[[^\]\n]*\]\(([^\s)]+)\)', text):
            if '://' in target or target.startswith(('#', 'mailto:')):
                continue
            target_parts = unquote(target).split('#', 1)
            relative = target_parts[0]
            if not relative or any(symbol in relative for symbol in '<>{}$'):
                continue
            destination = (path.parent / relative).resolve()
            require(destination.is_relative_to(root), f'link escapes package: {path.relative_to(root)} -> {target}')
            require(destination.exists(), f'missing link: {path.relative_to(root)} -> {target}')
            if len(target_parts) == 2 and re.fullmatch(r'pdf-page-[0-9]+', target_parts[1]):
                number = target_parts[1].removeprefix('pdf-page-')
                require(re.search(r'^## PDF page ' + number + r'\s*$', destination.read_text(encoding='utf-8'), re.M),
                        f'missing manual locator anchor: {path.relative_to(root)} -> {target}')
            checked_links += 1
            source_skill = path.relative_to(root).parts[0]
            target_skill = destination.relative_to(root).parts[0]
            cross_links += int(source_skill in names and target_skill in names and source_skill != target_skill)
    require(cross_links == manifest['cross_skill_links'], 'cross-skill link count differs')
    manual_root = root / 'gowin-arora-v-rtl/references/manuals'
    manuals = read_json(manual_root / 'manifest.json')['manuals']
    require(len(manuals) == 8, 'expected eight external manual indices')
    for key, manual in manuals.items():
        require(manual['bundled_pdf'] is False and manual['bundled_markdown_body'] is False,
                f'incorrect manual distribution metadata: {key}')
        require(not (manual_root / manual['file']).exists(), f'vendor PDF unexpectedly bundled: {key}')
        locator = (manual_root / 'markdown' / f'{key}.md').read_text(encoding='utf-8')
        require('not the manual text' in locator and manual['sha256'] in locator, f'invalid manual locator: {key}')
    return {'status': 'PASS', 'current_skills': 33, 'source_available_skills': 31,
            'source_explicit_disabled_skills': 2, 'indexed_files_verified': len(rows),
            'manifest_files_not_self_hashed': 1, 'total_package_files': len(actual),
            'indexed_resource_bytes': resource_bytes, 'local_links_checked': checked_links,
            'cross_skill_links_checked': cross_links, 'external_manual_editions': 8,
            'all_sha256_sizes_and_git_executable_bits_match': True,
            'original_host_paths_required_for_verification': False,
            'skill_or_vendor_runtime_execution_performed': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, help='verify another copied package (read-only)')
    args = parser.parse_args()
    root = (args.root or Path(__file__).resolve().parents[1]).resolve()
    try:
        result = verify(root)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'FAIL', 'reason': str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
