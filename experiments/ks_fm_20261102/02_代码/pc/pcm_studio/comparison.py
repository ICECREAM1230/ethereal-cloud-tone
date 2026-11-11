"""Exact mono/stereo PCM WAV comparison with a reference-frame offset."""
from pathlib import Path
import struct
import wave
from .sources import wav_info,expand_pcm


def compare_wavs(capture,reference,reference_offset=0):
    if reference_offset<0:raise ValueError('reference offset must be nonnegative')
    with wave.open(str(capture),'rb') as a,wave.open(str(reference),'rb') as b:
        rate,width=wav_info(a);other_rate,other_width=wav_info(b)
        channels=a.getnchannels()
        if rate!=other_rate:raise ValueError('sample rates differ')
        if channels!=b.getnchannels():raise ValueError('channel counts differ')
        target_width=2 if width==2 else 4
        if target_width!=(2 if other_width==2 else 4):raise ValueError('sample container widths differ')
        frame_bytes=channels*target_width
        unpack=struct.Struct('<'+('h' if target_width==2 else 'i')*channels)
        if not a.getnframes() or b.getnframes()-reference_offset<a.getnframes():
            raise ValueError('empty capture or reference too short')
        b.setpos(reference_offset)
        count=0;compared=0;first=[]
        while True:
            # Comparison checks the actual containers too: a changed valid bit must be reported.
            left=expand_pcm(a.readframes(8192),width,strict=False,channels=channels)
            if not left:
                if compared!=a.getnframes():raise ValueError('truncated capture WAV')
                break
            right=expand_pcm(b.readframes(len(left)//frame_bytes),other_width,strict=False,channels=channels)
            if len(right)!=len(left):raise ValueError('truncated or short reference WAV')
            for index,(actual,expected) in enumerate(zip(unpack.iter_unpack(left),unpack.iter_unpack(right))):
                if actual!=expected:
                    count+=1
                    if len(first)<8:first.append(dict(sample=compared+index,capture=actual,reference=expected))
            compared+=len(left)//frame_bytes
    return dict(status='PASS' if count==0 else 'FAIL',sample_rate=rate,compared_frames=compared,
        mismatched_frames=count,first_mismatches=first,reference_offset=reference_offset,
        capture=str(Path(capture).resolve()),reference=str(Path(reference).resolve()),
        channel_count=channels,container_bits=target_width*8,
        comparison='exact signed PCM containers; sample-frame offset')
