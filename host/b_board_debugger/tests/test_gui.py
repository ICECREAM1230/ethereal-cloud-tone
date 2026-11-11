"""Run under Xvfb. A real Tk window talks to DemoPort through SerialWorker."""
import os
import tempfile
import time
import unittest


@unittest.skipUnless(os.environ.get('DISPLAY'), 'GUI smoke: run xvfb-run -a ...')
class GuiSmokeTests(unittest.TestCase):
    def setUp(self):
        import tkinter as tk
        from host.b_board_debugger.gui import DebuggerApp
        self.root = tk.Tk()
        self.app = DebuggerApp(self.root)

    def tearDown(self):
        if self.root.winfo_exists():
            self.app.close()

    def wait_for(self, predicate, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.root.update()
            if predicate():
                return
            time.sleep(0.01)
        self.fail(self.app.controller.message)

    def test_real_window_edit_apply_save_strike_import_csv_and_disconnect(self):
        self.app.demo_button.invoke()
        self.wait_for(lambda: self.app.controller.ready)
        self.assertIn('演示', self.app.mode_var.get())
        self.assertEqual(len(self.app.note_vars), 12)
        self.assertEqual(len(self.app.gain_vars), 12)
        self.app.vars['volume'].set('82')
        self.app.note_vars[0].set('48')
        self.app.gain_vars[0].set('150')
        self.root.update()
        self.assertTrue(self.app.controller.dirty)
        self.assertEqual(self.app.controller.config['volume'], 100)
        self.app.apply_button.invoke()
        self.wait_for(lambda: not self.app.controller.writing and not self.app.controller.dirty)
        self.assertEqual(self.app.controller.config['volume'], 82)
        self.app.duration_var.set('500')
        self.app.strike_buttons[0].invoke()
        self.wait_for(lambda: any(r['synthetic'] and r['note'] == 48 for r in self.app.controller.events.rows))
        self.app.save_button.invoke()
        self.wait_for(lambda: not self.app.controller.writing and '保存完成' in self.app.status_var.get())
        with tempfile.TemporaryDirectory() as folder:
            config_path, csv_path = folder + '/config.json', folder + '/events.csv'
            self.app.export_path(config_path)
            self.app.vars['volume'].set('70')
            self.app.import_path(config_path)
            self.assertEqual(self.app.controller.draft['volume'], 82)
            self.assertTrue(self.app.controller.dirty)
            self.app.export_csv_path(csv_path)
            self.assertGreater(os.stat(csv_path).st_size, 100)
        # Small-window content stays reachable through both scroll axes.
        self.root.geometry('760x520')
        self.root.update()
        page = self.app.pages[0]
        self.assertGreater(page.canvas.bbox('all')[3], page.canvas.winfo_height())
        page.canvas.yview_moveto(1)
        self.root.update()
        self.assertGreater(page.canvas.yview()[0], 0)
        self.app.disconnect_button.invoke()
        self.root.update()
        self.assertFalse(self.app.controller.connected)
        self.assertFalse(self.app.controller.ready)

    def test_invalid_text_survives_telemetry_and_prevents_apply(self):
        self.app.demo_button.invoke()
        self.wait_for(lambda: self.app.controller.ready)
        self.app.vars['velocity_min'].set('broken')
        self.root.update()
        self.assertTrue(self.app.editor_error)
        self.wait_for(lambda: bool(self.app.controller.state))
        self.assertEqual(self.app.vars['velocity_min'].get(), 'broken')
        self.assertTrue(self.app.apply_button.instate(['disabled']))
        self.app.vars['velocity_min'].set('20')
        self.root.update()
        self.assertFalse(self.app.editor_error)

    def test_midi_wire_channel_selector_roundtrips_zero_and_fifteen(self):
        self.app.demo_button.invoke()
        self.wait_for(lambda: self.app.controller.ready)
        self.assertEqual(self.app.vars['channel'].get(), '1')
        self.assertEqual(self.app.controller.config['channel'], 2)
        for wire_id, public_channel in ((15, 16), (0, 1)):
            self.app.vars['channel'].set(str(wire_id))
            self.root.update()
            self.assertEqual(self.app.collect_draft()['channel'], public_channel)
            self.app.apply_button.invoke()
            self.wait_for(lambda: not self.app.controller.writing and not self.app.controller.dirty)
            self.assertEqual(self.app.controller.config['channel'], public_channel)
            self.assertEqual(self.app.vars['channel'].get(), str(wire_id))
            self.assertIn(f'编号 {wire_id}', self.app.midi_channel_var.get())
        for invalid in ('-1', '16'):
            self.app.vars['channel'].set(invalid)
            self.root.update()
            self.assertTrue(self.app.editor_error)
            self.assertTrue(self.app.apply_button.instate(['disabled']))


if __name__ == '__main__':
    unittest.main()
