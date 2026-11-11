#!/usr/bin/env python3
"""Loopback-only, read-only serial viewer for the existing FDC2214 CSV stream.

Starting without --serial only serves the GUI. No MCU command or firmware
operation is sent; DTR/RTS are set false before opening. Opening a port may
still cause hardware/OS reset or USB re-enumeration, which cannot be guaranteed
away in software. POST /api/clear affects this viewer only.
"""
from __future__ import annotations

import argparse
from collections import deque
import csv
from dataclasses import dataclass
import importlib.util
import io
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import signal
import stat
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("fdc_csv_protocol", ROOT / "tools/extract_csv.py")
_protocol = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
_spec.loader.exec_module(_protocol)
parse_record = _protocol.parse_record

HISTORY_LIMIT = 600
LINE_LIMIT = 4096
REQUEST_LIMIT = 2048
STALE_MS = 2000


class ApiError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status


def serial_ports() -> list[dict]:
    """List USB TTYs, preferring stable by-id links; never open any device."""
    from serial.tools import list_ports

    links = {}
    by_id = Path("/dev/serial/by-id")
    if by_id.is_dir():
        for link in sorted(by_id.iterdir()):
            try:
                if link.name.startswith("usb-") and stat.S_ISCHR(link.stat().st_mode):
                    links.setdefault(str(link.resolve()), str(link))
            except OSError:
                continue
    ports = {}
    for item in list_ports.comports():
        path = Path(item.device)
        try:
            if not stat.S_ISCHR(path.stat().st_mode):
                continue
        except OSError:
            continue
        # Do not expose the many legacy /dev/ttyS* entries as candidate devices.
        if item.vid is None and not path.name.startswith(("ttyACM", "ttyUSB")):
            continue
        real = str(path.resolve())
        selected = links.get(real, str(path))
        label = str(item.description or path.name)
        if item.serial_number:
            label += " · " + str(item.serial_number)
        ports[real] = {"path": selected, "label": label}
    return sorted(ports.values(), key=lambda p: p["path"])


class ViewerState:
    """Thread-safe bounded model. Observed ranges deliberately include warnings."""

    def __init__(self, *, clock=time.monotonic, logger=None):
        self.lock = threading.RLock()
        self.clock = clock
        self.logger = logger
        self.connected = False
        self.port = ""
        self.error = None
        self.session = 0
        self.epoch = None
        self.events = deque(maxlen=12)
        self.history = deque(maxlen=HISTORY_LIMIT)
        self.rates = deque(maxlen=100)
        self.partial = bytearray()
        self.dropping_line = False
        self._clear_measurements(clear_device=True, clear_latest=True)

    @staticmethod
    def _channel(index):
        return {"index": index, "raw": None, "flags": 0, "valid": False,
                "observed_min": None, "observed_max": None, "observed_span": None,
                "valid_count": None, "error_count": None,
                "device_min": None, "device_max": None, "device_span": None}

    def _clear_measurements(self, *, clear_device, clear_latest):
        self.history.clear()
        self.rates.clear()
        self.sample_count = 0
        self.output_skipped = 0
        self.malformed = 0
        self.last_sequence = None
        self.last_device_us = None
        if not hasattr(self, "channels") or clear_device:
            self.channels = [self._channel(ch) for ch in range(4)]
        else:
            for ch in self.channels:
                ch["observed_min"] = ch["observed_max"] = ch["observed_span"] = None
        if clear_latest:
            self.received_at = None
            for ch in self.channels:
                ch.update(raw=None, flags=0, valid=False)

    def _event(self, text):
        self.events.append(str(text).replace("\r", " ").replace("\n", " ")[:240])

    def _new_session(self, reason, epoch=None):
        self.session += 1
        self.epoch = epoch
        self._clear_measurements(clear_device=True, clear_latest=True)
        self._event(reason)

    def connected_to(self, port):
        with self.lock:
            self.partial.clear()
            self.dropping_line = False
            self.port = port
            self.connected = True
            self.error = None
            self._new_session("已连接，只读采集；未发送设备命令")

    def disconnected(self, error=None, port=None):
        with self.lock:
            self.connected = False
            self.error = str(error)[:240] if error else None
            if port is not None:
                self.port = port
            self.partial.clear()
            self.dropping_line = False
            self._event("串口已断开，保留数据仅供查看" + (": " + self.error if self.error else ""))

    def clear_window(self):
        with self.lock:
            previous_sequence, previous_device_us = self.last_sequence, self.last_device_us
            self.session += 1
            self._clear_measurements(clear_device=False, clear_latest=False)
            self.last_sequence, self.last_device_us = previous_sequence, previous_device_us
            self._event("仅清除GUI历史和观察范围；设备统计保留，未发送MCU clear")

    def _epoch(self, epoch):
        if self.epoch is None:
            if self.session == 0:
                self._new_session("收到首个数据区间", epoch)
            else:
                self.epoch = epoch
        elif epoch != self.epoch:
            self._new_session("设备epoch改变，已清除旧会话", epoch)

    def _line(self, line):
        if self.logger:
            self.logger.info("%s", line[:LINE_LIMIT])
        if "ESP-ROM:" in line:
            self._new_session("检测到设备启动信息，旧数据区间已清除")
            return
        try:
            record = parse_record(line)
        except (ValueError, csv.Error, OverflowError):
            record = None
        if record is None:
            if "sample," in line or "range," in line:
                self.malformed += 1
            elif "event," in line:
                # Firmware's epoch notification can precede its first sample.
                parts = line[line.find("event,"):].strip().split(",")
                if len(parts) == 4 and parts[2] == "epoch":
                    try:
                        epoch = int(parts[3])
                        if 0 <= epoch <= (1 << 64) - 1:
                            self._epoch(epoch)
                    except ValueError:
                        pass
                self._event(line)
            return
        # Values produced by firmware are uint64 or narrower. Reject an
        # otherwise structurally valid, nonsensically large integer before it
        # can reset a session or overflow rate/time conversion.
        if any(int(value, 0) > (1 << 64) - 1 for value in record[1:] if value):
            self.malformed += 1
            return
        epoch = int(record[1], 0)
        self._epoch(epoch)
        if record[0] == "range":
            ch = self.channels[int(record[2])]
            ch["valid_count"], ch["error_count"] = int(record[3]), int(record[4])
            present = int(record[5]) == 1
            ch["device_min"] = int(record[6]) if present else None
            ch["device_max"] = int(record[7]) if present else None
            ch["device_span"] = int(record[8]) if present else None
            return
        values = [int(value, 0) for value in record[1:]]
        if values[4] > 0xFFFF or values[5] > 0xFFFF:
            self.malformed += 1
            return
        sequence, device_us = values[1], values[2]
        if self.last_sequence is not None:
            if sequence < self.last_sequence or device_us < self.last_device_us:
                self._new_session("设备sequence或时钟回退，已清除旧会话", epoch)
            elif sequence == self.last_sequence:
                self.malformed += 1
                self._event("忽略重复sequence")
                return
        received = self.clock()
        raw = [values[index] for index in (9, 11, 13, 15)]
        flags = [values[index] for index in (10, 12, 14, 16)]
        mask = values[3]
        # Never present DATA warning bits as normal valid data, even if a
        # malformed producer accidentally sets the corresponding valid bit.
        if values[5]:
            mask = 0
        mask &= ~values[7]
        for index, ch in enumerate(self.channels):
            if flags[index]:
                mask &= ~(1 << index)
            ch.update(raw=raw[index], flags=flags[index], valid=bool(mask & (1 << index)))
            low, high = ch["observed_min"], ch["observed_max"]
            ch["observed_min"] = raw[index] if low is None else min(low, raw[index])
            ch["observed_max"] = raw[index] if high is None else max(high, raw[index])
            ch["observed_span"] = ch["observed_max"] - ch["observed_min"]
        self.history.append({"time_ms": device_us / 1000, "sequence": sequence,
                             "raw": raw, "flags": flags, "valid_mask": mask})
        self.rates.append((received, sequence, device_us))
        while len(self.rates) > 2 and received - self.rates[0][0] > 10:
            self.rates.popleft()
        self.received_at = received
        self.last_sequence, self.last_device_us = sequence, device_us
        self.output_skipped += values[8]
        self.sample_count += 1

    def feed(self, data: bytes):
        """Accept serial chunks; only newline-terminated bounded records publish."""
        with self.lock:
            self.partial.extend(data)
            while True:
                end = self.partial.find(b"\n")
                if end < 0:
                    if len(self.partial) > LINE_LIMIT:
                        self.partial.clear()
                        if not self.dropping_line:
                            self.malformed += 1
                            self._event("过长串口行已丢弃")
                        self.dropping_line = True
                    break
                line = bytes(self.partial[:end])
                del self.partial[:end + 1]
                if self.dropping_line:
                    self.dropping_line = False
                    continue
                if len(line) > LINE_LIMIT:
                    self.malformed += 1
                    continue
                self._line(line.decode("utf-8", errors="replace").rstrip("\r"))

    def state(self):
        with self.lock:
            age = None if self.received_at is None else max(0.0, (self.clock() - self.received_at) * 1000)
            stale = not self.connected or age is None or age > STALE_MS
            visible = device = None
            if not stale and len(self.rates) >= 2:
                first, last = self.rates[0], self.rates[-1]
                if last[0] > first[0]:
                    visible = (len(self.rates) - 1) / (last[0] - first[0])
                if last[2] > first[2]:
                    device = (last[1] - first[1]) * 1000000 / (last[2] - first[2])
            channels = [dict(channel) for channel in self.channels]
            if stale:
                for channel in channels:
                    channel["valid"] = False
            return {"connected": self.connected, "port": self.port, "error": self.error,
                    "sample_age_ms": age, "stale": stale, "session": self.session,
                    "epoch": self.epoch, "sample_count": self.sample_count,
                    "visible_rate_hz": visible, "device_rate_hz": device,
                    "output_skipped": self.output_skipped, "malformed": self.malformed,
                    "channels": channels,
                    "history": [{**row, "raw": row["raw"][:], "flags": row["flags"][:]} for row in self.history],
                    "events": list(self.events)}

    def export_csv(self):
        state = self.state()
        output = io.StringIO(newline="")
        writer = csv.writer(output)
        writer.writerow(["time_ms", "sequence", "ch0", "ch1", "ch2", "ch3",
                         "flags0", "flags1", "flags2", "flags3", "valid_mask"])
        for row in state["history"]:
            writer.writerow([row["time_ms"], row["sequence"], *row["raw"], *row["flags"], row["valid_mask"]])
        return output.getvalue().encode("utf-8"), state["session"]


class SerialReader:
    """Serialize connect/disconnect and own exactly one timeout-bounded reader."""

    def __init__(self, model, *, port_provider=serial_ports, serial_factory=None):
        self.model = model
        self.port_provider = port_provider
        self.serial_factory = serial_factory
        self.lifecycle = threading.Lock()
        self.handle = None
        self.thread = None
        self.stop = threading.Event()

    def ports(self):
        try:
            return self.port_provider()
        except Exception as exc:
            raise ApiError(503, "无法枚举USB串口: " + str(exc)[:180]) from exc

    def _disconnect(self):
        self.stop.set()
        thread, handle = self.thread, self.handle
        if thread:
            thread.join(timeout=1.0)
        if handle:
            try:
                handle.close()
            except Exception:
                pass
        if thread and thread.is_alive():
            thread.join(timeout=0.5)
        if thread and thread.is_alive():
            self.model.disconnected("串口读取线程尚未退出，拒绝建立第二个连接")
            raise ApiError(409, "读取线程未退出，请等待后再重连")
        self.thread = self.handle = None

    def connect(self, port):
        if not isinstance(port, str) or not port or len(port) > 512:
            raise ApiError(400, "需要有效USB串口路径")
        # Only the exact enumerated names are accepted: no arbitrary filesystem
        # paths, ttyS lists, symlink tricks supplied by the HTTP client or URLs.
        if port not in {entry["path"] for entry in self.ports()}:
            raise ApiError(400, "端口必须来自当前USB串口列表")
        with self.lifecycle:
            self._disconnect()
            factory = self.serial_factory
            if factory is None:
                import serial
                factory = serial.Serial
            handle = None
            try:
                handle = factory(port=None, baudrate=115200, timeout=0.1, write_timeout=0.1, exclusive=True)
                handle.dtr = False
                handle.rts = False
                handle.port = port
                handle.open()
            except Exception as exc:
                if handle:
                    try:
                        handle.close()
                    except Exception:
                        pass
                self.model.disconnected("打开串口失败: " + str(exc), port)
                raise ApiError(503, self.model.error) from exc
            self.handle = handle
            self.stop = threading.Event()
            self.model.connected_to(port)
            self.thread = threading.Thread(target=self._read, args=(handle, self.stop),
                                           name="fdc-serial-read", daemon=True)
            self.thread.start()
        return self.model.state()

    def _read(self, handle, stop):
        try:
            while not stop.is_set():
                chunk = handle.read(512)
                if chunk:
                    self.model.feed(chunk)
        except Exception as exc:
            if not stop.is_set():
                self.model.disconnected("串口读取失败: " + str(exc))
        finally:
            try:
                handle.close()
            except Exception:
                pass

    def disconnect(self):
        with self.lifecycle:
            self._disconnect()
            self.model.disconnected()
        return self.model.state()


@dataclass
class Response:
    status: int
    body: bytes
    content_type: str = "application/json; charset=utf-8"
    headers: dict | None = None


def json_response(value, status=200):
    return Response(status, json.dumps(value, ensure_ascii=False, allow_nan=False).encode("utf-8"))


class Application:
    def __init__(self, model=None, reader=None, *, port=8765, static_dir=None):
        self.model = model if model is not None else ViewerState()
        self.reader = reader if reader is not None else SerialReader(self.model)
        self.port = port
        self.static_dir = Path(static_dir) if static_dir else Path(__file__).resolve().parent / "static"

    def handle(self, method, target, body=b"", headers=None):
        """Pure request dispatch: tests call this without opening a socket/server."""
        try:
            headers = {key.lower(): value for key, value in (headers or {}).items()}
            authorities = {f"127.0.0.1:{self.port}", f"localhost:{self.port}"}
            if headers.get("host", "").lower() not in authorities:
                raise ApiError(403, "仅允许本机Host访问")
            origin = headers.get("origin")
            if origin and origin.lower() not in {"http://" + host for host in authorities}:
                raise ApiError(403, "拒绝跨站来源")
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not target.startswith("/") or target.startswith("//"):
                raise ApiError(400, "无效请求路径")
            path = unquote(parsed.path)
            if method == "GET":
                if path == "/api/state":
                    return json_response(self.model.state())
                if path == "/api/ports":
                    return json_response({"ports": self.reader.ports()})
                if path == "/api/export.csv":
                    data, session = self.model.export_csv()
                    return Response(200, data, "text/csv; charset=utf-8",
                                    {"Content-Disposition": f'attachment; filename="fdc2214-raw-session-{session}.csv"'})
                static = {"/": ("index.html", "text/html; charset=utf-8"),
                          "/index.html": ("index.html", "text/html; charset=utf-8"),
                          "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                          "/style.css": ("style.css", "text/css; charset=utf-8")}
                if path in static:
                    filename, mime = static[path]
                    base = self.static_dir.resolve()
                    resource = (base / filename).resolve()
                    if resource.parent != base or not resource.is_file():
                        raise ApiError(404, "界面资源不存在")
                    return Response(200, resource.read_bytes(), mime)
                raise ApiError(404, "接口或资源不存在")
            if method != "POST":
                raise ApiError(405, "仅支持GET/POST")
            if len(body) > REQUEST_LIMIT:
                raise ApiError(413, "请求内容过长")
            if headers.get("content-type", "").split(";", 1)[0].strip().lower() != "application/json":
                raise ApiError(415, "POST仅接受application/json")
            try:
                request = json.loads(body.decode("utf-8")) if body else {}
            except (ValueError, UnicodeDecodeError):
                raise ApiError(400, "JSON格式错误") from None
            if not isinstance(request, dict):
                raise ApiError(400, "JSON请求必须为对象")
            if path == "/api/connect":
                if set(request) != {"port"}:
                    raise ApiError(400, "连接请求只接受port字段")
                return json_response(self.reader.connect(request["port"]))
            if request:
                raise ApiError(400, "此接口不接受额外字段")
            if path == "/api/disconnect":
                return json_response(self.reader.disconnect())
            if path == "/api/clear":
                self.model.clear_window()
                return json_response(self.model.state())
            raise ApiError(404, "接口不存在")
        except ApiError as exc:
            return json_response({"error": str(exc)}, exc.status)
        except Exception as exc:
            return json_response({"error": "操作失败: " + str(exc)[:180]}, 500)


class RequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, format, *arguments):
        logger = getattr(self.server, "logger", None)
        if logger:
            logger.info("http " + format, *arguments)

    def _respond(self, response):
        self.send_response(response.status)
        self.send_header("Content-Type", response.content_type)
        self.send_header("Content-Length", str(len(response.body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; connect-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; frame-ancestors 'none'")
        for name, value in (response.headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(response.body)

    def _request(self):
        try:
            body = b""
            if self.headers.get("Transfer-Encoding"):
                self.close_connection = True
                raise ApiError(400, "不接受分块请求")
            if self.command == "POST":
                lengths = self.headers.get_all("Content-Length", [])
                if len(lengths) > 1:
                    self.close_connection = True
                    raise ApiError(400, "重复Content-Length")
                try:
                    length = int(lengths[0]) if lengths else 0
                except ValueError:
                    raise ApiError(400, "无效Content-Length") from None
                if length < 0 or length > REQUEST_LIMIT:
                    self.close_connection = True
                    raise ApiError(413, "请求内容过长")
                body = self.rfile.read(length)
                if len(body) != length:
                    raise ApiError(400, "请求内容不完整")
            response = self.server.application.handle(self.command, self.path, body, dict(self.headers))
        except ApiError as exc:
            response = json_response({"error": str(exc)}, exc.status)
        except TimeoutError:
            self.close_connection = True
            response = json_response({"error": "读取请求超时"}, 408)
        self._respond(response)

    do_GET = _request
    do_POST = _request
    do_OPTIONS = _request


class LocalServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", choices=("127.0.0.1",), default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--serial", help="Explicitly connect to one enumerated USB serial port at startup")
    parser.add_argument("--log-dir", type=Path, default=ROOT / "verification/gui")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger("fdc-gui")
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(args.log_dir / "viewer.log", maxBytes=1024 * 1024, backupCount=2, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
    logger.addHandler(handler)
    model = ViewerState(logger=logger)
    application = Application(model=model, port=args.port)
    server = LocalServer((args.host, args.port), RequestHandler)
    server.application = application
    server.logger = logger

    def stop_handler(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop_handler)
    try:
        if args.serial:
            try:
                application.reader.connect(args.serial)
            except ApiError as exc:
                logger.error("Startup connection failed: %s", exc)
                model.disconnected(str(exc), args.serial)
                print(str(exc), flush=True)
        print(f"FDC2214 GUI: http://{args.host}:{args.port} (no MCU commands)", flush=True)
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        application.reader.disconnect()
        logger.removeHandler(handler)
        handler.close()


if __name__ == "__main__":
    main()
