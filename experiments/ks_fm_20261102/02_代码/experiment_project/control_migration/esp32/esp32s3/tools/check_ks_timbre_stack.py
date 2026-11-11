#!/usr/bin/env python3
"""Bound actual Xtensa compiler frames on the new KS parameter compile path.

External libm/RTOS/interrupt frames are reserved, not measured. Board stack
high-water and worst-case control latency remain hardware acceptance items.
"""
import argparse
import json
import re
from pathlib import Path
from check_owner_stack import attributes

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--owner', type=Path, required=True)
p.add_argument('--compiler', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
nodes, edges = {}, {}
for path in (a.owner, a.compiler):
    for line in path.read_text().splitlines():
        if line.startswith('node:'):
            item = attributes(line, ['title', 'label'])
            usage = re.search(r'(\d+) bytes \(([^)]+)\)', item['label'])
            if usage:
                nodes[item['title']] = {'function': item['label'].splitlines()[0], 'bytes': int(usage[1]), 'allocation': usage[2]}
        elif line.startswith('edge:'):
            item = attributes(line, ['sourcename', 'targetname'])
            edges.setdefault(item['sourcename'], set()).add(item['targetname'])
entry = next(k for k, n in nodes.items() if n['function'] == 'voice_alloc_task')
sink = next(k for k, n in nodes.items() if n['function'] == 'ks_timbre_compile')
reach = {sink}
while True:
    before = len(reach)
    reach.update(k for k, children in edges.items() if children & reach)
    if before == len(reach):
        break
if entry not in reach:
    raise SystemExit('actual owner does not reach the KS compiler')

def max_path(key, active, only_path):
    if key not in nodes:
        return 0, []
    if key in active:
        raise ValueError('recursive KS path')
    node = nodes[key]
    if node['allocation'] != 'static':
        raise ValueError('unbounded KS frame: ' + key)
    children = [c for c in edges.get(key, ()) if not only_path or key == sink or c in reach]
    size, path = max((max_path(c, active | {key}, only_path and key != sink) for c in children), key=lambda x: x[0], default=(0, []))
    return node['bytes'] + size, [node] + path

size, path = max_path(entry, set(), True)
stack = int(re.search(r'^#define\s+VA_TASK_STACK\s+(\d+)', a.source.read_text(), re.M)[1])
reserve = 2048
result = {'scope': 'actual owner plus KS compiler and compiled helper frames', 'frames_bytes': size, 'task_stack_bytes': stack, 'external_interrupt_reserve': reserve, 'passed': size + reserve <= stack, 'path': path}
a.report.write_text(json.dumps(result, indent=2) + '\n')
print(f'KS compile stack: {"PASS" if result["passed"] else "FAIL"} {size}+{reserve} <= {stack} bytes')
raise SystemExit(0 if result['passed'] else 1)
