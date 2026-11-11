#!/usr/bin/env python3
"""JSON-only Gowin IP adapter for agents. No project integration or hardware writes."""
import argparse,atexit,fcntl,csv,fnmatch,hashlib,json,os,re,signal,subprocess,sys,time
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parent
IDE=Path(os.environ.get('GOWIN_IDE','/home/ice/Gowin/V1.9.12.04/IDE')).resolve()
# The C++ bridge is ABI/version specific; do not silently load it into another IDE.
PINNED_IDE=Path('/home/ice/Gowin/V1.9.12.04/IDE')
FILE_INPUTS={
 'ADVANCED_FIR':{'coefficients':'lineEditCoefficientFile'},
 'EDID_PROM':{'initialization':'iniFileLineEdit'},
 'EQUALIZER':{'coefficients_a':'AFileLineEdit','coefficients_b':'BFileLineEdit'},
 'IIR_Filter':{'feedforward':'feedforwardCoeFile','feedback':'feedbackCoeFile'},
 'PDM2PCM':{'coefficients':'coffFileLineEdit'}}
SMOKE_PRESETS={
 'MIPI_DPHY':{'EnableRXcheckBox':True,'RXClockLane':True,'RXLane0':True},
 'MIPI_RX_Advance':{'checkClkLane':True,'dphyLane0CheckBox':True,'checkLane0':True},
 'MIPI_TX_Advance':{'dphyClkCheckBox':True,'dphyLane0CheckBox':True,'checkBoxClkLane':True,'checkBoxLane0':True}}
class RequestError(Exception):
 def __init__(self,status,message,**details):self.payload={'ok':False,'status':status,'message':message,**details}
class JsonParser(argparse.ArgumentParser):
 def error(self,message):raise RequestError('INVALID_ARGUMENTS',message)
def emit(x):print(json.dumps(x,ensure_ascii=False,indent=2))
def inventory():
 rows=[]
 for p in sorted((IDE/'ipcore').rglob('*.ipspec')):
  x=ET.parse(p).getroot();ip=x.find('ip');target=x.find('target');d=p.parent
  rows.append({'ip':str(p.relative_to(IDE/'ipcore')),'id':ip.get('id'),'label':ip.get('label'),'display_name':(ip.text or ip.get('label')).strip(),'category':ip.get('category'),'version':ip.get('version'),'default_module':target.get('module','') if target is not None else '', 'directory':d.name,'plugin_libraries':[str(v) for v in d.glob('lib*.so')],'device_rules':[{'pattern':(v.text or '').strip(),'supported':v.get('supported','true')=='true'} for v in x.findall('./devices/device')],'required_inputs':FILE_INPUTS.get(d.name,{}),'smoke_preset':SMOKE_PRESETS.get(d.name),'metadata_warning': 'Duplicate label/id in vendor metadata; category and spec path are required' if d.name in {'MIPI_CPHY_RX','DigitalPreDistortion'} else None})
 return rows
def resolve(ident,items=None):
 matches=[v for v in (items or inventory()) if ident in (v['ip'],v['id'],v['label'],v['display_name'],v['directory'])]
 if len(matches)!=1:raise RequestError('AMBIGUOUS_IP' if matches else 'IP_NOT_FOUND','Use an exact relative .ipspec path',candidates=[v['ip'] for v in matches])
 return matches[0]
def supported(dev,rules):
 matches=[r['supported'] for r in rules if any(fnmatch.fnmatchcase(n,r['pattern']) for n in [dev[3]+'-'+dev[6]+'-x',dev[3]+'-x'])]
 return not rules or (True in matches and False not in matches)
def vendor_stub(ip):
 evidence=[]
 for path in ip['plugin_libraries']:
  symbols=subprocess.run(['nm','-D','-C','--defined-only',path],capture_output=True,text=True,timeout=5)
  creates=[line for line in symbols.stdout.splitlines() if '::create(QString const&, DEV::DeviceArg const&)' in line]
  if not creates:continue
  checks=[]
  for line in creates:
   address=int(line.split()[0],16)
   dump=subprocess.run(['objdump','-d','--start-address='+hex(address),'--stop-address='+hex(address+1),path],capture_output=True,text=True,timeout=5).stdout
   checks.append(bool(re.search(r'^\s*[0-9a-f]+:\s+c3\s+ret\b',dump,re.M)))
   evidence.append({'library':path,'sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest(),'symbol':line,'disassembly':dump})
  if checks and all(checks):return evidence
 return []
def choose_device(ip,part=None,version=None,project=None):
 rows=[r for r in csv.reader((IDE/'data/device/device_info.csv').read_text().splitlines()) if len(r)>7]
 if project:
  x=ET.parse(Path(project)).getroot().find('Device')
  if x is None:raise RequestError('INVALID_PROJECT','No Device element in project')
  matches=[r for r in rows if r[0]==(x.text or '').strip() and r[1]==x.get('pn')]
  if len(matches)!=1:raise RequestError('DEVICE_NOT_FOUND','Project device does not resolve uniquely')
  if part and part!=matches[0][1]:raise RequestError('DEVICE_CONFLICT','Explicit part differs from project device')
  if version and version!=(matches[0][5] or 'NA'):raise RequestError('DEVICE_CONFLICT','Explicit device version differs from project')
  chosen=matches[0];origin='project_read_only'
 elif part and part!='auto':
  matches=[r for r in rows if r[1]==part and (not version or (r[5] or 'NA')==version)]
  if len(matches)!=1:raise RequestError('DEVICE_AMBIGUOUS','Specify registered part and device version',candidates=[{'part':r[1],'version':r[5] or 'NA'} for r in matches])
  chosen=matches[0];origin='explicit'
 else:
  matches=[r for r in rows if supported(r,ip['device_rules'])]
  if not matches:raise RequestError('NO_REGISTERED_SUPPORTED_DEVICE','Metadata has no matching installed device; does not prove the IP is unusable')
  preferred=[r for r in matches if r[1]=='GW5AST-LV138PG484AC1/I0' and r[5]=='C']
  chosen=preferred[0] if preferred else max(matches,key=lambda r:int((re.search(r'(\d+)',r[6]) or ['0','0'])[1]));origin='isolated_auto_target'
 return {'id':chosen[0],'part':chosen[1],'version':chosen[5] or 'NA','name':chosen[3],'package':chosen[6],'source':origin,'metadata_support':supported(chosen,ip['device_rules'])}
def parse_parameters(value):
 if not value:return {}
 try:x=json.loads(Path(value[1:]).read_text() if value.startswith('@') else value)
 except (ValueError,OSError) as e:raise RequestError('INVALID_JSON',str(e))
 if not isinstance(x,dict):raise RequestError('INVALID_PARAMS','params must be a JSON object')
 return x
def validate_parameters(params,fields,check_constraints=True):
 validated={};order=[]
 for key,value in params.items():
  if key.split('/')[-1] in {'editCreateIn','editModuleName','editFileName','lineEditModuleName','lineEditFileName'}:raise RequestError('RESERVED_PARAMETER','Use output/module request fields; parameters cannot redirect generation or silently rename modules',parameter=key)
  matches=[f for f in fields if key in (f['object'],f.get('key')) and f['object']!='qt_spinbox_lineedit']
  if len(matches)!=1:raise RequestError('AMBIGUOUS_PARAMETER' if matches else 'UNKNOWN_PARAMETER','Use a unique schema key',parameter=key,candidates=[f.get('key') for f in matches])
  f=matches[0];typ=f['class']
  if f.get('read_only'):raise RequestError('PARAMETER_DISABLED','Parameter is not editable in this configuration',parameter=key)
  okay=True
  if typ=='QCheckBox':okay=type(value) is bool
  elif typ in ('QSpinBox','QDoubleSpinBox'):okay=type(value) in (int,float) and (typ!='QSpinBox' or type(value) is int) and (not check_constraints or f['min']<=value<=f['max'])
  elif typ=='QComboBox':okay=(isinstance(value,str) and (not check_constraints or value in f['choices'])) or (type(value) is int and (not check_constraints or 0<=value<len(f['choices'])))
  elif typ=='QLineEdit':okay=isinstance(value,str)
  if not okay:raise RequestError('INVALID_PARAMETER_VALUE','Value does not satisfy the live schema',parameter=key,value=value,schema=f)
  # Keep unqualified unique names where possible; full keys handle duplicate controls.
  validated[key]=value;order.append(key)
 return validated

def parameter_mismatches(params,fields):
 mismatches=[]
 for key,value in params.items():
  matches=[f for f in fields if key in (f['object'],f.get('key'))]
  if len(matches)!=1:
   mismatches.append({'parameter':key,'requested':value,'actual':None,'reason':'missing_or_ambiguous_widget'});continue
  f=matches[0];actual=f.get('index') if f['class']=='QComboBox' and type(value) is int else f.get('value')
  if actual!=value:mismatches.append({'parameter':key,'requested':value,'actual':actual})
 return mismatches

def run_vendor(ip,device,mode,module,out,params,timeout):
 paramfile=out.parent/(out.name+'.params.json');paramfile.write_text(json.dumps(params,ensure_ascii=False))
 cmd=[str(ROOT/'bin/vendor-runner'),'generate','--mode',mode,'--ip',ip['ip'],'--module',module,'--output',str(out),'--part',device['part'],'--device-version',device['version'],'--timeout',str(timeout),'--params',str(paramfile)]
 if ip['ip'].startswith('SERDES_IP/IPlib/'):
  cmd+=['--parent-ip','SERDES_IP/serdesip.ipspec','--protocol',ip['label']]
 with (out.parent/(out.name+'.runner.log')).open('w') as f:
  proc=subprocess.Popen(cmd,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  try:proc.wait(timeout=timeout+15)
  except BaseException:
   owned=[proc.pid]
   for parent in owned:
    for entry in Path('/proc').iterdir():
     if not entry.name.isdigit():continue
     try:
      text=(entry/'stat').read_text();fields=text[text.rfind(')')+2:].split()
      if int(fields[1])==parent and int(entry.name) not in owned:owned.append(int(entry.name))
     except OSError:pass
   for pid in reversed(owned):
    try:os.kill(pid,signal.SIGTERM)
    except ProcessLookupError:pass
   try:proc.wait(timeout=3)
   except subprocess.TimeoutExpired:
    for pid in reversed(owned):
     try:os.kill(pid,signal.SIGKILL)
     except ProcessLookupError:pass
    proc.wait()
   raise
 result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else {'returncode':proc.returncode}
 bridge=json.loads((out/'bridge.json').read_text()) if (out/'bridge.json').exists() else {}
 return result,bridge

def classify(result,bridge):
 state=bridge.get('status',result.get('bridge_status'))
 if result.get('generation_pass'):return 'GENERATED'
 msg=bridge.get('dialog_text','')
 if state=='GENERATOR_MESSAGE':
  return 'INPUT_REQUIRED' if re.search(r'file.*specified|coefficient|initialization',msg,re.I) else 'CONFIGURATION_REJECTED'
 if state=='NOT_AVAILABLE_FOR_TARGET':return 'CATALOG_ENTRY_UNRESOLVED'
 if state=='AMBIGUOUS_CATALOG_ENTRY':return 'AMBIGUOUS_IP'
 if result.get('returncode',0)<0:return 'VENDOR_PROCESS_CRASH'
 if result.get('returncode')==124:
  return 'UNHANDLED_DIALOG' if bridge.get('visible_surfaces') else 'TIMEOUT'
 return state or 'GENERATION_NOT_CONFIRMED'

def main(argv=None):
 ap=JsonParser(description=__doc__);ap.add_argument('--version',action='version',version='gowin-ip-ai 0.1 (Gowin V1.9.12.04 Linux adapter)');ap.add_argument('command',choices=['list','describe','inspect','generate','run']);ap.add_argument('--details',action='store_true');ap.add_argument('--request');ap.add_argument('--ip');ap.add_argument('--query',default='');ap.add_argument('--part');ap.add_argument('--device-version');ap.add_argument('--project');ap.add_argument('--output');ap.add_argument('--module');ap.add_argument('--params');ap.add_argument('--input',action='append',default=[]);ap.add_argument('--preset',choices=['smoke']);ap.add_argument('--timeout',type=int,default=180)
 a=ap.parse_args(argv)
 if a.command=='run':
  if not a.request:raise RequestError('REQUEST_REQUIRED','Use --request path/to/job.json')
  req=parse_parameters('@'+a.request);allowed={'action','ip','part','device_version','project','output','module','parameters','inputs','preset','timeout'}
  if set(req)-allowed:raise RequestError('UNKNOWN_REQUEST_KEYS','Request has unknown keys',keys=sorted(set(req)-allowed))
  action=req.get('action','generate')
  if action not in {'describe','inspect','generate'}:raise RequestError('INVALID_ACTION','Request action must be describe, inspect or generate')
  args=[action]
  for key in ['ip','part','device_version','project','output','module','preset','timeout']:
   if key in req:args+=['--'+key.replace('_','-'),str(req[key])]
  if 'parameters' in req:
   if not isinstance(req['parameters'],dict):raise RequestError('INVALID_PARAMS','parameters must be an object')
   args+=['--params',json.dumps(req['parameters'],ensure_ascii=False)]
  if not isinstance(req.get('inputs',{}),dict):raise RequestError('INVALID_INPUT','inputs must be an object')
  for key,path in req.get('inputs',{}).items():args+=['--input',key+'='+str(path)]
  return main(args)
 if IDE!=PINNED_IDE:raise RequestError('UNVERIFIED_IDE_VERSION','This bridge is verified only for the pinned installed IDE; rebuild and validate before changing it',ide=str(IDE))
 if a.command=='list':
  items=inventory();items=[x for x in items if a.query.casefold() in json.dumps(x,ensure_ascii=False).casefold()];emit({'ok':True,'status':'INVENTORY','count':len(items),'ips':items if a.details else [{k:v[k] for k in ['ip','id','display_name','category','required_inputs','metadata_warning']} for v in items]});return 0
 if not a.ip:raise RequestError('IP_REQUIRED','Specify --ip using list results')
 ip=resolve(a.ip);stub=vendor_stub(ip);ip['vendor_entry_stub']=stub;device=choose_device(ip,a.part,a.device_version,a.project)
 if a.command=='describe':emit({'ok':True,'status':'DESCRIPTION','ip':ip,'device':device,'next_step':'inspect for live parameter schema; generate with explicit input files and parameter JSON'});return 0
 if stub:raise RequestError('VENDOR_ENTRY_STUB','The installed plugin create() returns immediately without generation; input files cannot activate this entry',ip=ip['ip'],evidence=stub)
 if not a.output:raise RequestError('OUTPUT_REQUIRED','Use a new isolated --output directory')
 out=Path(a.output).resolve()
 if out.exists() and any(out.iterdir()):raise RequestError('OUTPUT_EXISTS','Existing nonempty output is never overwritten',output=str(out))
 if not 5<=a.timeout<=3600:raise RequestError('INVALID_TIMEOUT','timeout must be 5..3600 seconds')
 module=a.module or ip['default_module']
 if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',module or ''):raise RequestError('MODULE_REQUIRED','Supply a legal --module name',vendor_default=module)
 params=parse_parameters(a.params);inputs={}
 for entry in a.input:
  if '=' not in entry:raise RequestError('INVALID_INPUT','Use --input semantic_name=/absolute/file')
  key,path=entry.split('=',1)
  if key not in ip['required_inputs']:raise RequestError('UNKNOWN_INPUT','Input name not declared for this IP',input=key,known=ip['required_inputs'])
  file=Path(path).resolve()
  if not file.is_file() or file.stat().st_size==0:raise RequestError('INPUT_FILE_INVALID','Input must be an existing nonempty file',input=key,path=str(file))
  inputs[key]=file
 if a.command=='generate':
  for semantic,widget in ip['required_inputs'].items():
   if widget in params:raise RequestError('FILE_INPUT_REQUIRES_MAPPING','Use --input semantic_name=/absolute/path so files are copied and hashed',parameter=widget,input_name=semantic)
  missing=[key for key,field in ip['required_inputs'].items() if key not in inputs and not params.get(field)]
  if missing:raise RequestError('INPUT_REQUIRED','Supply real design inputs; this tool does not invent coefficients or initialization data',required_inputs=ip['required_inputs'],missing=missing)
 lease=open('/tmp/gowin-ip-ai-'+str(os.getuid())+'.lock','a+')
 try:fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:lease.close();raise RequestError('GENERATOR_BUSY','Another AI IP request is running; serialize generator requests and retry after it finishes')
 atexit.register(lease.close)
 out.mkdir(parents=True,exist_ok=True)
 try:
  fd=os.open(out/'.ai-ip-owner',os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600);os.write(fd,str(os.getpid()).encode());os.close(fd)
 except FileExistsError:raise RequestError('OUTPUT_IN_USE','Output is already owned by another invocation')
 result,bridge=run_vendor(ip,device,'inspect',module,out/'schema_probe',params if a.command=='inspect' else {},min(a.timeout,30))
 schema={'ip':ip,'device':device,'fields':bridge.get('fields_requested',bridge.get('fields_before',[])),'dialog_class':bridge.get('dialog_class'),'visible_surfaces':bridge.get('visible_surfaces',[]),'catalog':bridge.get('catalog',[]),'status':bridge.get('status')}
 (out/'schema.json').write_text(json.dumps(schema,ensure_ascii=False,indent=2))
 if bridge.get('status')!='INSPECTED':
  reply={'ok':False,'status':classify(result,bridge),'message':'Generator schema could not be obtained; inspect saved dialog/catalog diagnostics','ip':ip['ip'],'output':str(out),'schema':str(out/'schema.json')};(out/'response.json').write_text(json.dumps(reply,ensure_ascii=False,indent=2));emit(reply);return 3
 if a.command=='generate' and a.module and not any(f['object'] in ('editModuleName','lineEditModuleName') and not f.get('read_only') for f in schema['fields']):raise RequestError('MODULE_FIXED','This generator has a vendor-fixed module name; omit --module and read effective_modules from result')
 if a.command=='inspect':emit({'ok':True,'status':'INSPECTED','schema':str(out/'schema.json'),**schema});return 0
 if a.preset:
  if not ip['smoke_preset']:raise RequestError('PRESET_UNAVAILABLE','No verified smoke preset for this IP')
  params={**ip['smoke_preset'],**params}
 input_manifest=[]
 for key,file in inputs.items():
  import shutil
  dest=out/'inputs'/key/file.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest);params[ip['required_inputs'][key]]=str(dest)
  input_manifest.append({'name':key,'source':str(file),'copied_to':str(dest),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
 # Validate also raw file widget paths: no missing file may be silently accepted.
 for key in ip['required_inputs'].values():
  if key in params and (not isinstance(params[key],str) or not Path(params[key]).is_file()):raise RequestError('INPUT_FILE_INVALID','File widget must name an existing file',parameter=key)
 validate_parameters(params,schema['fields'],check_constraints=False)
 result,bridge=run_vendor(ip,device,'generate',module,out/'generation',params,a.timeout)
 status=classify(result,bridge);actual_fields={f['object']:f for f in bridge.get('fields_requested',[])}
 changed=parameter_mismatches(params,bridge.get('fields_requested',[]))
 if changed and status=='GENERATED':status='PARAMETER_NOT_RETAINED'
 reply={'ok':status=='GENERATED','status':status,'ip':ip['ip'],'module':module,'device':device,'output':str(out),'implementation_directory':str(out/'generation/ip'),'schema':str(out/'schema.json'),'requested_parameters':params,'parameters_verified_in_widgets':not changed,'parameter_mismatches':changed,'input_files':input_manifest,'preset':a.preset,'preset_scope':'Generation smoke configuration only; not board-specific' if a.preset else None,'vendor_message':bridge.get('dialog_text',bridge.get('message')),'effective_modules':result.get('effective_modules',[]),'module_policy':bridge.get('module_policy'),'files':result.get('files',[]),'configuration_files':result.get('configs',[]),'parameter_semantic_verification':'Inspect IPC and public HDL against intended design; widget retention alone is not semantic verification','four_files':result.get('required_files_present',{}),'checks':{'generation':status=='GENERATED','functional_simulation':'not_run','synthesis':'not_run','pnr':'not_run'},'diagnostics':{'bridge':str(out/'generation/bridge.json'),'log':str(out/'generation/tool.log')}}
 (out/'response.json').write_text(json.dumps(reply,ensure_ascii=False,indent=2));emit(reply);return 0 if reply['ok'] else 3
if __name__=='__main__':
 try:raise SystemExit(main())
 except RequestError as e:emit(e.payload);raise SystemExit(2)
 except Exception as e:emit({'ok':False,'status':'TOOL_ERROR','message':str(e)});raise SystemExit(4)
