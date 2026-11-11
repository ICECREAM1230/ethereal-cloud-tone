import copy
import csv
import io
import tempfile
import threading
import unittest

from host.b_board_debugger.config import DEFAULT_CONFIG
from host.b_board_debugger.controller import Controller
from host.b_board_debugger.events import EventLog
from host.b_board_debugger.transport import Inbox


class ManualWorker:
    def __init__(self, epoch, inbox, port, **kwargs):
        self.epoch, self.inbox = epoch, inbox
        self.lines = []
        self.stopped = False
        self.bad_frames = 0

    def start(self):
        self.inbox.put(self.epoch, 'opened', {})

    def send(self, data):
        self.lines.append(data.decode('ascii').strip())
        return True

    def stop(self):
        self.stopped = True

    def reply(self, config=None, revision=7, ok=True, code='ok', **extra):
        request_id = int(self.lines[-1].split()[1])
        frame = dict(v=1, type='reply', id=request_id, ok=ok, code=code, revision=revision)
        if config is not None:
            frame['config'] = config
        frame.update(extra)
        self.inbox.put(self.epoch, 'frame', frame)


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.clock = [10.0]
        self.controller = Controller(worker_factory=ManualWorker, now=lambda: self.clock[0])
        self.controller.connect('test-only')
        self.worker = self.controller.worker
        self.controller.poll()

    def bootstrap(self):
        self.assertTrue(self.worker.lines[-1].endswith(' -1 ping'))
        self.worker.reply(copy.deepcopy(DEFAULT_CONFIG))
        self.controller.poll()
        self.assertTrue(self.worker.lines[-1].endswith(' -1 get'))
        self.worker.reply(copy.deepcopy(DEFAULT_CONFIG))
        self.controller.poll()
        self.assertTrue(self.worker.lines[-1].endswith(' -1 status'))
        self.worker.reply(state={'uptime_ms': 100, 'revision': 7, 'events': []})
        self.controller.poll()
        self.assertTrue(self.worker.lines[-1].endswith(' -1 stream 200'))
        self.worker.reply()
        self.controller.poll()
        self.assertTrue(self.controller.ready)

    def test_connect_is_read_only_and_apply_is_only_active_after_ack(self):
        self.bootstrap()
        self.assertFalse(any(' apply ' in x or x.endswith(' save') for x in self.worker.lines))
        self.controller.edit({'volume': 88})
        self.controller.apply()
        self.assertTrue(self.controller.dirty)
        self.assertEqual(self.controller.config['volume'], 100)
        self.assertTrue(self.worker.lines[-1].endswith(' 7 apply volume=88'))
        updated = copy.deepcopy(DEFAULT_CONFIG)
        updated['volume'] = 88
        self.worker.reply(updated, revision=8)
        self.controller.poll()
        self.assertEqual(self.controller.config['volume'], 88)
        self.assertFalse(self.controller.dirty)

    def test_telemetry_and_get_never_overwrite_user_draft(self):
        self.bootstrap()
        self.controller.edit({'volume': 88})
        self.worker.inbox.put(self.worker.epoch, 'frame',
                              dict(v=1, type='state', revision=8, uptime_ms=200,
                                   controls={'volume': 70}, events=[]))
        self.controller.poll()
        self.assertTrue(self.worker.lines[-1].endswith(' -1 get'))
        remote = copy.deepcopy(DEFAULT_CONFIG)
        remote['volume'] = 70
        self.worker.reply(remote, revision=8)
        self.controller.poll()
        self.assertEqual(self.controller.draft['volume'], 88)
        self.assertEqual(self.controller.config['volume'], 70)
        self.assertEqual(self.controller.draft_revision, 7)
        self.controller.apply()
        self.assertIn(' 7 apply ', self.worker.lines[-1])
        self.worker.reply(remote, revision=8, ok=False, code='revision_conflict')
        self.controller.poll()
        self.assertTrue(self.controller.conflict)
        self.assertEqual(self.controller.draft['volume'], 88)
        self.controller.rebase_draft()
        self.assertEqual(self.controller.draft_revision, 8)
        self.assertEqual(self.controller.draft['volume'], 88)

    def test_write_timeout_reads_back_and_never_retries_write_or_accepts_late_ack(self):
        self.bootstrap()
        self.controller.edit({'gate': 300})
        self.controller.apply()
        timed_id = int(self.worker.lines[-1].split()[1])
        self.clock[0] += 20
        self.controller.poll()
        self.assertEqual(sum(' apply ' in line for line in self.worker.lines), 1)
        self.assertTrue(self.worker.lines[-1].endswith(' -1 get'))
        self.assertTrue(self.controller.uncertain)
        self.worker.inbox.put(self.worker.epoch, 'frame', dict(v=1, type='reply', id=timed_id,
                             ok=True, code='ok', revision=99, config=DEFAULT_CONFIG))
        self.controller.poll()
        self.assertEqual(self.controller.revision, 7)
        self.assertTrue(self.controller.dirty)

    def test_old_session_callbacks_and_wrong_reply_ids_cannot_update_new_session(self):
        self.bootstrap()
        old = self.worker
        self.controller.disconnect()
        self.controller.connect('new-test-only')
        self.controller.poll()
        self.worker = self.controller.worker
        self.assertTrue(old.stopped)
        old.reply(DEFAULT_CONFIG, revision=123)
        self.worker.inbox.put(self.worker.epoch, 'frame', dict(v=1, type='reply', id=99999,
                              ok=True, code='ok', revision=101, config=DEFAULT_CONFIG))
        self.controller.poll()
        self.assertIsNone(self.controller.revision)
        self.assertFalse(self.controller.ready)

    def test_edits_during_apply_remain_draft_after_ack_and_import_does_not_write(self):
        self.bootstrap()
        self.controller.edit({'gate': 300})
        self.controller.apply()
        self.controller.edit({'gate': 400})
        updated = copy.deepcopy(DEFAULT_CONFIG)
        updated['gate'] = 300
        self.worker.reply(updated, revision=8)
        self.controller.poll()
        self.assertEqual(self.controller.draft['gate'], 400)
        self.assertEqual(self.controller.config['gate'], 300)
        self.assertTrue(self.controller.dirty)
        before = len(self.worker.lines)
        self.controller.import_draft(DEFAULT_CONFIG)
        self.assertEqual(len(self.worker.lines), before)

    def test_malformed_handshake_does_not_leave_connecting_forever(self):
        self.worker.reply()  # ping is missing its required full config
        self.controller.poll()
        self.assertFalse(self.controller.ready)
        self.assertFalse(self.controller.connecting)
        self.assertIn('握手', self.controller.message)

    def test_invalid_apply_ack_is_uncertain_and_reads_back_without_changing_active(self):
        self.bootstrap()
        self.controller.edit({'volume': 82})
        self.controller.apply()
        self.worker.reply({'volume': 82}, revision=8)
        self.controller.poll()
        self.assertEqual(self.controller.config['volume'], 100)
        self.assertTrue(self.controller.uncertain)
        self.assertTrue(self.controller.dirty)
        self.assertTrue(self.worker.lines[-1].endswith(' -1 get'))

    def test_invalid_editor_text_holds_the_entire_draft_baseline_during_remote_readback(self):
        self.bootstrap()
        self.controller.hold_draft()
        self.controller.read_config()
        updated = copy.deepcopy(DEFAULT_CONFIG)
        updated['volume'] = 70
        self.worker.reply(updated, revision=8)
        self.controller.poll()
        self.assertEqual(self.controller.config['volume'], 70)
        self.assertEqual(self.controller.draft['volume'], 100)
        self.assertEqual(self.controller.draft_revision, 7)
        self.controller.edit({'volume': 90})
        self.controller.apply()
        self.assertIn(' 7 apply volume=90', self.worker.lines[-1])

    def test_mcu_reboot_keeps_uart_but_invalidates_old_ack_revision_and_event_session(self):
        self.bootstrap()
        old_epoch = self.controller.epoch
        self.worker.inbox.put(old_epoch, 'frame', dict(v=1, type='state', uptime_ms=200,
            revision=7, events=[[20, 150, 1, 20, 16, 60, False, 7]]))
        self.controller.poll()
        # A later snapshot has no events, so the event log alone cannot detect reboot.
        self.worker.inbox.put(old_epoch, 'frame', dict(v=1, type='state', uptime_ms=5000,
            revision=7, events=[]))
        self.controller.poll()
        self.controller.edit({'volume': 82})
        self.controller.apply()
        old_id = int(self.worker.lines[-1].split()[1])
        self.worker.inbox.put(old_epoch, 'frame', dict(v=1, type='state', uptime_ms=400,
            revision=0, events=[[1, 300, 1, 20, 16, 60, False, 0]]))
        self.controller.poll()
        self.assertEqual(self.controller.epoch, old_epoch)
        self.assertFalse(self.controller.ready)
        self.assertIsNone(self.controller.revision)
        self.assertIsNone(self.controller.config)
        self.assertIsNone(self.controller.draft_revision)
        self.assertEqual(self.controller.draft['volume'], 82)
        self.assertTrue(self.worker.lines[-1].endswith(' -1 get'))
        self.assertEqual(len(self.controller.events.rows), 2)
        self.assertNotEqual(self.controller.events.rows[0]['session'], self.controller.events.rows[1]['session'])
        self.worker.inbox.put(old_epoch, 'frame', dict(v=1, type='reply', id=old_id,
            ok=True, code='ok', revision=8, config=DEFAULT_CONFIG))
        self.controller.poll()
        self.assertIsNone(self.controller.revision)
        self.worker.reply(DEFAULT_CONFIG, revision=0)
        self.controller.poll()
        self.assertTrue(self.worker.lines[-1].endswith(' -1 status'))
        self.worker.reply(revision=0, state=dict(uptime_ms=450, revision=0, events=[]))
        self.controller.poll()
        self.worker.reply(revision=0)
        self.controller.poll()
        self.assertTrue(self.controller.ready)
        self.assertEqual(self.controller.draft_revision, 0)
        self.assertEqual(self.controller.draft['volume'], 82)
        self.controller.apply()
        self.assertIn(' 0 apply volume=82', self.worker.lines[-1])

    def test_uptime_uint32_wrap_does_not_invalidate_applied_config(self):
        self.bootstrap()
        for uptime in (0xfffffff0, 20):
            self.worker.inbox.put(self.worker.epoch, 'frame', dict(v=1, type='state',
                uptime_ms=uptime, revision=7, events=[]))
            self.controller.poll()
        self.assertTrue(self.controller.ready)
        self.assertEqual(self.controller.revision, 7)
        self.assertFalse(any(p.verb == 'get' for p in self.controller.pending.values()))

    def test_reboot_without_events_resets_dedup_before_later_first_low_sequence(self):
        self.bootstrap()
        epoch = self.worker.epoch
        for uptime, events in ((200, [[12, 150, 1, 20, 16, 60, False, 7]]),
                               (9000, []), (400, [])):
            self.worker.inbox.put(epoch, 'frame', dict(v=1, type='state',
                uptime_ms=uptime, revision=7 if uptime != 400 else 0, events=events))
            self.controller.poll()
        self.worker.inbox.put(epoch, 'frame', dict(v=1, type='state', uptime_ms=600,
            revision=0, events=[[1, 550, 1, 20, 16, 60, False, 0]]))
        self.controller.poll()
        self.assertEqual(len(self.controller.events.rows), 2)
        self.assertNotEqual(self.controller.events.rows[0]['session'], self.controller.events.rows[1]['session'])


class InboxTests(unittest.TestCase):
    def test_retired_worker_cannot_replace_current_session_snapshot_or_reply_queue(self):
        inbox = Inbox(control_limit=2)
        inbox.activate(2)
        inbox.put(2, 'frame', dict(v=1, type='state', uptime_ms=100, events=[]))
        for _ in range(10):
            inbox.put(1, 'frame', dict(v=1, type='reply', id=999, ok=True))
        inbox.put(1, 'frame', dict(v=1, type='state', uptime_ms=1, events=[]))
        batch = inbox.drain()
        self.assertEqual(batch.state[0], 2)
        self.assertEqual(batch.state[1]['uptime_ms'], 100)
        self.assertEqual(batch.controls, [])
        self.assertIsNone(batch.fatal)

    def test_burst_telemetry_preserves_events_and_does_not_starve_reply(self):
        inbox = Inbox(event_limit=64, control_limit=16)
        def producer():
            for seq in range(100):
                inbox.put(1, 'frame', dict(v=1, type='state', uptime_ms=seq,
                                          events=[[seq, seq, 1, 20, 20, 60, False, 0]]))
            inbox.put(1, 'frame', dict(v=1, type='reply', id=10, ok=True, revision=0))
        thread = threading.Thread(target=producer)
        thread.start()
        thread.join()
        batch = inbox.drain()
        self.assertEqual(batch.controls[-1][2]['id'], 10)
        self.assertEqual(batch.state[1]['uptime_ms'], 99)
        self.assertEqual(len(batch.events), 64)
        self.assertEqual(inbox.stats['event_drops'], 36)
        self.assertGreater(inbox.stats['state_coalesced'], 0)


class EventTests(unittest.TestCase):
    def test_real_long_shading_event_is_not_limited_to_strike_input_range(self):
        log = EventLog()
        log.new_connection(1)
        self.assertTrue(log.add([1, 120001, 1, 120000, 127, 60, False, 3], uptime_ms=120001))
        self.assertEqual(log.rows[0]['duration_ms'], 120000)

    def test_csv_is_per_event_deduplicated_and_has_synthetic_revision_and_session(self):
        log = EventLog(max_rows=3)
        log.new_connection(3, demo=True)
        event = [1, 20, 1, 300, 80, 60, True, 7]
        with tempfile.TemporaryDirectory() as folder:
            path = folder + '/events.csv'
            log.start_capture(path)
            self.assertTrue(log.add(event, uptime_ms=30))
            self.assertFalse(log.add(event, uptime_ms=30))
            log.add([2, 40, 2, 500, 127, -1, False, 8], uptime_ms=40)
            # Device reboot on the same UART connection is a distinct session.
            self.assertTrue(log.add(event, uptime_ms=5))
            log.stop_capture()
            with open(path, newline='', encoding='utf-8-sig') as source:
                rows = list(csv.DictReader(source))
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]['synthetic'], '1')
        self.assertEqual(rows[1]['revision'], '8')
        self.assertNotEqual(rows[0]['session'], rows[2]['session'])
        target = io.StringIO()
        log.write_csv(target)
        self.assertIn('duration_ms', target.getvalue())


if __name__ == '__main__':
    unittest.main()
