#!/usr/bin/env python3
"""Compile the frozen C parser without altering it and validate actual Java encoder output."""
import ctypes
import hashlib
import json
import pathlib
import subprocess
import tarfile
from toolchain import ROOT, JDK_BIN as JDK
from fm_contract import check_fm_contract, core_sources
check_fm_contract()
OUT=ROOT/'build/abi'
OUT.mkdir(parents=True,exist_ok=True)
# The original frozen inputs travel with this project, independently of its location.
reference=ROOT/'docs/integration-20261005/app-api-reference.tar.gz'
provenance=json.loads((ROOT/'docs/integration-20261005/参考来源.json').read_text())
if hashlib.sha256(reference.read_bytes()).hexdigest()!=provenance['archive_sha256']:
    raise SystemExit('Frozen reference archive changed')
with tarfile.open(reference) as stream:
    stream.extractall(OUT/'reference-root',filter='data')
HANDOFF=OUT/'reference-root/handoff'
for f in json.loads((HANDOFF/'基线与来源-20261005.json').read_text())['reference_files']:
    actual=hashlib.sha256((HANDOFF/'reference'/f['path']).read_bytes()).hexdigest()
    if actual!=f['sha256']:raise SystemExit('Reference changed: '+f['path'])
source=(HANDOFF/'reference/development/tang_mega_138k_synth/esp32c3/main/web_control.c').read_text()
functions=source[source.index('bool web_control_cc_allowed'):]
stub='''#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define WEB_CC 2
#define WEB_KEYS_UPDATE 3
#define WEB_STOP 4
#define WEB_ENGINE 5
#define WEB_KS_DECAY 6
#define WEB_KEYS_MASK ((1u<<25)-1)
typedef struct {uint32_t token,sequence,op,a,b;} web_event_t;
'''
(OUT/'frozen_parser.c').write_text(stub+functions)
subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(OUT/'frozen_parser.c'),'-o',str(OUT/'parser.so')],check=True)
lib=ctypes.CDLL(str(OUT/'parser.so'))
lib.web_control_parse.argtypes=[ctypes.c_char_p,ctypes.c_void_p];lib.web_control_parse.restype=ctypes.c_bool
core=core_sources()
subprocess.run([str(JDK/'javac'),'--release','8','-encoding','UTF-8','-d',str(OUT),*[str(x) for x in core],str(ROOT/'tools/ProtocolVectors.java')],check=True)
encoded=subprocess.check_output([str(JDK/'java'),'-cp',str(OUT),'ProtocolVectors'],text=True).splitlines()
for body in encoded:
    event=(ctypes.c_uint32*5)()
    if not lib.web_control_parse(body.encode('ascii'),event):raise AssertionError('C rejected '+body)
    if list(event)!=[int(x) for x in body.split(' ')]:raise AssertionError('decode mismatch')
invalid=['1 1 2 64 127','1 1 2 119 0','1 1 6 199 0','1 1 6 8001 0','0 1 3 0 100','1 0 3 0 100',
    '1 1 3 33554432 100','1 1 3 1 0','1 1 3 -1 100','4294967296 1 3 0 100','1 1 3 0 100\n','1 1 3 0 100 ',
    '1  1 3 0 100','1\t1 3 0 100','1 1 4294967299 0 100','1 1 4 1 0']
for body in invalid:
    if lib.web_control_parse(body.encode('ascii'),(ctypes.c_uint32*5)()):raise AssertionError('invalid C accepted '+repr(body))
print(f'RESULT frozen ABI: 53 reference hashes verified; {len(encoded)} actual Java bodies accepted and decoded exactly by frozen C; {len(invalid)} malformed/unsupported bodies rejected')
