#!/usr/bin/env python3
"""Detached stage runner and log viewer. No device access, staging or commits."""
import datetime,hashlib,json,os,re,runpy,shutil,subprocess,sys,time,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def inputs():
    paths=[ROOT/'ethereal_cloud_tone.gprj',ROOT/'scripts/run_control_project.tcl',Path(__file__).resolve()]
    for e in ET.parse(paths[0]).getroot().findall('./FileList/File'):
        if e.get('enable')=='1':paths.append(ROOT/e.get('path'))
    paths+=list((ROOT/'firmware/ae350/app').rglob('*.[ch]'))+list((ROOT/'firmware/ae350/midi').glob('*.[ch]'))+list((ROOT/'firmware/ae350/include').glob('*.h'))+[ROOT/'firmware/ae350/app/start.S',ROOT/'firmware/ae350/app/control_link.ld',ROOT/'firmware/ae350/images/control.bin']
    paths+=list((ROOT/'rtl').rglob('*.hex'))+list((ROOT/'rtl').rglob('*.mem'))+list((ROOT/'rtl').rglob('*.vh'))+list((ROOT/'rtl').rglob('*.svh'))
    paths+=[ROOT/'ip/ae350/riscv_ae350_soc.ipc',ROOT/'scripts/build_control_firmware.py']
    paths+=list((ROOT/'ip/ae350/itcm_initial').glob('itcm*'))
    paths += [ROOT/'scripts'/name for name in ['run_gowin_synced.py','gowin_gui_sync.tcl','gowin_results.py','sync_control_uart_sdc.py']]
    # Bind the public generated ROM image and its official generation receipt.
    # Updating input byte lanes alone does not refresh protected AE350 HDL.
    paths += [ROOT/'scripts/verify_ae350_itcm.py',
              ROOT/'ip/ae350/RiscV_AE350_SOC_Top.vo',
              ROOT/'ip/ae350/embedded-firmware.json']
    # FDC uses real RV32 soft-float support; preserve every compiled source,
    # local compatibility header, build settings and exact library identity.
    paths += list((ROOT/'firmware/ae350/fdc').rglob('*.[ch]'))
    paths += list((ROOT/'firmware/ae350/fdc_integration').glob('*.[ch]'))
    record = ROOT/'firmware/ae350/images/control-build.json'
    if record.is_file():
        paths.append(record)
        paths += [ROOT / name for name in json.loads(record.read_text()).get('libraries', {})]
    return {str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p):
            hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
def dump(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n')
def verify_uart_sdc(render=False):
    command=[sys.executable,'-I',str(ROOT/'scripts/sync_control_uart_sdc.py')]
    if not render:command.append('--check')
    result=subprocess.run(command,capture_output=True,text=True)
    if result.returncode:raise SystemExit('UART SDC preparation failed: '+result.stderr.strip())
    return json.loads(result.stdout)
def verify_generated_itcm():
    report=runpy.run_path(str(ROOT/'scripts/verify_ae350_itcm.py'))['verify_project'](ROOT,'syn')
    if report['status']!='PASS':raise SystemExit('Actual generated AE350 ITCM is stale; regenerate the official IP')
    return report
def verify_synthesis():
    verify_uart_sdc()
    state=json.loads((ROOT/'logs/control_build/latest-syn.json').read_text());old=Path(state['run'])
    result=json.loads((old/'exit-status.json').read_text())
    if result['exit_code']!=0:raise SystemExit('Synthesis failed; do not start PnR')
    if inputs()!=json.loads((old/'inputs.json').read_text()):raise SystemExit('Synthesis input drift; do not start PnR')
    log=(old/'gowin.log').read_text(errors='replace')
    if 'GowinSynthesis finish' not in log or re.search(r'(?m)^\s*ERROR(?:\s|\()',log):
        raise SystemExit('Synthesis success evidence absent or error present')
    check=subprocess.run([sys.executable,'-I',str(ROOT/'scripts/run_gowin_synced.py'),'--check-results',str(ROOT/'ethereal_cloud_tone.gprj'),'--stage','syn'],capture_output=True,text=True)
    if check.returncode:raise SystemExit('Synthesis artifacts/configuration are stale: '+check.stderr.strip())
    return {'status':'PASS','run':str(old),'inputs_unchanged':True}
def worker(stage,run):
    log=(run/'gowin.log').open('w')
    launcher=os.environ.get('GOWIN_SH') or shutil.which('gw_sh') or str(Path.home()/'.local/bin/gw_sh')
    cmd=[launcher,str(ROOT/'scripts/run_control_project.tcl'),stage]
    p=subprocess.Popen(cmd,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    dump(run/'started.json',{'stage':stage,'build_pid':p.pid,'supervisor_pid':os.getpid(),'command':cmd,'log':str(run/'gowin.log'),'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
    rc=p.wait();log.close()
    dump(run/'exit-status.json',{'exit_code':rc,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
def viewer(run):
    dump(run/'viewer-started.json',{'pid':os.getpid()})
    position=0
    while True:
        f=run/'gowin.log'
        if f.exists():
            with f.open(errors='replace') as stream:
                stream.seek(position);text=stream.read();position=stream.tell()
                if text:print(text,end='',flush=True)
        done=run/'exit-status.json'
        if done.exists():print('\nStage exit:',done.read_text());input('Press Enter to close log viewer. ');return
        time.sleep(0.5)
def launch(stage):
    (ROOT/'logs/control_build').mkdir(parents=True,exist_ok=True)
    # Render only before synthesis freezes its inputs. PnR must not rewrite SDC.
    verify_uart_sdc(render=stage=='syn')
    verify_generated_itcm()
    if stage=='pnr':
        verify_synthesis()
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    run=ROOT/'logs/control_build'/(stage+'_'+stamp);run.mkdir(exist_ok=False)
    dump(run/'inputs.json',inputs())
    f=(run/'supervisor.log').open('w')
    p=subprocess.Popen([sys.executable,__file__,'_worker',stage,str(run)],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);f.close()
    deadline=time.monotonic()+8
    while not (run/'started.json').exists():
        if time.monotonic()>deadline:raise SystemExit('Missing stage startup handshake')
        time.sleep(0.05)
    state=json.loads((run/'started.json').read_text());state['run']=str(run)
    v=subprocess.run(['/usr/bin/gnome-terminal','--window','--title=Ethereal Cloud Tone '+stage,'--',sys.executable,__file__,'_viewer',str(run)],capture_output=True,text=True,timeout=10)
    state['terminal_launch_exit']=v.returncode
    if v.returncode:state['terminal_error']=v.stderr
    deadline=time.monotonic()+8
    while not (run/'viewer-started.json').exists() and time.monotonic()<deadline:time.sleep(0.05)
    if (run/'viewer-started.json').exists():state['viewer_pid']=json.loads((run/'viewer-started.json').read_text())['pid']
    dump(ROOT/'logs/control_build'/('latest-'+stage+'.json'),state);print(json.dumps(state,ensure_ascii=False))
    if not state.get('viewer_pid'):raise SystemExit('Build remains detached; log viewer did not start')
if __name__=='__main__':
    if sys.argv[1]=='verify':print(json.dumps(verify_synthesis()))
    elif sys.argv[1]=='launch' and sys.argv[2] in ('syn','pnr'):launch(sys.argv[2])
    elif sys.argv[1]=='_worker':worker(sys.argv[2],Path(sys.argv[3]))
    elif sys.argv[1]=='_viewer':viewer(Path(sys.argv[2]))
    else:raise SystemExit('Usage: manage_control_build.py launch syn|pnr, or verify')
