#!/usr/bin/env python3
"""Read-only checks for the publishable source tree and official FPGA input closure."""
from pathlib import Path
import hashlib,json,os,re,runpy,subprocess,sys,xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
listed=subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z','--cached','--others','--exclude-standard'])
names=sorted(set(os.fsdecode(n) for n in listed.split(b'\0') if n))
forbidden=bytes.fromhex('6e6f61696469')
bad_names=[];bad_contents=[];large=[];private_files=[];symlinks=[]
for name in names:
    p=ROOT/name
    if p.is_symlink():symlinks.append(name)
    if not p.is_file():raise AssertionError('Missing indexed file: '+name)
    body=p.read_bytes()
    if forbidden in os.fsencode(name).lower():bad_names.append(name)
    if forbidden in body.lower():bad_contents.append(name)
    if len(body)>=100*1024*1024:large.append(name)
    if p.suffix.lower() in {'.keystore','.jks','.p12','.pfx'} or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',body):private_files.append(name)
assert not bad_names and not bad_contents,{'obsolete_paths':bad_names,'obsolete_content':bad_contents}
assert not large and not private_files and not symlinks,{'oversize':large,'private_material':private_files,'symlinks':symlinks}
license_text=(ROOT/'LICENSE').read_text()
assert license_text.startswith('MIT License\n') and 'Copyright (c) 2026 ICECREAM1230' in license_text
project=ROOT/'fpga/ethereal_cloud_tone.gprj'
entries=list(ET.parse(project).iter('File'))
assert len(entries)==278 and sum(e.get('enable')!='0' for e in entries)==276
for entry in entries:
    relative=Path(entry.attrib['path']);p=project.parent/relative
    assert not relative.is_absolute() and p.resolve().is_relative_to(project.parent.resolve()) and p.is_file(),str(p)
    assert str(p.relative_to(ROOT)) in names,'Required GPRJ input is ignored: '+str(p)
helpers=runpy.run_path(str(ROOT/'fpga/scripts/gowin_results.py'))
inputs=helpers['inputs'](project,ROOT/'fpga/scripts/run_control_project.tcl')
assert all(str((project.parent/n).resolve().relative_to(ROOT)) in names for n in inputs),'Official input missing from published file list'
image=runpy.run_path(str(ROOT/'fpga/scripts/verify_ae350_itcm.py'))['verify_project'](project.parent,'syn')
assert image['status']=='PASS',image
print(json.dumps({'status':'PASS','publishable_files':len(names),'obsolete_name_paths':0,'obsolete_name_bytes':0,'private_keys_or_signing_stores':0,'files_over_100MiB':0,'gprj_entries':len(entries),'gprj_enabled_entries':276,'official_input_files':len(inputs),'all_official_inputs_in_source_tree':True,'ae350_embedded_image_matches_binary':True,'ae350_binary_bytes':image['generated']['binary_bytes'],'scope':'Source integrity, naming, selected private-key signatures and build inputs only; not functional, timing or hardware acceptance'},ensure_ascii=False,indent=2))
