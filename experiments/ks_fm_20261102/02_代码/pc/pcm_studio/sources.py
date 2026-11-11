"""Input adapters. Creating one never programs, resets or flashes hardware."""
from pathlib import Path
import binascii
import math
import struct
import time
import wave

from .protocol import HEADER, METADATA, BLOCK_BYTES

SAMPLE_RATE=96008


class PacedSource:
    format='hspi'
    sample_rate=SAMPLE_RATE
    hardware_capture=False

    def __init__(self, realtime=True):
        self.realtime=realtime
        self._deadline=None

    def pace(self, samples, stop):
        if not self.realtime:
            return not stop.is_set()
        now=time.monotonic()
        if self._deadline is None:
            self._deadline=now
        if stop.wait(max(0,self._deadline-now)):
            return False
        self._deadline += samples/self.sample_rate
        return True

    def close(self):
        pass


class RawSource(PacedSource):
    def __init__(self,path,realtime=True):
        super().__init__(realtime)
        self.path=Path(path)
        self.label=f'RAW 回放：{self.path.name}'
        self._file=None

    def open(self):
        self._file=self.path.open('rb')
        header=self._file.read(HEADER.size);self._file.seek(0)
        self._samples_per_block=128
        if len(header)==HEADER.size:
            magic,version,flags,_,rate,length,_=HEADER.unpack(header)
            if magic==0xa55a and rate>0:
                self.sample_rate=rate
                if version==1 and flags in (0,3) and length==512:
                    self._samples_per_block=1792 if flags==0 else 448

    def read(self,stop):
        data=self._file.read(16*BLOCK_BYTES)
        if data and not self.pace(max(1,len(data)//BLOCK_BYTES)*self._samples_per_block,stop):return b''
        return data if data else None

    def close(self):
        if self._file:self._file.close()


def wav_info(stream):
    if stream.getnchannels() not in (1,2) or stream.getsampwidth() not in (2,3,4):
        raise ValueError('只支持单/双声道 16、24 或 32 位 PCM WAV')
    if stream.getcomptype()!='NONE' or stream.getframerate()<=0:
        raise ValueError('WAV 必须为未压缩 PCM，采样率必须大于零')
    return stream.getframerate(),stream.getsampwidth()


def expand_pcm(data,width,*,strict=True,channels=2):
    if channels not in (1,2) or width not in (2,3,4) or len(data)%(channels*width):
        raise ValueError('incomplete PCM WAV sample frame')
    if width==2:return data
    if width==4:
        if strict and any(data[::4]):
            raise ValueError('32-bit WAV 的低 8 位不为零，不是本工程的 24-bit/s32 格式')
        return data
    out=bytearray(len(data)//3*4)
    out[1::4]=data[0::3];out[2::4]=data[1::3];out[3::4]=data[2::3]
    return bytes(out)


class WavSource(PacedSource):
    format='pcm'

    def __init__(self,path,realtime=True):
        super().__init__(realtime)
        self.path=Path(path);self.label=f'WAV 回放：{self.path.name}（无传输 CRC）'
        self._wav=None

    def open(self):
        self._wav=wave.open(str(self.path),'rb')
        self.sample_rate,self.width=wav_info(self._wav)
        self.channels=self._wav.getnchannels()
        self.sample_width=4 if self.width==3 else self.width
        self.valid_sample_bits=self.width*8
        self._frames_read=0

    def read(self,stop):
        if not self.pace(2048,stop):return b''
        data=self._wav.readframes(2048)
        if not data:
            if self._frames_read!=self._wav.getnframes():raise ValueError('truncated WAV: fewer samples than declared in header')
            return None
        pcm=expand_pcm(data,self.width,strict=False,channels=self.channels)
        self._frames_read+=len(pcm)//(self.channels*self.sample_width)
        return pcm

    def close(self):
        if self._wav:self._wav.close()


class DemoSource(PacedSource):
    """Known 1 kHz / 1.5 kHz tones encoded through the real packet decoder."""
    label='合成演示：L 1000 Hz / R 1500 Hz（非实板）'

    def open(self):
        self.index=0;self.seq=0

    def read(self,stop):
        if not self.pace(2048,stop):return b''
        blocks=[]
        for _ in range(16):
            pcm=b''.join(struct.pack('<ii',
                round(0.5*8388608*math.sin(2*math.pi*1000*(self.index+i)/SAMPLE_RATE))<<8,
                round(0.25*8388608*math.sin(2*math.pi*1500*(self.index+i)/SAMPLE_RATE))<<8)
                for i in range(128))
            payload=METADATA.pack(self.index,(self.index+127)&0xffffffff,0x13,0)+pcm
            h=HEADER.pack(0xa55a,2,0x93,self.seq,SAMPLE_RATE,len(payload),0)
            frame=h[:12]+struct.pack('<H',binascii.crc_hqx(h+payload,0xffff))+payload
            blocks.append(frame+bytes(BLOCK_BYTES-len(frame)))
            self.index=(self.index+128)&0xffffffff;self.seq=(self.seq+1)&0xffff
        return b''.join(blocks)


def usb_backend():
    import usb.backend.libusb1
    try:
        import libusb_package
        backend=usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
    except ImportError:
        backend=None
    backend=backend or usb.backend.libusb1.get_backend()
    if backend is None:
        raise RuntimeError('未找到 libusb 后端；请运行安装脚本，Windows 还需设备对应的 WinUSB 驱动')
    return backend


def list_usb_devices():
    import usb.core
    devices=usb.core.find(find_all=True,idVendor=0x359f,idProduct=0x3001,backend=usb_backend())
    return [dict(bus=d.bus,address=d.address,label=f'CH569 359f:3001 · bus {d.bus} / address {d.address}')
            for d in devices]


class UsbSource:
    format='hspi'
    sample_rate=SAMPLE_RATE
    hardware_capture=True
    label='CH569 USB 359f:3001 / EP 0x82'

    def __init__(self,bus=None,address=None):
        self.bus=bus;self.address=address;self.device=None;self.claimed=False

    def open(self):
        import usb.core
        import usb.util
        devices=list(usb.core.find(find_all=True,idVendor=0x359f,idProduct=0x3001,backend=usb_backend()))
        if self.address is not None:
            devices=[d for d in devices if d.bus==self.bus and d.address==self.address]
        if not devices:raise RuntimeError('未找到所选 CH569 359f:3001；请刷新设备列表并检查连接')
        if len(devices)>1:raise RuntimeError('发现多个 CH569，请先选择具体设备')
        self.device=devices[0]
        try:
            cfg=self.device.get_active_configuration()
            interface=cfg[(0,0)]
            ep=usb.util.find_descriptor(interface,bEndpointAddress=0x82)
            if ep is None or ep.bmAttributes&3!=2:raise RuntimeError('未找到 Bulk IN 端点 0x82')
            usb.util.claim_interface(self.device,0);self.claimed=True
            self.usb_info=dict(bus=self.device.bus,address=self.device.address,interface=0,
                              endpoint=0x82,read_only=True,start_request_sent=False,
                              max_packet_bytes=getattr(ep,'wMaxPacketSize',None))
        except Exception as exc:
            self.close()
            if isinstance(exc,usb.core.USBError):
                raise RuntimeError(f'USB 打开失败：{exc}。Linux 请检查 USB 访问权限；Windows 请检查 WinUSB 驱动。') from exc
            raise

    def read(self,stop):
        import usb.core
        if stop.is_set():return b''
        try:
            return bytes(self.device.read(0x82,BLOCK_BYTES,timeout=200))
        except usb.core.USBTimeoutError:
            return b''

    def close(self):
        import usb.util
        if self.device is not None:
            try:
                if self.claimed:usb.util.release_interface(self.device,0)
            finally:
                usb.util.dispose_resources(self.device)
                self.device=None;self.claimed=False
