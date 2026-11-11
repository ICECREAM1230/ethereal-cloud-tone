"""Strict 4KB decoding for official v1 PCM and the existing v2 profile."""
from __future__ import annotations
import binascii
import math
import struct
import wave
from pathlib import Path

BLOCK_BYTES = 4096
HEADER = struct.Struct('<HBBHIHH')
METADATA = struct.Struct('<IIII')
SAMPLES_PER_BLOCK = 128
PAYLOAD_BYTES = 16 + SAMPLES_PER_BLOCK * 8

def decode_block(block: bytes) -> dict:
    if len(block) != BLOCK_BYTES:
        raise ValueError('incomplete 4096-byte HSPI block')
    if block[2] == 1:
        return decode_v1_block(block)
    magic, version, flags, seq, rate, length, crc = HEADER.unpack_from(block)
    if (magic, version, flags, length) != (0xA55A, 2, 0x93, PAYLOAD_BYTES):
        raise ValueError('unexpected monitor header/version/format/length')
    if rate != 96008:
        raise ValueError(f'profile sample rate mismatch: {rate}')
    frame_end = HEADER.size + length
    protected = block[:12] + b'\0\0' + block[14:frame_end]
    if binascii.crc_hqx(protected, 0xFFFF) != crc:
        raise ValueError('application CRC-16/CCITT-FALSE mismatch')
    if any(block[frame_end:]):
        raise ValueError('nonzero block padding')
    first, last, build_word, dropped = METADATA.unpack_from(block, HEADER.size)
    if (last-first) & 0xFFFFFFFF != SAMPLES_PER_BLOCK-1:
        raise ValueError('input sample index gap inside block')
    if build_word & 0x0F != 3 or not build_word & 0x10:
        raise ValueError('build is not subtractive+KS+capture only')
    if dropped:
        raise ValueError(f'FPGA input FIFO overflow: {dropped} samples')
    pcm = block[HEADER.size+METADATA.size:frame_end]
    if any(pcm[i] for i in range(0, len(pcm), 4)):
        raise ValueError('nonzero low eight bits in native-24/s32 container')
    return dict(seq=seq, sample_rate=rate, first_index=first,
                last_index=last, build_word=build_word, dropped=dropped, pcm=pcm,
                protocol_version=2,channels=2,sample_width=4,valid_sample_bits=24,
                frames_count=1,crc_verified=True,sample_index_verified=True)


def decode_v1_block(block: bytes) -> dict:
    """Seven526-byte frames, then414 zero bytes; samples remain unmodified."""
    pcm=[];first=None;previous_seq=None;profile=None
    for offset in range(0,7*526,526):
        magic,version,flags,seq,rate,length,crc=HEADER.unpack_from(block,offset)
        if magic!=0xa55a or version!=1 or flags not in (0,3) or length!=512 or rate<=0:
            raise ValueError('invalid v1 PCM header/version/format/length/rate')
        if crc:
            raise ValueError('unsupported nonzero v1 CRC field; checksum not verified')
        current=(flags,rate)
        if profile is not None and profile!=current:
            raise ValueError('PCM format or sample rate changed inside v1 block')
        if previous_seq is not None and seq!=(previous_seq+1)&65535:
            raise ValueError('v1 frame sequence gap/repeat/reorder')
        if first is None:first=seq
        previous_seq=seq;profile=current
        pcm.append(block[offset+HEADER.size:offset+526])
    if any(block[7*526:]):raise ValueError('nonzero v1 block padding')
    flags,rate=profile
    channels,width=(1,2) if flags==0 else (2,4)
    return dict(seq=first,last_seq=previous_seq,sample_rate=rate,pcm=b''.join(pcm),
                protocol_version=1,flags=flags,channels=channels,sample_width=width,
                valid_sample_bits=16 if width==2 else None,frames_count=7,crc_verified=False,
                sample_index_verified=False,first_index=None,last_index=None,
                build_word=None,dropped=None)

def pattern_pair(index: int) -> tuple[int, int]:
    left = (index * 17 + 0x13579B) & 0xFFFFFF
    right = left ^ 0xFFFFFF
    return tuple((v if v < 0x800000 else v-0x1000000) << 8 for v in (left, right))

def validate_frames(frames: list[dict], *, require_pattern: bool = False) -> bytes:
    previous = None
    pcm = bytearray()
    for frame in frames:
        if previous is not None:
            if frame['seq'] != (previous['seq']+1) & 0xFFFF:
                raise ValueError('USB block sequence gap/repeat/reorder')
            if frame['first_index'] != (previous['last_index']+1) & 0xFFFFFFFF:
                raise ValueError('input sample index gap between blocks')
            if frame['build_word'] != previous['build_word']:
                raise ValueError('build word changed inside one capture')
        if require_pattern:
            if not frame['build_word'] & 0x20:
                raise ValueError('pattern check requires build word bit 5')
            for i, pair in enumerate(struct.iter_unpack('<ii', frame['pcm'])):
                if pair != pattern_pair((frame['first_index']+i) & 0xFFFFFFFF):
                    raise ValueError(f'pattern mismatch at sample {frame["first_index"]+i}')
        pcm.extend(frame['pcm'])
        previous = frame
    return bytes(pcm)

def write_wav(path: Path, pcm: bytes, rate: int = 96008) -> None:
    with wave.open(str(path), 'wb') as stream:
        stream.setnchannels(2)
        stream.setsampwidth(4)
        stream.setframerate(rate)
        stream.writeframes(pcm)

def read_wav(path: Path) -> tuple[int, bytes]:
    with wave.open(str(path), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth()) != (2, 4):
            raise ValueError('expected stereo signed-32 WAV containing left-aligned native 24-bit PCM')
        return stream.getframerate(), stream.readframes(stream.getnframes())

def levels(pcm: bytes, *, channels=2,sample_width=4) -> dict:
    pairs = list(struct.iter_unpack('<'+('h' if sample_width==2 else 'i')*channels,pcm))
    result = {'sample_frames': len(pairs), 'channels': []}
    for channel in range(channels):
        values = [pair[channel] / (1<<(sample_width*8-1)) for pair in pairs]
        rms = math.sqrt(sum(v*v for v in values)/len(values)) if values else 0.0
        peak = max(map(abs, values), default=0.0)
        result['channels'].append(dict(channel='M' if channels==1 else 'LR'[channel], peak=peak, rms=rms,
            peak_dbfs=20*math.log10(peak) if peak else None,
            rms_dbfs=20*math.log10(rms) if rms else None,
            dc=sum(values)/len(values) if values else 0.0))
    return result
