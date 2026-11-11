#!/usr/bin/env python3
"""Check owner snapshot call chains, reserving stack for external calls/interrupts.

GCC emits the callgraph with -fcallgraph-info=su using the actual target flags.
This checks the voice-owner state publication path. It does not
measure external callees or replace runtime high-water and board regressions.
"""
import argparse
import json
import re
from pathlib import Path


def attributes(line, names):
    pattern = r'(' + '|'.join(names) + r'): ("(?:\\.|[^"\\])*")'
    return {key: json.loads(value) for key, value in re.findall(pattern, line)}


def owner_stack(callgraph, source, reserve):
    match = re.search(r'^#define\s+VA_TASK_STACK\s+(\d+)\s*$', source.read_text(), re.M)
    if not match:
        raise ValueError("VA_TASK_STACK must be an explicit byte budget")
    stack_bytes = int(match[1])
    nodes, edges = {}, {}
    for line in callgraph.read_text().splitlines():
        if line.startswith('node:'):
            item = attributes(line, ['title', 'label'])
            usage = re.search(r'(\d+) bytes \(([^)]+)\)', item['label'])
            if usage:
                nodes[item['title']] = {
                    'function': item['label'].splitlines()[0],
                    'bytes': int(usage[1]), 'allocation': usage[2]}
        elif line.startswith('edge:'):
            item = attributes(line, ['sourcename', 'targetname'])
            edges.setdefault(item['sourcename'], []).append(item['targetname'])
    entries = [key for key, node in nodes.items() if node['function'] == 'voice_alloc_task']
    if len(entries) != 1:
        raise ValueError("missing or ambiguous compiled voice_alloc_task")

    # The additive snapshot APIs are retired. Keep checking the full web
    # snapshot publication; unrelated CC/Panic callgraph cycles need argument-
    # sensitive analysis and are outside this regression's declared scope.
    sinks = {'web_control_store'}
    reachable = set(sinks)
    changed = True
    while changed:
        previous = len(reachable)
        reachable.update(node for node, children in edges.items()
                         if any(child in reachable for child in children))
        changed = len(reachable) != previous
    if entries[0] not in reachable:
        raise ValueError("owner snapshot call paths were not compiled")

    def longest(node, active):
        if node not in nodes:
            return 0, []  # External callee: covered by the explicit reserve.
        if node in active:
            raise ValueError("recursive owner call path: " + node)
        frame = nodes[node]
        if frame['allocation'] != 'static':
            raise ValueError("unbounded owner frame: " + node)
        child_bytes, child_path = max(
            (longest(child, active | {node}) for child in edges.get(node, [])
             if child in reachable),
            key=lambda item: item[0], default=(0, []))
        return frame['bytes'] + child_bytes, [frame] + child_path

    total, path = longest(entries[0], set())
    return {'scope': 'compiled owner state publication path',
            'task_stack_bytes': stack_bytes, 'external_and_interrupt_reserve': reserve,
            'compiled_owner_limit': stack_bytes - reserve,
            'max_compiled_owner_frames': total, 'path': path,
            'passed': total <= stack_bytes - reserve}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--callgraph', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--reserve', type=int, default=1024)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.reserve < 1024:
        parser.error("reserve must leave at least 1024 bytes for external calls and interrupts")
    try:
        report = owner_stack(args.callgraph, args.source, args.reserve)
    except (KeyError, ValueError) as error:
        parser.error(str(error))
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    path = ' -> '.join(item['function'] for item in report['path'])
    print('voice_alloc snapshot stack: {} {}B owner frames <= {}B budget '
          '({}B task - {}B reserve)'.format(
              'PASS' if report['passed'] else 'FAIL', report['max_compiled_owner_frames'],
              report['compiled_owner_limit'], report['task_stack_bytes'],
              report['external_and_interrupt_reserve']))
    print(path)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
