#!/usr/bin/env python3
"""Authorized FPGA SRAM + ESP32 app-only program, then receiver-first PCM recheck.

Requires the current accepted PnR fs, explicit review and ESP32 manifest. One
shared lock spans both writes and all captures. No FPGA Flash, bootloader,
partition/NVS/DDS storage or CH569 write. Import/--help access no devices.
"""
from pathlib import Path
import argparse, datetime, fcntl, hashlib, importlib.util, json, os
import re, shutil, signal, subprocess, sys, time

ROOT=Path(__file__).resolve().parents[2]
SHARED=Path('/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028')
PC=Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
ESP_PYTHON=Path('/home/ice/.espressif/python_env/idf6.1_py3.12_env/bin/python')
ESP_PORT='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_84:C7:BB:79:C4:C4-if00'
APP_OFFSET=0x10000
APP_CAPACITY=0x177000  # existing config/partitions.dds.csv factory partition
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
stamp=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
jst=lambda:datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).isoformat()

def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n');path.chmod(0o600)

def validate_build_lineage(build_root,source_root):
    """A copied build is usable only while both copies equal its frozen lineage."""
    build_root=Path(build_root).resolve();source_root=Path(source_root).resolve()
    if build_root==source_root:return {'status':'CURRENT_SOURCE_PROJECT','build_root':str(build_root)}
    path=build_root/'source-project-manifest.json';lineage=json.loads(path.read_text())
    assert lineage['status']=='HASH_VERIFIED_BUILD_SNAPSHOT'
    assert Path(lineage['source_root']).resolve()==source_root,'Snapshot source lineage mismatch'
    assert Path(lineage['build_root']).resolve()==build_root,'Snapshot build lineage mismatch'
    assert isinstance(lineage['files'],dict) and lineage['files'],'Empty build lineage'
    for name,digest in lineage['files'].items():
        item=Path(name)
        if item.is_absolute():
            assert item.name=='libgcc.a' and item.is_file(),'Unexpected external build input'
            assert sha(item)==digest,'External libgcc drift'
            continue
        assert '..' not in item.parts,'Build lineage escaped root'
        source=(source_root/item).resolve(strict=True);built=(build_root/item).resolve(strict=True)
        assert source.is_relative_to(source_root) and built.is_relative_to(build_root)
        assert source.is_file() and built.is_file()
        assert sha(source)==digest==sha(built),'Source/build snapshot drift: '+name
    return {'status':lineage['status'],'manifest':str(path),'SHA256':sha(path),'files':len(lineage['files'])}

def validate_pnr_binding(root,image,checksum,review,checked,source_root=None):
    """Pure guard: an independently hashed old fs is not the accepted artifact."""
    root=Path(root).resolve();image=Path(image).resolve(strict=True)
    source_root=Path(source_root).resolve() if source_root is not None else root
    lineage=validate_build_lineage(root,source_root)
    official=(root/'impl/pnr/ae350_control_lab.fs').resolve(strict=True)
    assert image==official,'Image is not the current official PnR fs'
    assert checked['stage']=='pnr' and Path(checked['project']).resolve()==root/'ae350_control_lab.gprj'
    artifact=checked['artifacts']['impl/pnr/ae350_control_lab.fs']
    assert artifact['sha256'].lower()==checksum.lower()==sha(image),'Accepted PnR image SHA256 differs'
    assert artifact['size']==image.stat().st_size>0,'Accepted PnR image size differs'
    assert review['status']=='REVIEWED_FOR_AUTHORIZED_SRAM_DEBUG'
    full_sta=review.get('full_STA_accepted',False)
    assert type(full_sta) is bool,'PnR review full_STA_accepted must be an explicit bool'
    assert Path(review['project']).resolve()==root
    if root!=source_root:assert Path(review['source_project']).resolve()==source_root,'Review source lineage mismatch'
    assert Path(review['image']).resolve()==official and review['image_SHA256'].lower()==checksum.lower()
    return {'image':str(official),'SHA256':sha(image),'bytes':image.stat().st_size,
            'artifact':'impl/pnr/ae350_control_lab.fs','completed_utc':checked.get('completed_utc'),'lineage':lineage,
            'full_STA_accepted':full_sta,'source_input_count':len(checked.get('inputs',{}))}

def validate_esp_manifest(path):
    """Validate exactly the supplied image and every declared source; no fallback."""
    path=Path(path).resolve(strict=True);manifest=json.loads(path.read_text())
    assert manifest['status']=='PASS' and manifest['chip']=='esp32s3'
    assert (manifest['flash_size'],manifest['flash_mode'],manifest['flash_freq'])==('8MB','dio','80m')
    assert type(manifest['build_fresh']) is bool,'build_fresh must state actual build history'
    provenance=manifest['build_provenance'];assert provenance,'Missing explicit build provenance'
    if isinstance(provenance,str):
        source=Path(provenance);assert source.is_absolute() and source.is_file()
    else:assert isinstance(provenance,dict),'Invalid build provenance'
    source_root=Path(manifest['source_root']);assert source_root.is_absolute() and source_root.is_dir()
    assert isinstance(manifest['inputs'],dict) and manifest['inputs'],'Missing source hashes'
    for name,digest in manifest['inputs'].items():
        source=Path(name);assert source.is_absolute() and source.is_file()
        assert isinstance(digest,str) and re.fullmatch('[0-9a-fA-F]{64}',digest)
        assert sha(source)==digest.lower(),'ESP32 source hash drift: '+name
    app=manifest['app'];image=Path(app['path'])
    assert image.is_absolute() and image.is_file()
    assert int(str(app['offset']),0)==APP_OFFSET,'Only the app partition may be written'
    assert type(app['bytes']) is int and 0<app['bytes']<=APP_CAPACITY
    assert app['bytes']==image.stat().st_size and sha(image)==app['SHA256'].lower(),'ESP32 image hash/size drift'
    return manifest

def wait_with_restoration(process,timeout,on_timeout=None):
    """SIGTERM lets the installed adapter stop its vendor and restore USB.

    Never SIGKILL its Python parent; retain the shared lease until wait returns.
    """
    def stop_and_wait():
        previous={s:signal.signal(s,signal.SIG_IGN)for s in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP)}
        try:
            try:process.terminate()
            except ProcessLookupError:pass
            return process.wait()
        finally:
            for s,handler in previous.items():signal.signal(s,handler)
    try:return process.wait(timeout=timeout),False
    except subprocess.TimeoutExpired:
        try:
            if on_timeout:on_timeout()
        finally:code=stop_and_wait()
        return code,True
    except BaseException:
        stop_and_wait()
        raise

def programmer_evidence(folder):
    folder=Path(folder);result=json.loads((folder/'result.json').read_text())
    candidates=list((folder/'usb-state/gowin-usb').glob('*/result.json'))
    assert len(candidates)==1,'Missing/ambiguous installed-adapter run'
    adapter=json.loads(candidates[0].read_text());vendor=candidates[0].with_name('vendor.log')
    text=vendor.read_text(errors='replace')
    users=re.findall(r'User Code is:\s*(0x[0-9A-Fa-f]+)',text)
    statuses=re.findall(r'Status Code is:\s*(0x[0-9A-Fa-f]+)',text)
    assert users and statuses,'This run has no UserCode/status readback'
    assert adapter['exit']==0 and adapter['broker_exit']==0,'Programmer/USB restoration failed'
    assert result['status']=='PROGRAMMED_SRAM' and 'Finished.' in text
    assert int(users[-1],16)==int(result['image']['expected_user'],16) and int(statuses[-1],16)!=0
    return {'result':result,'adapter_result':adapter,'read_user_code':users[-1],
            'device_status':statuses[-1],'vendor_log':str(vendor),'vendor_log_SHA256':sha(vendor)}

def redact_startup(text):
    sensitive=re.compile(r'password|passwd|\bpsk\b|token|secret|authorization|credential|\bssid\b',re.I)
    lines=text.splitlines();count=sum(bool(sensitive.search(line))for line in lines)
    return '\n'.join('[REDACTED sensitive startup line]' if sensitive.search(line) else line for line in lines)+'\n',count

def passive_startup(folder,duration=8):
    """Read fixed USB console with inactive DTR/RTS; no reset or TX bytes."""
    import serial
    record={'port':ESP_PORT,'UART_tx_bytes':0,'extra_reset':False,'duration_seconds':duration}
    received=bytearray();console=serial.Serial(port=None,baudrate=115200,timeout=.1)
    console.dtr=False;console.rts=False;console.port=ESP_PORT
    try:
        console.open();until=time.monotonic()+duration
        while time.monotonic()<until:received+=console.read(4096)
        text=received.decode('utf-8',errors='replace');clean,count=redact_startup(text)
        path=folder/'startup-redacted.log';path.write_text(clean);path.chmod(0o600)
        record.update(bytes=len(received),sensitive_lines_redacted=count,log=str(path),
                      panic_seen=bool(re.search(r'Guru Meditation|Backtrace:|panic\(|abort\(\)',text,re.I)))
    except Exception as exc:record.update(status='PASSIVE_READ_UNAVAILABLE',error=type(exc).__name__+': '+str(exc))
    finally:console.close();record['console_closed']=not console.is_open
    return record

def ledger_state(old,record,out,released=False):
    """Factual current-run ledger; the immutable before snapshot preserves history."""
    state=dict(old)
    state.update(updated_utc=stamp(),updated_jst=jst(),authorization=record['authorization'],run=str(out),
        previous_board_state=str(out/'board-state-before.json'),
        device_programming_lock=str(SHARED/'logs/board-programming.lock'),
        device_lock_state=('RELEASED_AFTER_'+record['status']) if released else 'HELD_FOR_UPSTREAM_PROGRAM_AND_PCM',
        FPGA_Flash_written=False,CH569_written=False,ESP32_written=record['ESP32_written'],
        full_STA_accepted=record.get('full_STA_accepted',False),source_inputs=record.get('source_input_count'),
        runtime={'status':record['status'],'error':record.get('error'),'validation':str(out/'session.json')})
    if record.get('FPGA_program_attempted'):
        evidence=record.get('program_evidence',{})
        state.update(phase='UPSTREAM_SUBTRACTIVE_LAB_'+record['status'],
            programmed=True if record['FPGA_SRAM_written'] else None,
            image_certainty='CONFIRMED_CURRENT_RUN_READBACK' if record['FPGA_SRAM_written'] else 'UNKNOWN_AFTER_DOWNLOAD_ATTEMPT',
            board_design='Independent upstream2409560 subtractive adapter',
            image=record.get('program_image'),source_image=record.get('image'),image_sha256=record.get('image_SHA256'),
            expected_user_code=evidence.get('result',{}).get('image',{}).get('expected_user'),
            read_user_code=evidence.get('read_user_code'),device_status=evidence.get('device_status'),
            operation='FPGA SRAM Program (2)',programming_run=str(out/'programmer'),
            programming_verification=str(out/'programmer/result.json'),
            program_exit=record.get('program_exit'),programmed_utc=evidence.get('result',{}).get('finished_utc'),
            programmed_jst=(datetime.datetime.fromisoformat(evidence['result']['finished_utc']).astimezone(
                datetime.timezone(datetime.timedelta(hours=9))).isoformat()) if evidence.get('result',{}).get('finished_utc') else None)
    if record.get('ESP32_program_attempted'):
        state['ESP32']={'status':'PROGRAMMED_APP_ONLY_HASH_VERIFIED' if record['ESP32_written'] else 'UNKNOWN_AFTER_APP_WRITE_ATTEMPT',
            'image':record.get('esp_app_snapshot'),'app_SHA256':record.get('esp_app_SHA256'),
            'app_only_offset':'0x10000','hash_verified':record.get('ESP32_hash_verified',False),
            'programming_run':str(out/'esp32'),'manifest':record.get('esp32_manifest'),
            'build_fresh':record.get('esp_build_fresh'),'previous_ESP32':old.get('ESP32')}
    return state

def wait_sub_mode(client,timeout=30):
    deadline=time.monotonic()+timeout;last_error=None
    while True:
        try:
            debug=client.status();state=client.request('GET','/api/v2/state');mode=state['mode']
            if (debug.get('ready') and not debug['web']['session'] and
                mode.get('requested')==0 and mode.get('applied')==0 and
                not mode.get('switching') and not mode.get('recovering') and
                not state['legacy'].get('rendering') and not any(state['legacy']['keys'])):
                return {'debug':debug,'state':state}
            last_error='Actual mode/owner not idle SUB0: '+json.dumps(mode)
        except Exception as exc:last_error=type(exc).__name__+': '+str(exc)
        if time.monotonic()>deadline:raise TimeoutError('ESP32 raw link/actual SUB0 not ready: '+str(last_error))
        time.sleep(.2)

def performance(capture,notes):
    """Receiver has valid preroll first; clear this same Web owner's old tails."""
    def play(output,record):
        c=capture.MidiDebugClient('http://192.168.4.1')
        try:
            capture.check_patch(c.request('GET','/api/v2/state'))
            routes=c.request('GET','/api/audio/routes');reverb=c.request('GET','/api/v2/effects/reverb')
            assert routes.get('valid') and routes.get('ready') and routes.get('pcm_b')==3,'PCM-B is not ready SUM'
            assert reverb.get('valid') and not reverb.get('pending') and not reverb.get('fault')
            assert reverb.get('wet_q15')==0 and reverb.get('damp_q15')==0,'Reverb differs from reference'
            c.open();c.send([0xb0,120,0]);c.report['cleanup_events']=list(c.report['events']);c.report['events']=[]
            save(output/'cleanup-cc120.json',{'scope':'same Web owner/source channel1, after valid PCM preroll',
                'events':c.report['cleanup_events'],'at_monotonic':time.monotonic()})
            c.wait_until(time.monotonic()+.15)
            events=[{'at_ms':50,'bytes':sum(([0x90,n,100]for n in notes),[])},
                    {'at_ms':500,'bytes':sum(([0x80,n,0]for n in notes),[])}]
            save(output/'events.json',events)
            record.update(notes=notes,velocity=100,planned_hold_ms=450,routes=routes,reverb=reverb,
                chord_dispatch='host-timed raw batches; no sample tags or 1ms stagger',midi_launch_monotonic=time.monotonic())
            start=time.monotonic();c.report['replay_start_ms']=(start-c.started)*1000
            c.report['short_gap_heartbeat_omitted']=True
            for event in events:
                time.sleep(max(0,start+event['at_ms']/1000-time.monotonic()))
                c.send(event['bytes'],planned_ms=event['at_ms'])
            c.wait_until(time.monotonic()+3)
            capture.check_patch(c.request('GET','/api/v2/state'));c.report['status']='PASS'
        except BaseException:
            if c.token:
                before=len(c.report['events'])
                try:c.send([0xb0,120,0])
                except Exception as exc:c.report['failure_stop_error']=str(exc)
                c.report['failure_stop_events']=c.report['events'][before:];c.report['events']=c.report['events'][:before]
            raise
        finally:save(output/'midi.json',c.report)
    return play

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run',required=True);ap.add_argument('--image',type=Path,required=True)
    ap.add_argument('--sha256',required=True);ap.add_argument('--pnr-review',type=Path,required=True)
    ap.add_argument('--esp32-manifest',type=Path,required=True)
    ap.add_argument('--build-root',type=Path,default=ROOT,help='Optional hash-bound isolated build snapshot; source/output stay in this project')
    args=ap.parse_args();assert re.fullmatch('[A-Za-z0-9_-]+',args.run)
    build_root=args.build_root.resolve(strict=True)
    out=ROOT/'sim/results/upstream_pcm_recheck'/args.run;out.mkdir(parents=True,exist_ok=False)
    r={'status':'PREPARING','started_utc':stamp(),
       'authorization':'用户2026-11-01明确授权PnR完成后重烧FPGA与ESP32并再次采集PCM对比仿真',
       'FPGA_SRAM_written':False,'FPGA_Flash_written':False,'ESP32_written':False,
       'CH569_written':False,'scenarios':{},'output':str(out),'source_project':str(ROOT),'build_root':str(build_root)}
    lock=None;acquired=False;old=None
    ledger=SHARED/'sim/results/ae350_programming_20261030/board-state.json'
    def report():save(out/'session.json',r)
    def publish(released=False):
        if acquired and old is not None:
            new=ledger_state(old,r,out,released);save(out/'board-state-after.json',new);save(ledger,new)
    def current_pnr():
        if r.get('pnr_review_SHA256'):
            assert sha(args.pnr_review)==r['pnr_review_SHA256'],'PnR review changed during session'
        validate_build_lineage(build_root,ROOT)
        checked=subprocess.run([sys.executable,'-I',str(build_root/'scripts/run_gowin_synced.py'),
            '--check-results',str(build_root/'ae350_control_lab.gprj'),'--stage','pnr'],cwd=build_root,capture_output=True,text=True)
        (out/'pnr-result-check.json').write_text(checked.stdout);(out/'pnr-result-check.stderr').write_text(checked.stderr)
        assert checked.returncode==0,'Official PnR/input/ITCM check failed'
        return validate_pnr_binding(build_root,args.image,args.sha256,json.loads(args.pnr_review.read_text()),json.loads(checked.stdout),ROOT)
    def timeout_record(stage):
        r.update(status='WAITING_'+stage+'_RESTORATION',timeout_stage=stage);report();publish()
    report()
    def interrupted(signum,frame):raise InterruptedError('Session interrupted by signal '+str(signum))
    handlers={s:signal.signal(s,interrupted)for s in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP)}
    try:
        binding=current_pnr();esp=validate_esp_manifest(args.esp32_manifest)
        assert Path(esp['source_root']).resolve()==ROOT/'control_migration/esp32/esp32s3','ESP32 manifest source is not this project S3'
        r.update(pnr_review=str(args.pnr_review.resolve()),pnr_review_SHA256=sha(args.pnr_review),
            image=binding['image'],image_SHA256=binding['SHA256'],pnr_binding=binding,
            full_STA_accepted=binding['full_STA_accepted'],source_input_count=binding['source_input_count'],
            esp32_manifest=str(args.esp32_manifest.resolve()),esp32_manifest_SHA256=sha(args.esp32_manifest),
            esp_build_fresh=esp['build_fresh'],esp_build_provenance=esp['build_provenance'])
        lock=os.open(SHARED/'logs/board-programming.lock',os.O_RDONLY|os.O_NOFOLLOW)
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);acquired=True
        r['shared_lock_acquired_utc']=stamp();old=json.loads(ledger.read_text());save(out/'board-state-before.json',old)
        current_pnr();assert sha(args.esp32_manifest)==r['esp32_manifest_SHA256'];validate_esp_manifest(args.esp32_manifest)
        image=out/'reviewed-image.fs';shutil.copy2(args.image,image);assert sha(image)==args.sha256.lower()
        esp_out=out/'esp32';esp_out.mkdir();app=esp_out/'reviewed-app.bin';shutil.copy2(esp['app']['path'],app)
        assert sha(app)==esp['app']['SHA256'].lower()
        r.update(status='PROGRAMMING_FPGA',program_image=str(image),esp_app_snapshot=str(app),esp_app_SHA256=sha(app))
        report();publish()
        cmd=[sys.executable,'-B',str(ROOT/'scripts/program_fpga_sram.py'),'--image',str(image),
             '--sha256',args.sha256,'--log-dir',str(out/'programmer')]
        r['program_command']=cmd;r['FPGA_program_attempted']=True;report();publish()
        with(out/'programmer-supervisor.log').open('w')as log:
            p=subprocess.Popen(cmd,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
            r['program_pid']=p.pid;report()
            rc,timed_out=wait_with_restoration(p,180,lambda:timeout_record('PROGRAMMER_USB'))
        r['program_exit']=rc;r['program_timed_out']=timed_out
        assert not timed_out and rc==0,'Inspect original Programmer logs; no automatic retry'
        r['program_evidence']=programmer_evidence(out/'programmer')
        r.update(FPGA_SRAM_written=True,status='PROGRAMMING_ESP32');report();publish()
        current_pnr();assert sha(args.esp32_manifest)==r['esp32_manifest_SHA256'];validate_esp_manifest(args.esp32_manifest)
        assert sha(app)==r['esp_app_SHA256'],'ESP32 frozen image changed before write'
        cmd=[str(ESP_PYTHON),'-m','esptool','--chip','esp32s3','--port',ESP_PORT,'--baud','460800',
             '--before','default-reset','--after','hard-reset','write-flash','--flash-mode','dio',
             '--flash-size','8MB','--flash-freq','80m','0x10000',str(app)]
        r.update(ESP32_program_attempted=True,esp32_program_command=cmd);report();publish()
        with(esp_out/'flash.log').open('w')as log:
            p=subprocess.Popen(cmd,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
            r['esp32_program_pid']=p.pid;report()
            rc,timed_out=wait_with_restoration(p,180,lambda:timeout_record('ESP32_SERIAL'))
        r.update(esp32_program_exit=rc,esp32_program_timed_out=timed_out)
        assert not timed_out and rc==0 and 'Hash of data verified.' in (esp_out/'flash.log').read_text(),'ESP32 write/hash failed; no retry'
        r.update(ESP32_written=True,ESP32_hash_verified=True,status='WAITING_LINK');report();publish()
        serial_code='import importlib.util; s=importlib.util.spec_from_file_location("runner",'+repr(str(Path(__file__).resolve()))+');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);r=m.passive_startup(m.Path('+repr(str(esp_out))+'));m.save(m.Path('+repr(str(esp_out/'startup.json'))+'),r)'
        with(esp_out/'startup-supervisor.log').open('w')as log:
            p=subprocess.Popen([str(ESP_PYTHON),'-B','-c',serial_code],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
            startup_exit,startup_timeout=wait_with_restoration(p,20,lambda:timeout_record('PASSIVE_SERIAL'))
        assert startup_exit==0 and not startup_timeout,'Passive ESP32 console helper failed'
        startup=json.loads((esp_out/'startup.json').read_text());r['passive_startup']=startup
        assert not startup.get('panic_seen'),'ESP32 startup panic observed'
        spec=importlib.util.spec_from_file_location('upstream_proven_capture',ROOT/'sim/config/capture_sub_supersaw_reset_20261101.py')
        capture=importlib.util.module_from_spec(spec);spec.loader.exec_module(capture)
        protected=list((PC/'pcm_studio').glob('*.py'))+[capture.S3/'tools/midi_debug.py',ROOT/'sim/config/capture_sub_supersaw_reset_20261101.py']
        hashes={str(p):sha(p)for p in protected};save(out/'tool-hashes-before.json',hashes)
        client=capture.MidiDebugClient('http://192.168.4.1');save(out/'link-ready.json',wait_sub_mode(client))
        try:capture.receive(out/'transport_settle',lambda *_:None);r['transport_settle']='PASS'
        except Exception as exc:r['transport_settle']={'status':'REJECTED_NO_NOTE_PREROLL','error':str(exc)}
        report();capture.receive(out/'parameter_alignment',capture.prepare);save(out/'expected-patch.json',capture.EXPECTED)
        r.update(status='CAPTURING',control_transport='Existing ESP32 HTTP raw MIDI owner, not serial NoteOn injection',
            event_timing='Host50/500ms batches; no native sample tag or exact 1ms stagger')
        report();publish()
        jobs=[('single_c4',[60]),('chord_ceg',[60,64,67]),('poly32_requested',list(range(48,80)))]
        jobs += [('single_c4_repeat'+str(i),[60])for i in range(1,6)]
        for name,notes in jobs:
            capture.receive(out/name,performance(capture,notes))
            r['scenarios'][name]={'status':'PASS_CAPTURE_AND_CONTROL','notes':notes,'velocity':100};report()
        assert all(sha(Path(p))==h for p,h in hashes.items()),'Production capture/control tools changed'
        current_pnr();r.update(status='CAPTURED_PENDING_OFFLINE_COMPARISON',production_tools_unchanged=True,
            capture_boundary='PCM-B finalSUM; original WAV/RAW; no CRC/native indices supplied by v1',
            reference=str(ROOT/'sim/results/upstream_sub_supersaw_final_20261101'))
    except BaseException as exc:
        r.update(status='FAIL',error=type(exc).__name__+': '+str(exc));raise
    finally:
        for s in handlers:signal.signal(s,signal.SIG_IGN)
        r['finished_utc']=stamp()
        try:publish(released=True)
        except Exception as exc:r['ledger_update_error']=type(exc).__name__+': '+str(exc)
        finally:
            if acquired:
                fcntl.flock(lock,fcntl.LOCK_UN);r['shared_lock_released_utc']=stamp()
            if lock is not None:os.close(lock)
            report()
            for s,handler in handlers.items():signal.signal(s,handler)
        print(json.dumps({k:r.get(k)for k in ['status','FPGA_SRAM_written','ESP32_written','error','output']},ensure_ascii=False),flush=True)
    return 0
if __name__=='__main__':raise SystemExit(main())
