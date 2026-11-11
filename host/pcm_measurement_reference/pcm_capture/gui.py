"""Tkinter GUI for the PCM capture tool."""

from __future__ import annotations

from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path
import json
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from .capture import run_capture
from .readers import FakeReader, RateLimitedReader, SerialReader, UsbBulkReader


DEFAULT_USB_VID = "0x359f"
DEFAULT_USB_PID = "0x3001"
DEFAULT_USB_EP_IN = "0x82"
DEFAULT_USB_TIMEOUT_MS = "200"


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


class QueueWriter:
    def __init__(self, out_queue: queue.Queue[tuple[str, str]]) -> None:
        self._out_queue = out_queue

    def write(self, text: str) -> int:
        if text:
            self._out_queue.put(("log", text))
        return len(text)

    def flush(self) -> None:
        return None


class CaptureGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("PCM Capture")
        self.geometry("780x560")
        self.minsize(700, 500)

        self._queue: queue.Queue[tuple[str, object]] = queue.Queue()
        self._worker: threading.Thread | None = None

        self.source_var = tk.StringVar(value="usb")
        self.port_var = tk.StringVar(value="COM16")
        self.baud_var = tk.StringVar(value="115200")
        self.usb_vid_var = tk.StringVar(value=DEFAULT_USB_VID)
        self.usb_pid_var = tk.StringVar(value=DEFAULT_USB_PID)
        self.usb_ep_var = tk.StringVar(value=DEFAULT_USB_EP_IN)
        self.usb_timeout_var = tk.StringVar(value=DEFAULT_USB_TIMEOUT_MS)
        self.duration_var = tk.StringVar(value="5")
        self.label_var = tk.StringVar(value="usb_pcm")
        self.out_var = tk.StringVar(value="captures")
        self.fake_pattern_var = tk.StringVar(value="counter")
        self.fake_freq_var = tk.StringVar(value="1000")
        self.status_var = tk.StringVar(value="Ready")
        self.output_dir_var = tk.StringVar(value="")

        self._build_layout()
        self._sync_source_state()
        self.after(100, self._poll_queue)

    def _build_layout(self) -> None:
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)

        settings = ttk.Frame(self, padding=12)
        settings.grid(row=0, column=0, sticky="ew")
        settings.columnconfigure(1, weight=1)
        settings.columnconfigure(3, weight=1)

        ttk.Label(settings, text="Source").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=4)
        source_buttons = ttk.Frame(settings)
        source_buttons.grid(row=0, column=1, sticky="w", pady=4)
        self.usb_button = ttk.Button(source_buttons, text="USB", command=lambda: self._set_source("usb"))
        self.serial_button = ttk.Button(source_buttons, text="Serial", command=lambda: self._set_source("serial"))
        self.fake_button = ttk.Button(source_buttons, text="Fake", command=lambda: self._set_source("fake"))
        self.usb_button.grid(row=0, column=0, padx=(0, 4))
        self.serial_button.grid(row=0, column=1, padx=(0, 4))
        self.fake_button.grid(row=0, column=2)

        ttk.Label(settings, text="Duration").grid(row=0, column=2, sticky="w", padx=(24, 8), pady=4)
        ttk.Entry(settings, textvariable=self.duration_var, width=12).grid(row=0, column=3, sticky="w", pady=4)

        ttk.Label(settings, text="USB VID").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=4)
        self.usb_vid_entry = ttk.Entry(settings, textvariable=self.usb_vid_var, width=16)
        self.usb_vid_entry.grid(row=1, column=1, sticky="w", pady=4)

        ttk.Label(settings, text="USB PID").grid(row=1, column=2, sticky="w", padx=(24, 8), pady=4)
        self.usb_pid_entry = ttk.Entry(settings, textvariable=self.usb_pid_var, width=12)
        self.usb_pid_entry.grid(row=1, column=3, sticky="w", pady=4)

        ttk.Label(settings, text="USB EP IN").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=4)
        self.usb_ep_entry = ttk.Entry(settings, textvariable=self.usb_ep_var, width=16)
        self.usb_ep_entry.grid(row=2, column=1, sticky="w", pady=4)

        ttk.Label(settings, text="USB Timeout").grid(row=2, column=2, sticky="w", padx=(24, 8), pady=4)
        self.usb_timeout_entry = ttk.Entry(settings, textvariable=self.usb_timeout_var, width=12)
        self.usb_timeout_entry.grid(row=2, column=3, sticky="w", pady=4)

        ttk.Label(settings, text="Port").grid(row=3, column=0, sticky="w", padx=(0, 8), pady=4)
        self.port_entry = ttk.Entry(settings, textvariable=self.port_var, width=16)
        self.port_entry.grid(row=3, column=1, sticky="w", pady=4)

        ttk.Label(settings, text="Baud").grid(row=3, column=2, sticky="w", padx=(24, 8), pady=4)
        self.baud_entry = ttk.Entry(settings, textvariable=self.baud_var, width=12)
        self.baud_entry.grid(row=3, column=3, sticky="w", pady=4)

        ttk.Label(settings, text="Fake").grid(row=4, column=0, sticky="w", padx=(0, 8), pady=4)
        self.pattern_box = ttk.Combobox(
            settings,
            textvariable=self.fake_pattern_var,
            values=("counter", "sine"),
            state="readonly",
            width=14,
        )
        self.pattern_box.grid(row=4, column=1, sticky="w", pady=4)

        ttk.Label(settings, text="Freq").grid(row=4, column=2, sticky="w", padx=(24, 8), pady=4)
        self.fake_freq_entry = ttk.Entry(settings, textvariable=self.fake_freq_var, width=12)
        self.fake_freq_entry.grid(row=4, column=3, sticky="w", pady=4)

        ttk.Label(settings, text="Label").grid(row=5, column=0, sticky="w", padx=(0, 8), pady=4)
        ttk.Entry(settings, textvariable=self.label_var).grid(row=5, column=1, sticky="ew", pady=4)

        ttk.Label(settings, text="Output").grid(row=5, column=2, sticky="w", padx=(24, 8), pady=4)
        output_frame = ttk.Frame(settings)
        output_frame.grid(row=5, column=3, sticky="ew", pady=4)
        output_frame.columnconfigure(0, weight=1)
        ttk.Entry(output_frame, textvariable=self.out_var).grid(row=0, column=0, sticky="ew")
        ttk.Button(output_frame, text="Browse", command=self._browse_output).grid(row=0, column=1, padx=(6, 0))

        actions = ttk.Frame(self, padding=(12, 0, 12, 8))
        actions.grid(row=1, column=0, sticky="ew")
        actions.columnconfigure(1, weight=1)
        self.start_button = ttk.Button(actions, text="Start Capture", command=self._start_capture)
        self.start_button.grid(row=0, column=0, sticky="w")
        ttk.Label(actions, textvariable=self.status_var).grid(row=0, column=1, sticky="w", padx=(12, 0))

        panes = ttk.PanedWindow(self, orient=tk.VERTICAL)
        panes.grid(row=2, column=0, sticky="nsew", padx=12, pady=(0, 12))

        log_frame = ttk.Frame(panes)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        self.log_text = tk.Text(log_frame, height=14, wrap="word")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        log_scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        log_scroll.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=log_scroll.set)
        panes.add(log_frame, weight=3)

        result_frame = ttk.Frame(panes, padding=(0, 8, 0, 0))
        result_frame.columnconfigure(1, weight=1)
        ttk.Label(result_frame, text="Last Run").grid(row=0, column=0, sticky="w", padx=(0, 8))
        ttk.Entry(result_frame, textvariable=self.output_dir_var, state="readonly").grid(row=0, column=1, sticky="ew")
        ttk.Button(result_frame, text="Open Folder", command=self._open_last_folder).grid(row=0, column=2, padx=(8, 0))
        self.summary_text = tk.Text(result_frame, height=7, wrap="word")
        self.summary_text.grid(row=1, column=0, columnspan=3, sticky="nsew", pady=(8, 0))
        result_frame.rowconfigure(1, weight=1)
        panes.add(result_frame, weight=1)

    def _set_source(self, source: str) -> None:
        self.source_var.set(source)
        self._sync_source_state()

    def _sync_source_state(self) -> None:
        source = self.source_var.get()
        usb_enabled = source == "usb"
        serial_enabled = source == "serial"
        fake_enabled = source == "fake"
        self._set_state(self.usb_vid_entry, usb_enabled)
        self._set_state(self.usb_pid_entry, usb_enabled)
        self._set_state(self.usb_ep_entry, usb_enabled)
        self._set_state(self.usb_timeout_entry, usb_enabled)
        self._set_state(self.port_entry, serial_enabled)
        self._set_state(self.baud_entry, serial_enabled)
        self._set_state(self.pattern_box, fake_enabled)
        self._set_state(self.fake_freq_entry, fake_enabled)
        self.usb_button.configure(state="disabled" if usb_enabled else "normal")
        self.serial_button.configure(state="disabled" if serial_enabled else "normal")
        self.fake_button.configure(state="disabled" if fake_enabled else "normal")
        if usb_enabled and self.label_var.get() in ("com16_probe", "fake_probe"):
            self.label_var.set("usb_pcm")
        elif fake_enabled and self.label_var.get() in ("usb_pcm", "com16_probe"):
            self.label_var.set("fake_probe")
        elif serial_enabled and self.label_var.get() in ("usb_pcm", "fake_probe"):
            self.label_var.set("com16_probe")

    def _set_state(self, widget: tk.Widget, enabled: bool) -> None:
        widget.configure(state="normal" if enabled else "disabled")

    def _browse_output(self) -> None:
        selected = filedialog.askdirectory(initialdir=str(Path(self.out_var.get()).resolve()))
        if selected:
            self.out_var.set(selected)

    def _start_capture(self) -> None:
        if self._worker and self._worker.is_alive():
            return

        try:
            duration = float(self.duration_var.get())
            if duration <= 0:
                raise ValueError("Duration must be greater than 0 in the GUI.")
            reader = self._make_reader()
        except Exception as exc:
            messagebox.showerror("PCM Capture", str(exc))
            return

        output_root = Path(self.out_var.get())
        label = self.label_var.get().strip() or None
        self._set_busy(True)
        self._clear_texts()

        self._worker = threading.Thread(
            target=self._capture_worker,
            kwargs={
                "reader": reader,
                "duration": duration,
                "output_root": output_root,
                "label": label,
            },
            daemon=True,
        )
        self._worker.start()

    def _make_reader(self):
        if self.source_var.get() == "usb":
            timeout_ms = int(self.usb_timeout_var.get())
            if timeout_ms <= 0:
                raise ValueError("USB timeout must be positive.")
            return UsbBulkReader(
                vid=parse_int(self.usb_vid_var.get()),
                pid=parse_int(self.usb_pid_var.get()),
                ep_in=parse_int(self.usb_ep_var.get()),
                timeout_ms=timeout_ms,
            )

        if self.source_var.get() == "fake":
            pattern = self.fake_pattern_var.get()
            frequency = float(self.fake_freq_var.get())
            reader = FakeReader(
                pattern=pattern,
                sample_rate=48000,
                samples_per_frame=256,
                frequency=frequency,
            )
            frame_bytes = 14 + 256 * 2
            frames_per_second = 48000 / 256
            return RateLimitedReader(reader, frame_bytes * frames_per_second)

        port = self.port_var.get().strip()
        if not port:
            raise ValueError("Port is required.")
        baud = int(self.baud_var.get())
        return SerialReader(port=port, baud=baud)

    def _capture_worker(self, *, reader, duration: float, output_root: Path, label: str | None) -> None:
        writer = QueueWriter(self._queue)
        try:
            with redirect_stdout(writer), redirect_stderr(writer):
                capture_dir = run_capture(
                    reader=reader,
                    duration=duration,
                    output_root=output_root,
                    label=label,
                    debug_errors=False,
                )
            self._queue.put(("done", capture_dir))
        except Exception as exc:
            self._queue.put(("error", str(exc)))

    def _poll_queue(self) -> None:
        while True:
            try:
                kind, payload = self._queue.get_nowait()
            except queue.Empty:
                break

            if kind == "log":
                self._append_log(str(payload))
            elif kind == "done":
                self._show_done(Path(payload))
            elif kind == "error":
                self._set_busy(False)
                self.status_var.set("Error")
                messagebox.showerror("PCM Capture", str(payload))

        self.after(100, self._poll_queue)

    def _show_done(self, capture_dir: Path) -> None:
        self._set_busy(False)
        self.status_var.set("Done")
        self.output_dir_var.set(str(capture_dir))
        self._append_summary(capture_dir)

    def _append_summary(self, capture_dir: Path) -> None:
        stats_path = capture_dir / "stats.json"
        if not stats_path.exists():
            self.summary_text.insert("end", "stats.json was not written.\n")
            return

        stats = json.loads(stats_path.read_text(encoding="utf-8"))
        audio = stats.get("audio", {})
        lines = [
            f"Folder: {capture_dir}",
            f"Frames: {stats.get('valid_frames', 0)}",
            f"Samples: {audio.get('sample_count', 0)}",
            f"Raw bytes: {stats.get('raw_bytes', 0)}",
            f"Seq jumps: {stats.get('seq_jumps', 0)}",
            f"Resync: {stats.get('resync_count', 0)}",
            f"WAV verified: {stats.get('wav_verified', False)}",
        ]
        freq = audio.get("estimated_frequency_hz")
        if isinstance(freq, (int, float)):
            lines.append(f"Estimated frequency: {freq:.2f} Hz")

        self.summary_text.delete("1.0", "end")
        self.summary_text.insert("end", "\n".join(lines) + "\n")

    def _append_log(self, text: str) -> None:
        self.log_text.insert("end", text)
        self.log_text.see("end")

    def _clear_texts(self) -> None:
        self.log_text.delete("1.0", "end")
        self.summary_text.delete("1.0", "end")
        self.output_dir_var.set("")

    def _set_busy(self, busy: bool) -> None:
        self.start_button.configure(state="disabled" if busy else "normal")
        self.status_var.set("Capturing..." if busy else "Ready")

    def _open_last_folder(self) -> None:
        path = self.output_dir_var.get()
        if not path:
            return
        folder = Path(path)
        if not folder.exists():
            messagebox.showerror("PCM Capture", "The capture folder no longer exists.")
            return
        import os

        os.startfile(str(folder))


def main() -> int:
    app = CaptureGui()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
