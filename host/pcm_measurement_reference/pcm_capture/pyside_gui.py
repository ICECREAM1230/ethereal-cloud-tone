"""PySide6 GUI for PCM capture."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import json
import os
import sys

import PySide6

_PYSIDE_DIR = Path(PySide6.__file__).resolve().parent
_PYSIDE_PLUGIN_DIR = _PYSIDE_DIR / "plugins"
_PYSIDE_PLATFORM_DIR = _PYSIDE_PLUGIN_DIR / "platforms"

if _PYSIDE_PLUGIN_DIR.exists():
    os.environ["QT_PLUGIN_PATH"] = str(_PYSIDE_PLUGIN_DIR)
if _PYSIDE_PLATFORM_DIR.exists():
    os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(_PYSIDE_PLATFORM_DIR)

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QDesktopServices, QTextCursor
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QSizePolicy,
    QDoubleSpinBox,
    QVBoxLayout,
    QWidget,
)
from PySide6.QtCore import QUrl

from .capture import run_capture
from .readers import FakeReader, RateLimitedReader, SerialReader, UsbBulkReader


DEFAULT_USB_VID = "0x359f"
DEFAULT_USB_PID = "0x3001"
DEFAULT_USB_EP_IN = "0x82"
DEFAULT_USB_TIMEOUT_MS = "200"


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


class SignalWriter:
    def __init__(self, signal: Signal) -> None:
        self._signal = signal

    def write(self, text: str) -> int:
        if text:
            self._signal.emit(text)
        return len(text)

    def flush(self) -> None:
        return None


class CaptureThread(QThread):
    log = Signal(str)
    failed = Signal(str)
    completed = Signal(str)

    def __init__(
        self,
        *,
        source: str,
        port: str,
        baud: int,
        usb_vid: int,
        usb_pid: int,
        usb_ep_in: int,
        usb_timeout_ms: int,
        pattern: str,
        frequency: float,
        duration: float,
        output_root: Path,
        label: str | None,
    ) -> None:
        super().__init__()
        self._source = source
        self._port = port
        self._baud = baud
        self._usb_vid = usb_vid
        self._usb_pid = usb_pid
        self._usb_ep_in = usb_ep_in
        self._usb_timeout_ms = usb_timeout_ms
        self._pattern = pattern
        self._frequency = frequency
        self._duration = duration
        self._output_root = output_root
        self._label = label

    def run(self) -> None:
        try:
            reader = self._make_reader()
            writer = SignalWriter(self.log)
            with redirect_stdout(writer), redirect_stderr(writer):
                capture_dir = run_capture(
                    reader=reader,
                    duration=self._duration,
                    output_root=self._output_root,
                    label=self._label,
                    debug_errors=False,
                )
            self.completed.emit(str(capture_dir))
        except Exception as exc:
            self.failed.emit(str(exc))

    def _make_reader(self):
        if self._source == "fake":
            reader = FakeReader(
                pattern=self._pattern,
                sample_rate=48000,
                samples_per_frame=256,
                frequency=self._frequency,
            )
            frame_bytes = 14 + 256 * 2
            frames_per_second = 48000 / 256
            return RateLimitedReader(reader, frame_bytes * frames_per_second)

        if self._source == "usb":
            return UsbBulkReader(
                vid=self._usb_vid,
                pid=self._usb_pid,
                ep_in=self._usb_ep_in,
                timeout_ms=self._usb_timeout_ms,
            )

        return SerialReader(port=self._port, baud=self._baud)


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("PCM Capture")
        self.resize(900, 640)
        self._thread: CaptureThread | None = None
        self._last_dir: Path | None = None

        root = QWidget()
        layout = QVBoxLayout(root)
        layout.setContentsMargins(14, 14, 14, 14)
        layout.setSpacing(10)

        layout.addWidget(self._build_settings())
        layout.addWidget(self._build_environment())
        layout.addWidget(self._build_actions())
        layout.addWidget(self._build_log(), stretch=3)
        layout.addWidget(self._build_summary(), stretch=1)
        self.setCentralWidget(root)

        self._apply_style()
        self._sync_source()

    def _build_environment(self) -> QWidget:
        frame = QFrame()
        row = QHBoxLayout(frame)
        row.setContentsMargins(0, 0, 0, 0)
        label = QLabel(f"Python: {sys.executable}")
        label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        row.addWidget(label)
        return frame

    def _build_settings(self) -> QWidget:
        group = QGroupBox("Capture Settings")
        grid = QGridLayout(group)
        grid.setHorizontalSpacing(14)
        grid.setVerticalSpacing(10)

        self.source_combo = QComboBox()
        self.source_combo.addItems(["usb", "serial", "fake"])
        self.source_combo.currentTextChanged.connect(self._sync_source)

        source_buttons = QHBoxLayout()
        self.usb_button = QPushButton("USB")
        self.serial_button = QPushButton("Serial")
        self.fake_button = QPushButton("Fake")
        for button in (self.usb_button, self.serial_button, self.fake_button):
            button.setCheckable(True)
        self.usb_button.clicked.connect(lambda: self.source_combo.setCurrentText("usb"))
        self.serial_button.clicked.connect(lambda: self.source_combo.setCurrentText("serial"))
        self.fake_button.clicked.connect(lambda: self.source_combo.setCurrentText("fake"))
        source_buttons.addWidget(self.usb_button)
        source_buttons.addWidget(self.serial_button)
        source_buttons.addWidget(self.fake_button)

        self.port_edit = QLineEdit("COM16")
        self.baud_edit = QLineEdit("115200")
        self.usb_vid_edit = QLineEdit(DEFAULT_USB_VID)
        self.usb_pid_edit = QLineEdit(DEFAULT_USB_PID)
        self.usb_ep_edit = QLineEdit(DEFAULT_USB_EP_IN)
        self.usb_timeout_edit = QLineEdit(DEFAULT_USB_TIMEOUT_MS)
        self.duration_spin = QDoubleSpinBox()
        self.duration_spin.setRange(0.1, 3600.0)
        self.duration_spin.setDecimals(1)
        self.duration_spin.setValue(5.0)
        self.duration_spin.setSuffix(" s")

        self.pattern_combo = QComboBox()
        self.pattern_combo.addItems(["counter", "sine"])
        self.frequency_spin = QDoubleSpinBox()
        self.frequency_spin.setRange(1.0, 20000.0)
        self.frequency_spin.setDecimals(1)
        self.frequency_spin.setValue(1000.0)
        self.frequency_spin.setSuffix(" Hz")

        self.label_edit = QLineEdit("usb_pcm")
        self.output_edit = QLineEdit("captures")
        browse = QPushButton("Browse")
        browse.clicked.connect(self._choose_output)

        output_row = QHBoxLayout()
        output_row.addWidget(self.output_edit)
        output_row.addWidget(browse)

        grid.addWidget(QLabel("Source"), 0, 0)
        grid.addLayout(source_buttons, 0, 1)
        grid.addWidget(QLabel("Duration"), 0, 2)
        grid.addWidget(self.duration_spin, 0, 3)
        grid.addWidget(QLabel("USB VID"), 1, 0)
        grid.addWidget(self.usb_vid_edit, 1, 1)
        grid.addWidget(QLabel("USB PID"), 1, 2)
        grid.addWidget(self.usb_pid_edit, 1, 3)
        grid.addWidget(QLabel("USB EP IN"), 2, 0)
        grid.addWidget(self.usb_ep_edit, 2, 1)
        grid.addWidget(QLabel("USB Timeout"), 2, 2)
        grid.addWidget(self.usb_timeout_edit, 2, 3)
        grid.addWidget(QLabel("Port"), 3, 0)
        grid.addWidget(self.port_edit, 3, 1)
        grid.addWidget(QLabel("Baud"), 3, 2)
        grid.addWidget(self.baud_edit, 3, 3)
        grid.addWidget(QLabel("Fake Pattern"), 4, 0)
        grid.addWidget(self.pattern_combo, 4, 1)
        grid.addWidget(QLabel("Frequency"), 4, 2)
        grid.addWidget(self.frequency_spin, 4, 3)
        grid.addWidget(QLabel("Label"), 5, 0)
        grid.addWidget(self.label_edit, 5, 1)
        grid.addWidget(QLabel("Output"), 5, 2)
        grid.addLayout(output_row, 5, 3)
        self.source_combo.hide()
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(3, 1)
        return group

    def _build_actions(self) -> QWidget:
        frame = QFrame()
        row = QHBoxLayout(frame)
        row.setContentsMargins(0, 0, 0, 0)

        self.start_button = QPushButton("Start Capture")
        self.start_button.clicked.connect(self._start_capture)
        self.status_label = QLabel("Ready")
        self.status_label.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)

        row.addWidget(self.start_button)
        row.addWidget(self.status_label)
        return frame

    def _build_log(self) -> QWidget:
        group = QGroupBox("Log")
        layout = QVBoxLayout(group)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        layout.addWidget(self.log_view)
        return group

    def _build_summary(self) -> QWidget:
        group = QGroupBox("Last Capture")
        layout = QVBoxLayout(group)
        top = QHBoxLayout()
        self.last_dir_edit = QLineEdit()
        self.last_dir_edit.setReadOnly(True)
        open_button = QPushButton("Open Folder")
        open_button.clicked.connect(self._open_last_dir)
        top.addWidget(self.last_dir_edit)
        top.addWidget(open_button)

        self.summary_view = QPlainTextEdit()
        self.summary_view.setReadOnly(True)
        layout.addLayout(top)
        layout.addWidget(self.summary_view)
        return group

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QMainWindow { background: #f6f7f9; }
            QGroupBox {
                font-weight: 600;
                border: 1px solid #d7dce2;
                border-radius: 6px;
                margin-top: 10px;
                background: white;
            }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
            QPushButton {
                background: #ffffff;
                border: 1px solid #aeb8c4;
                border-radius: 5px;
                padding: 6px 12px;
                color: #111827;
            }
            QPushButton:hover { background: #eef5ff; border-color: #6aa1e6; }
            QPushButton:pressed { background: #d9eaff; }
            QPushButton:checked {
                background: #2563eb;
                border-color: #1d4ed8;
                color: #ffffff;
                font-weight: 600;
            }
            QPushButton:disabled {
                background: #eef0f3;
                border-color: #d3d8df;
                color: #8a94a3;
            }
            QLineEdit, QComboBox, QDoubleSpinBox, QPlainTextEdit {
                border: 1px solid #cfd5dd;
                border-radius: 5px;
                padding: 4px;
                background: white;
            }
            """
        )

    def _sync_source(self) -> None:
        source = self.source_combo.currentText()
        usb = source == "usb"
        serial = source == "serial"
        fake = source == "fake"
        for button, active in (
            (self.usb_button, usb),
            (self.serial_button, serial),
            (self.fake_button, fake),
        ):
            button.setChecked(active)
        self.usb_vid_edit.setEnabled(usb)
        self.usb_pid_edit.setEnabled(usb)
        self.usb_ep_edit.setEnabled(usb)
        self.usb_timeout_edit.setEnabled(usb)
        self.port_edit.setEnabled(serial)
        self.baud_edit.setEnabled(serial)
        self.pattern_combo.setEnabled(fake)
        self.frequency_spin.setEnabled(fake)
        if usb and self.label_edit.text() in ("com16_probe", "fake_probe"):
            self.label_edit.setText("usb_pcm")
        elif serial and self.label_edit.text() in ("usb_pcm", "fake_probe"):
            self.label_edit.setText("com16_probe")
        elif fake and self.label_edit.text() in ("usb_pcm", "com16_probe"):
            self.label_edit.setText("fake_probe")

    def _choose_output(self) -> None:
        selected = QFileDialog.getExistingDirectory(self, "Select output folder", self.output_edit.text())
        if selected:
            self.output_edit.setText(selected)

    def _start_capture(self) -> None:
        if self._thread and self._thread.isRunning():
            return

        try:
            baud = int(self.baud_edit.text())
            if baud <= 0:
                raise ValueError("Baud must be positive.")
            usb_vid = parse_int(self.usb_vid_edit.text())
            usb_pid = parse_int(self.usb_pid_edit.text())
            usb_ep_in = parse_int(self.usb_ep_edit.text())
            usb_timeout_ms = int(self.usb_timeout_edit.text())
            if usb_timeout_ms <= 0:
                raise ValueError("USB timeout must be positive.")
        except ValueError as exc:
            QMessageBox.critical(self, "PCM Capture", f"Invalid setting: {exc}")
            return

        self.log_view.clear()
        self.summary_view.clear()
        self.last_dir_edit.clear()
        self.start_button.setEnabled(False)
        self.status_label.setText("Capturing...")

        self._thread = CaptureThread(
            source=self.source_combo.currentText(),
            port=self.port_edit.text().strip(),
            baud=baud,
            usb_vid=usb_vid,
            usb_pid=usb_pid,
            usb_ep_in=usb_ep_in,
            usb_timeout_ms=usb_timeout_ms,
            pattern=self.pattern_combo.currentText(),
            frequency=self.frequency_spin.value(),
            duration=self.duration_spin.value(),
            output_root=Path(self.output_edit.text()),
            label=self.label_edit.text().strip() or None,
        )
        self._thread.log.connect(self._append_log)
        self._thread.failed.connect(self._capture_failed)
        self._thread.completed.connect(self._capture_completed)
        self._thread.finished.connect(lambda: self.start_button.setEnabled(True))
        self._thread.start()

    def _append_log(self, text: str) -> None:
        self.log_view.moveCursor(QTextCursor.MoveOperation.End)
        self.log_view.insertPlainText(text)
        self.log_view.moveCursor(QTextCursor.MoveOperation.End)

    def _capture_failed(self, message: str) -> None:
        self.status_label.setText("Error")
        QMessageBox.critical(self, "PCM Capture", message)

    def _capture_completed(self, path_text: str) -> None:
        capture_dir = Path(path_text)
        self._last_dir = capture_dir
        self.status_label.setText("Done")
        self.last_dir_edit.setText(str(capture_dir))
        self._load_summary(capture_dir)

    def _load_summary(self, capture_dir: Path) -> None:
        stats_path = capture_dir / "stats.json"
        if not stats_path.exists():
            self.summary_view.setPlainText("stats.json was not written.")
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
        self.summary_view.setPlainText("\n".join(lines))

    def _open_last_dir(self) -> None:
        if not self._last_dir or not self._last_dir.exists():
            QMessageBox.information(self, "PCM Capture", "No capture folder is available.")
            return
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(self._last_dir)))


def main() -> int:
    app = QApplication.instance() or QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
