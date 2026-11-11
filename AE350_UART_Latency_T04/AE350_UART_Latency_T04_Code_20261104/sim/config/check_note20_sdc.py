#!/usr/bin/env python3
"""Actual-netlist coverage for the bounded NoteOn observer, not vendor STA."""
import argparse
import collections
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--netlist', type=Path, default=ROOT/'impl/gwsynthesis/ae350_latency_lab.vg')
p.add_argument('--sdc', type=Path, default=ROOT/'constraints/latency_lab.sdc')
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
modules = {}
cell_re = re.compile(r'^\s*([\w$]+)\s+(\\\S+|[\w$]+)\s*\(\s*(.*?)\);', re.M|re.S)
port_re = re.compile(r'\.(\\\S+|[\w$]+)\s*\(\s*([^()]*)\s*\)')
for m in re.finditer(r'^module\s+(\S+).*?^endmodule', a.netlist.read_text(), re.M|re.S):
    b = m.group(0)
    b = b[b.index(';')+1:]
    modules[m.group(1)] = {n.lstrip('\\'): (k, {q.lstrip('\\'): v.strip() for q,v in port_re.findall(z)})
                          for k,n,z in cell_re.findall(b)}

def resolve(path):
    k = 'midi_pcm_latency_lab_top'
    for n in path.split('/'):
        k = modules[k][n][0]
    return k

def inventory(k, path):
    result = {}
    for n,(child,ps) in modules.get(k,{}).items():
        full = path+'/'+n
        if child in modules:
            result.update(inventory(child,full))
        else:
            for pin in ps:
                result[full+'/'+pin] = child
    return result

def matches(name, pattern):
    # Gowin SUG940: only * and ? are wildcards, including across hierarchy.
    return re.fullmatch(re.escape(pattern).replace(r'\*','.*').replace(r'\?','.'),name) is not None

# Architecture changed by the user's explicit T0/T1/T2/T3 request. Old N203
# evidence remains immutable in its run/frozen files and must fail this checker.
roots = {
    'capture':'u_lab/u_system/g_cpu_timing.u_capture',
    'uart':'u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc',
}
errors,queries=[],[]
expected_all,covered_all=set(),set()
sdc=a.sdc.read_text()
for label,root in roots.items():
    try:
        pins=inventory(resolve(root),root)
        if not pins:raise ValueError('empty hierarchy')
        required=['u_begin','u_ack','u_audio','u_result'] if label=='capture' else []
        for stage in required:
            objects={x for x in pins if x.startswith(root+'/'+stage+'/')}
            if not objects or not any('/req_meta' in x and x.endswith('/D') for x in objects):
                errors.append('Required T04 CDC mailbox missing: '+stage)
        covered=set();local_queries=[]
        for number,line in enumerate(sdc.splitlines(),1):
            if root not in line or line.lstrip().startswith('#'):continue
            item={'line':number,'command':line,'groups':[],'hierarchy':root}
            for group in re.findall(r'\[get_pins\s+\{([^}]+)\}\]',line):
                patterns=group.split();hits={x for x in pins if any(matches(x,t) for t in patterns)}
                item['groups'].append({'patterns':patterns,'pins':sorted(hits),
                    'cell_types':dict(collections.Counter(pins[x] for x in hits))})
                if not hits:errors.append('Empty selector at line '+str(number))
                if line.startswith('set_false_path -to'):
                    covered|=hits
                    if any(not x.endswith('/D') or not pins[x].startswith('DFF') for x in hits):
                        errors.append('Not a physical first-stage DFF/D at '+str(number))
                    if len(hits)==1 and (len(patterns)!=1 or any(c in patterns[0] for c in '*?')):
                        errors.append('Singleton must use exact physical pin at '+str(number))
                elif line.startswith('set_max_delay') and not line.endswith('20.000'):
                    errors.append('Held mailbox budget is not 20ns')
            local_queries.append(item)
        maximum=[q for q in local_queries if q['command'].startswith('set_max_delay')]
        if len(maximum)!=1 or len(maximum[0]['groups'])!=2:
            errors.append('Expected one held-data budget pair for '+label)
        else:
            source=set(maximum[0]['groups'][0]['pins']);destination=set(maximum[0]['groups'][1]['pins'])
            source_expected={x for x,k in pins.items() if k.startswith('DFF') and
                re.search(r'/held_data(?:_\d+)?_s\d+/Q$',x)}
            destination_expected={x for x,k in pins.items() if k.startswith('DFF') and
                re.search(r'/dst_data_o(?:_\d+)?_s\d+/D$',x)}
            if source!=source_expected or destination!=destination_expected or not source_expected or not destination_expected:
                errors.append('Held physical DFF coverage mismatch for '+label)
        if len(local_queries)!=(7 if label=='capture' else 4):errors.append('Wrong rule count for '+label)
        expected={x for x,k in pins.items() if k.startswith('DFF') and
            re.search(r'/(?:ack_meta|req_meta|loss_meta|alive_meta|result_loss_meta)(?:_\d+)?_s\d+/D$',x)}
        if not expected:errors.append('No first stages identified for '+label)
        if expected-covered:errors.append('Missing first D: '+str(sorted(expected-covered)))
        if covered-expected:errors.append('Unexpected/second-stage exception: '+str(sorted(covered-expected)))
        expected_all|=expected;covered_all|=covered;queries+=local_queries
    except (KeyError,ValueError) as e:errors.append('Actual T04 hierarchy absent/mismatched '+label+': '+str(e))
for path in ['u_lab/u_system/g_note20_latency.u_batch','u_lab/g_note20_uart.u_uart','u_lab/u_system/g_midi_pcm_latency.u_probe']:
    try:
        if inventory(resolve(path),path):errors.append('Retired observer/formatter survived synthesis: '+path)
    except KeyError:pass
for number,line in enumerate(sdc.splitlines(),1):
    if not line.lstrip().startswith('#') and any(x in line for x in ['g_note20_latency.u_batch','g_note20_uart.u_uart','g_midi_pcm_latency.u_probe','u_lab/u_debug_monitor/']):
        errors.append('Active stale observer SDC at '+str(number))
record={'status':'PASS' if not errors else 'FAIL','scope':'Actual T04 mailbox first-D/held20ns coverage only, not official STA',
    'hierarchies':roots,'expected_first_stage_pins':len(expected_all),'covered_first_stage_pins':len(covered_all),
    'queries':queries,'errors':errors,
    'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.netlist,a.sdc,Path(__file__)]}}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps({k:record[k] for k in ['status','expected_first_stage_pins','covered_first_stage_pins','errors']}))
raise SystemExit(bool(errors))
