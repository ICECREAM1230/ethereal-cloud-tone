"""Byte readers for fake, serial, and USB Bulk capture sources."""

from __future__ import annotations

import math
import os
from pathlib import Path
import struct
import sys
import time
from typing import Protocol

from .protocol import pack_frame


CH569_STREAM_REQUEST = 0x8F
CH569_STREAM_START_VALUE = 0x007F
MSYS2_MINGW64_BIN = Path(r"C:\msys64\mingw64\bin")


def prepare_libusb_dll_path() -> None:
    if os.name != "nt" or not MSYS2_MINGW64_BIN.exists():
        return

    path_text = str(MSYS2_MINGW64_BIN)
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(path_text)
    if path_text.lower() not in os.environ.get("PATH", "").lower().split(";"):
        os.environ["PATH"] = path_text + os.pathsep + os.environ.get("PATH", "")


class Reader(Protocol):
    source_name: str

    def read(self, max_bytes: int) -> bytes:
        ...

    def close(self) -> None:
        ...


class FakeReader:
    def __init__(
        self,
        *,
        pattern: str,
        sample_rate: int,
        samples_per_frame: int,
        frequency: float,
    ) -> None:
        self.source_name = f"fake_{pattern}"
        self._pattern = pattern
        self._sample_rate = sample_rate
        self._samples_per_frame = samples_per_frame
        self._frequency = frequency
        self._seq = 0
        self._sample_index = 0
        self._buffer = bytearray()

    def read(self, max_bytes: int) -> bytes:
        while len(self._buffer) < max_bytes:
            self._buffer.extend(self._next_frame())
        chunk = bytes(self._buffer[:max_bytes])
        del self._buffer[:max_bytes]
        return chunk

    def close(self) -> None:
        return None

    def _next_frame(self) -> bytes:
        samples = bytearray()
        for _ in range(self._samples_per_frame):
            if self._pattern == "counter":
                value = ((self._sample_index & 0xFFFF) ^ 0x8000) - 0x8000
            else:
                phase = 2.0 * math.pi * self._frequency * self._sample_index / self._sample_rate
                value = int(round(26000.0 * math.sin(phase)))
            samples.extend(struct.pack("<h", value))
            self._sample_index += 1

        frame = pack_frame(
            bytes(samples),
            seq=self._seq,
            sample_rate=self._sample_rate,
        )
        self._seq = (self._seq + 1) & 0xFFFF
        return frame


class SerialReader:
    def __init__(self, *, port: str, baud: int, timeout: float = 0.2) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "serial mode requires pyserial in the Python environment that started this GUI.\n"
                f"Current Python: {sys.executable}\n"
                f"Install with: \"{sys.executable}\" -m pip install pyserial"
            ) from exc

        self.source_name = f"serial_{port}"
        self._serial = serial.Serial(port=port, baudrate=baud, timeout=timeout)

    def read(self, max_bytes: int) -> bytes:
        return bytes(self._serial.read(max_bytes))

    def close(self) -> None:
        self._serial.close()


class UsbBulkReader:
    def __init__(
        self,
        *,
        vid: int,
        pid: int,
        ep_in: int | None,
        timeout_ms: int = 200,
        arm_ch569_stream: bool = True,
    ) -> None:
        prepare_libusb_dll_path()
        try:
            import usb.core  # type: ignore
            import usb.util  # type: ignore
        except ImportError as exc:
            raise RuntimeError(
                "usb mode requires pyusb in the Python environment that started this GUI.\n"
                f"Current Python: {sys.executable}\n"
                f"Install with: \"{sys.executable}\" -m pip install pyusb"
            ) from exc

        self._usb_core = usb.core
        self._usb_util = usb.util
        self._timeout_ms = timeout_ms
        self._arm_ch569_stream = arm_ch569_stream
        self.source_name = f"usb_{vid:04x}_{pid:04x}"

        self._dev = usb.core.find(idVendor=vid, idProduct=pid)
        if self._dev is None:
            raise RuntimeError(f"USB device {vid:04x}:{pid:04x} was not found")

        self._dev.set_configuration()
        cfg = self._dev.get_active_configuration()
        intf = cfg[(0, 0)]
        self._ep = self._resolve_endpoint(intf, ep_in)
        try:
            self._dev.clear_halt(self._ep.bEndpointAddress)
        except Exception:
            pass
        if self._arm_ch569_stream:
            self._arm_stream()

    def read(self, max_bytes: int) -> bytes:
        try:
            data = self._dev.read(self._ep.bEndpointAddress, max_bytes, self._timeout_ms)
            return bytes(data)
        except self._usb_core.USBTimeoutError:
            return b""

    def close(self) -> None:
        self._usb_util.dispose_resources(self._dev)

    def _arm_stream(self) -> None:
        self._dev.ctrl_transfer(
            0x40,
            CH569_STREAM_REQUEST,
            CH569_STREAM_START_VALUE,
            0,
            None,
            self._timeout_ms,
        )

    def _resolve_endpoint(self, intf, ep_in: int | None):
        if ep_in is not None:
            endpoint = self._usb_util.find_descriptor(
                intf,
                custom_match=lambda ep: ep.bEndpointAddress == ep_in,
            )
            if endpoint is None:
                raise RuntimeError(f"IN endpoint 0x{ep_in:02x} was not found")
            return endpoint

        endpoint = self._usb_util.find_descriptor(
            intf,
            custom_match=lambda ep: (
                self._usb_util.endpoint_direction(ep.bEndpointAddress)
                == self._usb_util.ENDPOINT_IN
                and self._usb_util.endpoint_type(ep.bmAttributes)
                == self._usb_util.ENDPOINT_TYPE_BULK
            ),
        )
        if endpoint is None:
            raise RuntimeError("No Bulk IN endpoint was found")
        return endpoint


class RateLimitedReader:
    """Wraps a reader to keep fake captures near wall-clock sample rate."""

    def __init__(self, reader: Reader, bytes_per_second: float) -> None:
        self._reader = reader
        self._bytes_per_second = bytes_per_second
        self._started = time.monotonic()
        self._bytes_returned = 0
        self.source_name = reader.source_name

    def read(self, max_bytes: int) -> bytes:
        expected_elapsed = self._bytes_returned / self._bytes_per_second
        actual_elapsed = time.monotonic() - self._started
        if expected_elapsed > actual_elapsed:
            time.sleep(expected_elapsed - actual_elapsed)
        chunk = self._reader.read(max_bytes)
        self._bytes_returned += len(chunk)
        return chunk

    def close(self) -> None:
        self._reader.close()
