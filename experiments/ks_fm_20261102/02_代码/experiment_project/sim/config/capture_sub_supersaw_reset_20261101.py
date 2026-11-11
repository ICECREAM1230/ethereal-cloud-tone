#!/usr/bin/env python3
"""Task-only PCM receiver plus existing HTTP parameter/MIDI interfaces.

No firmware, bitstream, storage, or production-tool writes. Factory LOAD is
the existing RAM patch switch (includes the owner's normal all-note closure).
Every NoteOn follows valid PCM preroll; all raw and host-time evidence retained.
"""
from pathlib import Path
import fcntl, hashlib, json, os, sys, time
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[2]
PC = Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
S3 = ROOT / 'control_migration/esp32/esp32s3'
sys.path[:0] = [str(PC), str(S3 / 'tools')]
from pcm_studio.session import CaptureSession
from pcm_studio.sources import UsbSource
from midi_debug import MidiDebugClient

EXPECTED = dict(voice=1, osc=[[0,0,0,0],[3,-12,0,0]], mix=0, detune=6,
                spread=127, volume=207, pan=0, filter=[0,512,0,1,64],
                cutoff_offset=0, resonance_offset=0, amp=[152,32,240,40],
                mod=[152,32,240,40], mod_depth=2048,
                lfo=[[2,350,16,0],[2,350,0,0]], velocity=[64,64])

def save(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n')
    path.chmod(0o600)  # Raw API evidence can contain ephemeral session tokens.

def check_patch(state):
    actual = state['legacy']
    errors = {k: {'expected': v, 'actual': actual.get(k)}
              for k, v in EXPECTED.items() if actual.get(k) != v}
    if errors:
        raise RuntimeError('Patch differs from simulation: ' + json.dumps(errors))
    if (state['mode']['applied'] != 0 or state['mode']['switching'] or
            actual['rendering'] or any(actual['keys'])):
        raise RuntimeError('Not idle in the SUB mode')

class StartIfIdle(UsbSource):
    def __init__(self):
        super().__init__()
        self.attempted = False
        self.start_events = []

    def read(self, stop):
        data = super().read(stop)
        if not data and not stop.is_set() and not self.attempted:
            self.attempted = True
            at = time.monotonic()
            result = self.device.ctrl_transfer(0x40, 0x8f, 0x007f, 0, None, timeout=1000)
            self.start_events.append(dict(at_monotonic=at, request=[64,143,127,0], result=int(result)))
            self.usb_info.update(read_only=False, start_request_sent=True)
        return data

def receive(output, action):
    output.mkdir()
    session, source = CaptureSession(), StartIfIdle()
    record = {'capture_started_monotonic': time.monotonic(), 'sample_accurate': False}
    try:
        session.start(lambda: source, output / 'capture', duration=8)
        deadline = time.monotonic() + 4
        while session.stats().get('sample_frames', 0) < 24000:
            if not session.running:
                raise RuntimeError('Receiver stopped before MIDI: ' + str(session.stats()))
            if time.monotonic() > deadline:
                raise TimeoutError('No valid PCM preroll; no notes sent')
            time.sleep(.02)
        record['ready_preroll'] = session.stats()
        record['action_started_monotonic'] = time.monotonic()
        action(output, record)
        while not session.wait(.2):
            pass
        record['capture_stats'] = session.stats()
        if record['capture_stats']['status'] != 'PASS':
            raise RuntimeError('Capture did not pass')
    except Exception as exc:
        record['error'] = str(exc)
        raise
    finally:
        session.stop()
        session.wait()
        record.update(START_events=source.start_events, source_released=source.device is None,
                      final_capture_stats=session.stats())
        save(output / 'run.json', record)
    print(json.dumps({'scenario': output.name, 'status': record['capture_stats']['status'],
                      'pairs': record['capture_stats']['sample_frames'],
                      'channels': record['capture_stats']['channels']}, ensure_ascii=False), flush=True)

def prepare(output, record):
    c = MidiDebugClient('http://192.168.4.1')
    try:
        state = c.request('GET', '/api/v2/state')
        debug = c.status()
        if any(state['legacy']['keys']) or debug['web']['session'] or not debug['ready']:
            raise RuntimeError('Existing performance owner busy; patch not changed')
        record['before'] = state
        # Immutable factory preset is read from the existing firmware, then
        # switched in RAM. No DDS write/save/default operation is requested.
        receipt = c.request('POST', '/api/dds/presets?' + urlencode({
            'op': 'load', 'path': 'Patches/Factory/Pads/Supersaw.toml'}))
        record['factory_load_receipt'] = receipt
        deadline = time.monotonic() + 3
        while True:
            status = c.request('GET', '/api/dds/status?id=' + str(receipt['job_id']))
            if status['phase'] == 'done':
                break
            if status['phase'] == 'failed' or time.monotonic() >= deadline:
                raise RuntimeError('RAM factory load did not complete: ' + str(status))
            time.sleep(.04)
        record['factory_load_completed'] = status
        # Patch job completion acknowledges the owner; RAW/template readiness
        # can follow it. Wait for that existing readiness before session open.
        deadline = time.monotonic() + 3
        while True:
            debug = c.status()
            if debug['ready'] and not debug['web']['session']:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('MIDI owner did not become ready after patch switch')
            time.sleep(.04)
        c.open()
        for cc in (75, 103):
            c.sequence += 1
            body = f'{c.token} {c.sequence} {c.context} 2 {cc} 111 0'.encode()
            c.request('POST', '/api/v2/command', body, 'text/plain')
            c.last_lease = time.monotonic()
            c._await_owner(c.sequence)
        # Match the simulation's performance expression and VM gain/pan.
        c.send([0xb0,7,127, 0xb0,10,64, 0xb0,1,0, 0xe0,0,64, 0xd0,0])
        states = []
        for _ in range(3):
            c.wait_until(time.monotonic() + .3)
            state = c.request('GET', '/api/v2/state')
            check_patch(state)
            states.append(state)
        record['matching_state_checks'] = states
        save(output / 'verified-state.json', states[-1])
        save(output / 'expected-patch.json', EXPECTED)
        c.report['status'] = 'PASS'
    finally:
        save(output / 'control.json', c.report)

def performance(notes, *, short_gap_without_heartbeat=True):
    def play(output, record):
        c = MidiDebugClient('http://192.168.4.1')
        try:
            check_patch(c.request('GET', '/api/v2/state'))
            routes = c.request('GET', '/api/audio/routes')
            reverb = c.request('GET', '/api/v2/effects/reverb')
            record.update(routes=routes, reverb=reverb)
            if not routes.get('valid') or routes.get('pcm_b') != 3:
                raise RuntimeError('PCM-B is not the valid SUM route')
            if not reverb.get('valid') or reverb.get('wet_q15') != 0:
                raise RuntimeError('Reverb does not match simulation')
            c.open()
            events = [dict(at_ms=50, bytes=sum(([0x90,n,100] for n in notes), [])),
                      dict(at_ms=500, bytes=sum(([0x80,n,0] for n in notes), []))]
            save(output / 'events.json', events)
            record.update(notes=notes, velocity=100, planned_hold_ms=450,
                          chord_dispatch='single raw MIDI batch, 3 bytes per note; no exact 1ms staggering',
                          midi_launch_monotonic=time.monotonic())
            if short_gap_without_heartbeat:
                # The 450ms On/Off gap is below the existing 1200ms lease.
                # A heartbeat immediately before Off can add several HTTP
                # round-trips. Keep the original send/ACK checks and schedule
                # this short pair directly; renew normally after NoteOff.
                start = time.monotonic()
                c.report['replay_start_ms'] = (start-c.started)*1000
                c.report['short_gap_heartbeat_omitted'] = True
                for event in events:
                    time.sleep(max(0,start+event['at_ms']/1000-time.monotonic()))
                    c.send(event['bytes'],planned_ms=event['at_ms'])
            else:
                c.replay(events)
            c.wait_until(time.monotonic() + 3)
            check_patch(c.request('GET', '/api/v2/state'))
            c.report['status'] = 'PASS'
        finally:
            save(output / 'midi.json', c.report)
    return play

def main():
    out = Path(sys.argv[1]).resolve()
    lock = os.open(ROOT / 'logs/board-programming.lock', os.O_RDONLY | os.O_NOFOLLOW)
    summary = {'scenarios': {}, 'parameter_comparison': 'exact exposed patch fields; native readback not available'}
    protected = list((PC / 'pcm_studio').glob('*.py')) + [S3 / 'tools/midi_debug.py']
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    save(out / 'tool-hashes-before.json', before)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        receive(out / 'parameter_alignment', prepare)
        for name, notes in [('single_c4',[60]), ('chord_ceg',[60,64,67]),
                            ('poly32_requested',list(range(48,80))), ('single_c4_repeat',[60])]:
            receive(out / name, performance(notes))
            summary['scenarios'][name] = 'PASS'
            save(out / 'capture-summary.json', summary)
        summary['status'] = 'PASS'
    except Exception as exc:
        summary.update(status='FAIL', error=str(exc))
        raise
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)
        summary['board_lock_released'] = True
        summary['production_tools_unchanged'] = all(
            hashlib.sha256(Path(p).read_bytes()).hexdigest() == h for p,h in before.items())
        save(out / 'capture-summary.json', summary)

if __name__ == '__main__':
    main()
