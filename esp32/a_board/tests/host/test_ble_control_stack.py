#!/usr/bin/env python3
"""Budget the actual Xtensa worker callgraph, including route/sink callbacks.

Requires the target build's -fstack-usage -fcallgraph-info=su output. The
external/context reserve is explicit; this does not claim runtime high-water
or measure the NimBLE host's complete caller stack.
"""
import argparse
import json
import re
from pathlib import Path


def attributes(line):
    return {key: json.loads(value) for key, value in re.findall(
        r'(title|label|sourcename|targetname): ("(?:\\.|[^"\\])*")', line)}


def check(build, source, reserve):
    directory = build / 'esp-idf/main/CMakeFiles/__idf_main.dir'
    names = ['ble_control.c', 'ble_control_codec.c', 'control_api.c', 'control_source.c',
             'ble_midi.c', 'ble_midi_unframer.c', 'midi_feedback_guard.c', 'raw_midi.c',
             'web_control.c', 'web_raw_midi.c', 'web_raw_queue.c', 'event_bus.c',
             'parameter_cc.c', 'engine_link.c', 'wifi_web.c']
    if (source / 'ks_timbre_document.c').exists():
        names += ['ks_timbre_document.c', 'ks_timbre_control.c', 'ks_timbre_params.c',
                  'fdc_control.c', 'vendor/tomlc17/tomlc17.c']
    graphs = [directory / (name + '.ci') for name in names]
    for name, graph in zip(names, graphs):
        if not graph.is_file():
            raise ValueError('missing actual target callgraph: ' + str(graph))
        if (source / name).stat().st_mtime_ns > graph.stat().st_mtime_ns:
            raise ValueError('callgraph is older than source: ' + name)
    # Include other target-instrumented project callees (for example DDS),
    # rather than treating known available frames as part of SDK reserve.
    graphs = sorted(set(graphs) | set(directory.rglob('*.ci')))
    nodes, edges = {}, {}
    for graph in graphs:
        for line in graph.read_text().splitlines():
            item = attributes(line)
            if line.startswith('node:'):
                usage = re.search(r'(\d+) bytes \(([^)]+)\)', item['label'])
                if usage:
                    nodes[item['title']] = {
                        'function': item['label'].splitlines()[0],
                        'bytes': int(usage[1]), 'allocation': usage[2]}
            elif line.startswith('edge:'):
                edges.setdefault(item['sourcename'], set()).add(item['targetname'])
    by_name = {}
    for key, node in nodes.items():
        by_name.setdefault(node['function'], []).append(key)

    # GCC reports these as indirect calls. Resolve only the actual route
    # table, response sink and packet sink; NPL/SDK internals use the reserve.
    table = (source / 'control_api.c').read_text().split('s_routes[]={', 1)[1].split('\n};', 1)[0]
    entries = re.findall(r'\{\s*"[^"]+"\s*,([^\n]+)\}', table)
    route_rows = [entry.split(',') for entry in entries]
    if not route_rows or any(len(row) != 8 for row in route_rows):
        raise ValueError('shared-core route table was not recognized')
    routes = [row[-1].strip() for row in route_rows]
    # prepare sets fixed=(mutation || capture). emit returns the frozen body
    # before calling a route handler when fixed=true. These routes therefore
    # cannot re-enter KS/FDC mutations during the sending pass.
    emit_routes = [row[-1].strip() for row in route_rows
                   if row[2].strip() == 'false' and row[5].strip() == 'false']
    for entry in ('control_api_prepare', 'control_api_emit'):
        for key in by_name.get(entry, []):
            for route in routes if entry == 'control_api_prepare' else emit_routes:
                candidates = by_name.get(route, [])
                if len(candidates) != 1:
                    raise ValueError('missing/ambiguous compiled route: ' + route)
                edges.setdefault(key, set()).add(candidates[0])
    for key, children in edges.items():
        if key not in nodes or '__indirect_call' not in children:
            continue
        name = nodes[key]['function']
        if name == 'flush_packet':
            children.update(by_name.get('transmit', []))

    ks_source = source / 'ks_timbre_document.c'
    bounds = {}
    if ks_source.exists():
        text = ks_source.read_text()
        depth = re.search(r'KS_TIMBRE_TOML_DEPTH_MAX\s*=\s*(\d+)', text)
        json_depth = re.search(r'depth\s*>\s*(\d+)\s*\|\|\s*p->used', text)
        vendor = (source / 'vendor/tomlc17/tomlc17.c').read_text()
        keys = re.search(r'^#define\s+KEYPARTMAX\s+(\d+)', vendor, re.M)
        wired_guard = re.search(r'if\s*\(\s*!toml_depth_safe\(s,\s*n\)\s*\)\s*return false', text)
        if not depth or not json_depth or not keys or not wired_guard:
            raise ValueError('missing verified KS JSON/TOML depth guards')
        bounds = {'toml_containers': int(depth[1]), 'json_value_calls': int(json_depth[1]) + 2,
                  'key_parts': int(keys[1])}
        # Global AOT headers may add two nodes per key part; a subsequent
        # dotted assignment adds another key chain. Each inline table can
        # add at most KEYPARTMAX nodes. Include root and terminal scalar.
        bounds['toml_global_tree_calls'] = (bounds['toml_containers'] + 3) * bounds['key_parts'] + 2

    def longest(key, active, sink='ble'):
        if key not in nodes:
            candidates = by_name.get(key, [])
            if len(candidates) != 1:
                return 0, []
            key = candidates[0]
        node = nodes[key]
        name = node['function']
        vendor_node = '/vendor/tomlc17/' in key
        containers = sum(nodes[item]['function'] in ('parse_inline_array', 'parse_inline_table')
                         and '/vendor/tomlc17/' in item for item in active)
        if vendor_node and name in ('parse_inline_array', 'parse_inline_table'):
            if not any(nodes[item]['function'] == 'ks_timbre_document_parse' for item in active):
                raise ValueError('TOML reached outside the verified KS depth-guarded path')
            if containers >= bounds['toml_containers']:
                return 0, []  # Input was rejected before entering pinned vendor.
        occurrences = active.count(key)
        if occurrences:
            if '/ks_timbre_document.c:' in key and name == 'value':
                if occurrences >= bounds['json_value_calls']:
                    return 0, []  # Includes the depth>5 rejection invocation.
            elif vendor_node and name in ('set_source_recursive', 'set_flag_recursive', 'datum_free'):
                limit = bounds['toml_global_tree_calls'] if not containers else \
                    (bounds['toml_containers'] - containers + 1) * bounds['key_parts'] + 2
                if occurrences >= limit:
                    return 0, []
            elif vendor_node and name in ('parse_val', 'parse_inline_array', 'parse_inline_table'):
                pass  # The combined container budget above bounds this cycle.
            else:
                raise ValueError('unaccounted recursive worker call path: ' + key)
        if node['allocation'] != 'static':
            raise ValueError('non-static compiled frame: ' + key)
        children = set(edges.get(key, []))
        if name in ('control_api_emit', 'api_chunk') and '__indirect_call' in children:
            # prepare captures/discards output; it never invokes a transport
            # sink. Only emit may call the BLE encoder or HTTP chunk writer.
            preparing = any(nodes[item]['function'] == 'control_api_prepare' for item in active)
            if not preparing:
                children.update(by_name.get('encode_body' if sink == 'ble' else 'http_control_write', []))
        child_bytes, child_path = max(
            (longest(child, active + (key,), sink) for child in sorted(children)),
            key=lambda value: value[0], default=(0, []))
        return node['bytes'] + child_bytes, [node] + child_path

    match = re.search(r'^#define\s+CONTROL_TASK_STACK\s+(\d+)u?\s*$',
                      (source / 'ble_control.c').read_text(), re.M)
    if not match:
        raise ValueError('CONTROL_TASK_STACK must be an explicit byte budget')
    stack = int(match[1])
    chains = {}
    roots = ['control_worker', 'notify_event', 'access_request', 'start_deferred',
             'ble_rx_task', 'ble_midi_feedback_notify_event']
    http_enabled = 'http_control' in by_name
    if http_enabled:
        roots.append('http_control')
    else:
        configuration = build / 'config/sdkconfig.h'
        if not configuration.exists() or re.search(r'^#define\s+CONFIG_ECT_WIFI_WEB\s+1',
                                                  configuration.read_text(), re.M):
            raise ValueError('missing HTTP adapter frame without an explicit Wi-Fi-disabled configuration')
    for root in roots:
        if len(by_name.get(root, [])) != 1:
            raise ValueError('missing/ambiguous entry: ' + root)
        length, path = longest(by_name[root][0], (), 'http' if root == 'http_control' else 'ble')
        chains[root] = {'compiled_bytes': length, 'path': path}
    # Include the concrete URI wrapper above the common adapter. HTTP server
    # and RTOS callers beyond this compilation unit use the explicit reserve.
    wrappers = [key for key, children in edges.items() if any(
        nodes.get(child, {}).get('function') == 'http_control' for child in children)]
    def concrete_http_path(value):
        length, path = value
        handler = next((node['function'] for node in path
                        if node['function'].startswith('core_')), '')
        # Equal-frame URI wrappers all call the same adapter. Prefer the
        # wrapper whose fixed URI actually selects the displayed core handler.
        matching = bool(path and handler == 'core_' + path[0]['function'])
        return length, matching
    http_total, http_path = max((longest(key, (), 'http') for key in wrappers),
                               key=concrete_http_path, default=(0, []))
    if http_enabled and http_total > chains['http_control']['compiled_bytes']:
        chains['http_control'] = {'compiled_bytes': http_total, 'path': http_path}
    maximum = chains['control_worker']['compiled_bytes']
    http_match = re.search(r'http\.stack_size\s*=\s*(\d+)', (source / 'wifi_web.c').read_text())
    if not http_match:
        raise ValueError('HTTP task requires an explicit stack byte budget')
    http_stack = int(http_match[1])
    http_maximum = chains['http_control']['compiled_bytes'] if http_enabled else 0
    rx_match = re.search(r'^#define\s+BLE_RX_TASK_STACK\s+(\d+)',
                         (source / 'ble_midi.c').read_text(), re.M)
    if not rx_match:
        raise ValueError('standard BLE MIDI RX requires an explicit stack byte budget')
    rx_stack = int(rx_match[1])
    rx_maximum = chains['ble_rx_task']['compiled_bytes']
    return {
        'scope': 'actual compiled worker plus resolved shared-core routes and BLE sinks',
        'callgraphs': [str(graph) for graph in graphs],
        'indirect_route_candidates': routes,
        'emit_route_candidates': emit_routes,
        'route_count': len(routes), 'verified_recursion_bounds': bounds,
        'task_stack_bytes': stack, 'external_and_context_reserve': reserve,
        'maximum_compiled_worker_bytes': maximum,
        'nominal_remaining_bytes': stack - reserve - maximum,
        'http_stack_bytes': http_stack, 'maximum_compiled_http_bytes': http_maximum,
        'http_enabled': http_enabled,
        'http_nominal_remaining_bytes': http_stack - reserve - http_maximum,
        'midi_rx_stack_bytes': rx_stack, 'maximum_compiled_midi_rx_bytes': rx_maximum,
        'midi_rx_nominal_remaining_bytes': rx_stack - reserve - rx_maximum,
        'callback_scope': 'local compiled subtrees; complete NimBLE/Timer caller stacks not measured',
        'chains': chains, 'passed': maximum + reserve <= stack and
        (not http_enabled or http_maximum + reserve <= http_stack) and rx_maximum + reserve <= rx_stack}


def show_path(path):
    parts = []
    for node in path:
        name = node['function']
        if parts and parts[-1][0] == name:
            parts[-1][1] += 1
        else:
            parts.append([name, 1])
    return ' -> '.join(name if count == 1 else '{} x{}'.format(name, count)
                       for name, count in parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--source', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'main')
    parser.add_argument('--reserve', type=int, default=1024)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.reserve < 1024:
        parser.error('at least 1024 bytes must remain for external/context calls')
    try:
        result = check(args.build, args.source, args.reserve)
    except (OSError, KeyError, IndexError, ValueError) as error:
        parser.error(str(error))
    if args.report:
        args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print('ble_control stack: {} {}B compiled + {}B reserve <= {}B task; nominal margin={}B'.format(
        'PASS' if result['passed'] else 'FAIL', result['maximum_compiled_worker_bytes'],
        args.reserve, result['task_stack_bytes'], result['nominal_remaining_bytes']))
    print(show_path(result['chains']['control_worker']['path']))
    if result['http_enabled']:
        print('HTTP adapter stack: {}B compiled + {}B reserve <= {}B task; nominal margin={}B'.format(
            result['maximum_compiled_http_bytes'], args.reserve, result['http_stack_bytes'],
            result['http_nominal_remaining_bytes']))
        print(show_path(result['chains']['http_control']['path']))
    else:
        print('HTTP adapter not compiled: verified Wi-Fi-disabled configuration')
    print('Standard BLE MIDI RX: {}B compiled + {}B reserve <= {}B task; nominal margin={}B'.format(
        result['maximum_compiled_midi_rx_bytes'], args.reserve, result['midi_rx_stack_bytes'],
        result['midi_rx_nominal_remaining_bytes']))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
