#!/usr/bin/env python3
"""Receiver-first KS/FM measurements on the existing channel-routed image.

No firmware, reset or persistent patch changes. Raw MIDI explicitly carries
channel1 for KS or channel2 for FM (zero based); op5 alone does not rewrite it.
"""
from pathlib import Path
import argparse
import fcntl
import importlib.util
import json
import os
import re
import signal
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
SHARED = Path('/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028')


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    sys.modules[name] = obj
    spec.loader.exec_module(obj)
    return obj


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run', required=True)
    args = ap.parse_args()
    assert re.fullmatch('[A-Za-z0-9_-]+', args.run)
    runner = module('ksfm_runner', ROOT/'sim/config/run_upstream_pcm_recheck_board.py')
    capture = module('ksfm_capture', ROOT/'sim/config/capture_sub_supersaw_reset_20261101.py')
    out = ROOT/'sim/results/ks_fm_pcm_check_20261102'/args.run
    out.mkdir(parents=True, exist_ok=False)
    record = {'status': 'PREPARING', 'started_utc': runner.stamp(), 'output': str(out),
              'firmware_written': False, 'device_reset': False, 'scenarios': {},
              'velocity': 100, 'planned_hold_ms': 450,
              'KS': 'Legacy KS; ENABLE_KS_TIMBRES=0; existing decay retained',
              'FM': 'Existing CPU boot/default, algorithm7, one keyed operator; not a multi-operator patch test',
              'reference_scope': 'No matching KS/FM full WAV reference; board measurements only'}
    fd = os.open(SHARED/'logs/board-programming.lock', os.O_RDONLY | os.O_NOFOLLOW)
    acquired = False

    def cpu_ok(state):
        cpu = state['cpu_mode']
        assert cpu['valid'] and cpu['channel_routes'] and cpu['ready'] and not cpu['busy'] and not cpu['fault']

    def select(client, channel):
        client.sequence += 1
        body = f'{client.token} {client.sequence} {client.context} 5 {channel} 0 0'.encode()
        client.request('POST', '/api/v2/command', body, 'text/plain')
        client.last_lease = time.monotonic()
        client._await_owner(client.sequence)
        state = client.request('GET', '/api/v2/state')
        cpu_ok(state)
        assert state['clients'][client.client]['midi_channel'] == channel
        return state

    def performance(engine, channel, notes):
        def play(folder, take):
            client = capture.MidiDebugClient('http://192.168.4.1')
            note_on_sent = False
            note_off_sent = False
            try:
                state = client.request('GET', '/api/v2/state');cpu_ok(state)
                routes = client.request('GET', '/api/audio/routes')
                reverb = client.request('GET', '/api/v2/effects/reverb')
                assert routes['valid'] and routes['ready'] and routes['pcm_b'] == 3
                assert reverb['valid'] and not reverb['pending'] and not reverb['fault']
                assert reverb['wet_q15'] == reverb['damp_q15'] == 0
                client.open()
                selected = select(client, channel)
                cc, bend = 0xb0 | channel, 0xe0 | channel
                client.send([cc, 120, 0])
                client.send([cc, 7, 127, cc, 10, 64, cc, 11, 127,
                             cc, 1, 0, cc, 64, 0, bend, 0, 64])
                client.report['setup_events'] = client.report['events'];client.report['events'] = []
                client.wait_until(time.monotonic()+.15)
                assert time.monotonic()-take['capture_started_monotonic'] < 5, 'Control setup exceeded receiver budget'
                events = [{'at_ms': 50, 'bytes': sum(([0x90 | channel, n, 100] for n in notes), [])},
                          {'at_ms': 500, 'bytes': sum(([0x80 | channel, n, 0] for n in notes), [])}]
                runner.save(folder/'events.json', events)
                runner.save(folder/'selected-state.json', selected)
                take.update(engine=engine, MIDI_channel_zero_based=channel, notes=notes, velocity=100,
                            planned_hold_ms=450, routes=routes, reverb=reverb,
                            midi_launch_monotonic=time.monotonic(),
                            ks_decay_ms=selected['legacy']['ks_decay_ms'])
                start = time.monotonic()
                client.report['replay_start_ms'] = (start-client.started)*1000
                for i, event in enumerate(events):
                    time.sleep(max(0, start+event['at_ms']/1000-time.monotonic()))
                    client.send(event['bytes'], planned_ms=event['at_ms'])
                    if i == 0:note_on_sent = True
                    else:note_off_sent = True
                # Renew only after Off. Keep the same raw owner during the
                # measured release, without additional musical events.
                client.wait_until(min(start+5.5, take['capture_started_monotonic']+7.6))
                take['state_after'] = client.request('GET', '/api/v2/state')
                cpu_ok(take['state_after'])
                take['fdc_after'] = client.request('GET', '/api/v2/fdc')
                # Restore only this client's UI metadata to channel0; raw
                # music has already been stopped on its explicit channel.
                select(client, 0)
                client.report['status'] = 'PASS'
            except BaseException:
                if client.token:
                    try:
                        client.send([0xb0 | channel, 120, 0])
                    except Exception as exc:
                        client.report['failure_stop_error'] = str(exc)
                raise
            finally:
                take.update(note_on_sent=note_on_sent, note_off_sent=note_off_sent)
                runner.save(folder/'midi.json', client.report)
        return play

    def interrupted(signum, frame):
        raise InterruptedError('KS/FM measurement interrupted '+str(signum))

    handlers = {s: signal.signal(s, interrupted) for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB);acquired = True
        ledger = json.loads((SHARED/'sim/results/ae350_programming_20261030/board-state.json').read_text())
        record.update(board_image_SHA256=ledger['image_sha256'], board_user_code=ledger['read_user_code'])
        client = capture.MidiDebugClient('http://192.168.4.1')
        record['initial_state'] = client.request('GET', '/api/v2/state');cpu_ok(record['initial_state'])
        raw = client.status();assert raw['ready'] and not raw['web']['session']
        assert not any(record['initial_state']['legacy']['keys'])
        record['fdc_before'] = client.request('GET', '/api/v2/fdc')
        assert not record['fdc_before']['requested_enable'] and not record['fdc_before']['driver_enabled']
        try:record['ks_timbre'] = client.request('GET', '/api/v2/ks/timbre')
        except Exception as exc:record['ks_timbre'] = {'unavailable': str(exc)}
        protected = list((runner.PC/'pcm_studio').glob('*.py')) + [capture.S3/'tools/midi_debug.py',
                    ROOT/'sim/config/capture_sub_supersaw_reset_20261101.py']
        hashes = {str(p): runner.sha(p) for p in protected}
        runner.save(out/'tool-hashes-before.json', hashes)
        record['status'] = 'CAPTURING';runner.save(out/'session.json', record)
        for engine, channel in [('ks', 1), ('fm', 2)]:
            jobs = [('single_c4', [60]), ('chord_ceg', [60, 64, 67]),
                    ('single_c4_repeat1', [60]), ('single_c4_repeat2', [60])]
            for name, notes in jobs:
                name = engine+'_'+name
                capture.receive(out/name, performance(engine, channel, notes))
                record['scenarios'][name] = {'status': 'PASS_CAPTURE_AND_CONTROL', 'engine': engine,
                                            'MIDI_channel_zero_based': channel, 'notes': notes}
                runner.save(out/'session.json', record)
        assert all(runner.sha(Path(p)) == value for p, value in hashes.items()), 'Production tools changed'
        record.update(status='CAPTURED_PENDING_ANALYSIS', production_tools_unchanged=True)
    except BaseException as exc:
        record.update(status='FAIL', error=type(exc).__name__+': '+str(exc));raise
    finally:
        if acquired:fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)
        record.update(finished_utc=runner.stamp(), shared_lock_released=acquired)
        runner.save(out/'session.json', record)
        for s, handler in handlers.items():signal.signal(s, handler)
        print(json.dumps({k: record.get(k) for k in ['status', 'output', 'error']}, ensure_ascii=False), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
