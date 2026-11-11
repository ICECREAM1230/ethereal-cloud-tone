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
        channels: int = 1,
        sample_width: int = 2,
    ) -> None:
        self.source_name = f"fake_{pattern}"
        self._channels,self._sample_width=channels,sample_width
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
            if self._sample_width==2:
                samples.extend(struct.pack('<h',value))
                if self._channels==2:samples.extend(struct.pack('<h',-value if value!=-32768 else 32767))
            else:
                if self._pattern=='counter':
                    value=(((self._sample_index & 0xffffff)^0x800000)-0x800000)<<8
                else:value=value<<16
                samples.extend(struct.pack('<i',value))
                if self._channels==2:samples.extend(struct.pack('<i',-value-256))
            self._sample_index += 1

        frame = pack_frame(
            bytes(samples),
            seq=self._seq,
            sample_rate=self._sample_rate,
            flags=(1 if self._channels==2 else 0)|(2 if self._sample_width==4 else 0),
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
        arm_ch569_stream: bool | None = None,
        require_superspeed: bool = False,
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
        self._arm_ch569_stream = (not sys.platform.startswith('linux')
                                  if arm_ch569_stream is None else arm_ch569_stream)
        self.source_name = f"usb_{vid:04x}_{pid:04x}"

        self._dev = usb.core.find(idVendor=vid, idProduct=pid)
        if self._dev is None:
            raise RuntimeError(f"USB device {vid:04x}:{pid:04x} was not found")

        self._claimed_interface = None
        self._prefetched = bytearray()
        try:
            # Follow the reference reader's current platform-specific setup.
            # Its Linux path retains the enumerated configuration/SS state.
            if sys.platform.startswith('linux'):
                try:
                    cfg = self._dev.get_active_configuration()
                except self._usb_core.USBError as exc:
                    if 'Configuration not set' not in str(exc):
                        raise
                    self._dev.set_configuration()
                    cfg = self._dev.get_active_configuration()
                intf = cfg[(0, 0)]
                self._usb_util.claim_interface(self._dev, intf.bInterfaceNumber)
                self._claimed_interface = intf.bInterfaceNumber
            else:
                self._dev.set_configuration()
                cfg = self._dev.get_active_configuration()
                intf = cfg[(0, 0)]
            self._ep = self._resolve_endpoint(intf, ep_in)
            if (self._usb_util.endpoint_direction(self._ep.bEndpointAddress) != self._usb_util.ENDPOINT_IN or
                    self._usb_util.endpoint_type(self._ep.bmAttributes) != self._usb_util.ENDPOINT_TYPE_BULK):
                raise RuntimeError('Selected endpoint must be Bulk IN')
            speed = getattr(self._dev, 'speed', None)
            speed_name = {1: 'LowSpeed', 2: 'FullSpeed', 3: 'HighSpeed USB2',
                          4: 'SuperSpeed USB3', 5: 'SuperSpeedPlus USB3'}.get(speed, 'Unknown')
            self.link_info = {'vid': vid, 'pid': pid, 'endpoint': self._ep.bEndpointAddress,
                              'interface': 0, 'alternate': 0, 'speed_code': speed,
                              'speed': speed_name, 'super_speed': speed in (4, 5),
                              'max_packet_bytes': self._ep.wMaxPacketSize,
                              'arm_request': [0x40, 0x8f, 0x007f, 0],
                              'arm_policy': ('if_idle' if sys.platform.startswith('linux') else 'on_open')
                                            if self._arm_ch569_stream else 'read_only',
                              'start_request_sent': False}
            if require_superspeed and speed not in (4, 5):
                raise RuntimeError(f'USB3 SuperSpeed required; actual link: {speed_name}. Check USB3 port/cable.')
            if not sys.platform.startswith('linux'):
                try:
                    self._dev.clear_halt(self._ep.bEndpointAddress)
                except self._usb_core.USBError:
                    pass
            if self._arm_ch569_stream:
                if sys.platform.startswith('linux'):
                    # Preserve a running queue. Explicit cold-start mode sends
                    # the original START only after one timed-out receive.
                    try:
                        self._prefetched.extend(self._dev.read(
                            self._ep.bEndpointAddress, 4096, self._timeout_ms))
                    except self._usb_core.USBTimeoutError:
                        self._arm_stream()
                else:
                    self._arm_stream()
        except Exception:
            self.close()
            raise

    def read(self, max_bytes: int) -> bytes:
        if self._prefetched:
            data = bytes(self._prefetched[:max_bytes])
            del self._prefetched[:max_bytes]
            return data
        try:
            data = self._dev.read(self._ep.bEndpointAddress, max_bytes, self._timeout_ms)
            return bytes(data)
        except self._usb_core.USBTimeoutError:
            return b""

    def close(self) -> None:
        if self._dev is None:
            return
        try:
            if self._claimed_interface is not None:
                self._usb_util.release_interface(self._dev, self._claimed_interface)
        finally:
            self._usb_util.dispose_resources(self._dev)
            self._dev = None
            self._claimed_interface = None

    def _arm_stream(self) -> None:
        self._dev.ctrl_transfer(
            0x40,
            CH569_STREAM_REQUEST,
            CH569_STREAM_START_VALUE,
            0,
            None,
            self._timeout_ms,
        )
        self.link_info['start_request_sent'] = True

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
