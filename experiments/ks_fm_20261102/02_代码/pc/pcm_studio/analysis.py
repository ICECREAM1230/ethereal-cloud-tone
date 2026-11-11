"""Bounded display buffer and Hann amplitude spectrum, independent of Qt/USB."""
from collections import deque
import math
import threading
import numpy as np


class RingBuffer:
    def __init__(self, capacity=131072, *, channels=2, sample_width=4):
        if capacity < 1:
            raise ValueError('ring capacity must be positive')
        self.capacity = capacity
        if channels not in (1,2) or sample_width not in (2,4):
            raise ValueError('unsupported PCM channel count or sample width')
        self.frame_bytes=channels*sample_width
        self._chunks = deque()
        self._bytes = 0
        self._lock = threading.Lock()

    @property
    def size(self):
        with self._lock:
            return self._bytes // self.frame_bytes

    def append(self, pcm):
        if len(pcm) % self.frame_bytes:
            raise ValueError('unaligned PCM sample frame')
        if not pcm:
            return
        pcm = bytes(pcm[-self.capacity*self.frame_bytes:])
        with self._lock:
            self._chunks.append(pcm)
            self._bytes += len(pcm)
            excess = self._bytes - self.capacity*self.frame_bytes
            while excess > 0:
                first = self._chunks.popleft()
                count = min(excess, len(first))
                if count < len(first):
                    self._chunks.appendleft(first[count:])
                self._bytes -= count
                excess -= count

    def latest(self, frames=None):
        with self._lock:
            count = self._bytes if frames is None else min(self._bytes, max(0,frames)*self.frame_bytes)
            parts = []
            for chunk in reversed(self._chunks):
                if not count:
                    break
                take = min(count,len(chunk))
                parts.append(chunk[-take:])
                count -= take
            return b''.join(reversed(parts))


def dbfs(value):
    return 20*math.log10(value) if value > 0 else None


def analyze_pcm(pcm, sample_rate, fft_size=8192, *, channels=2, sample_width=4,valid_sample_bits=24):
    if fft_size < 16 or fft_size & (fft_size-1):
        raise ValueError('FFT size must be a power of two >=16')
    if channels not in (1,2) or sample_width not in (2,4):raise ValueError('unsupported PCM format')
    if sample_rate <= 0 or len(pcm) % (channels*sample_width):
        raise ValueError('invalid sample rate or PCM alignment')
    scale=float(1<<(sample_width*8-1))
    values = np.frombuffer(pcm,dtype=f'<i{sample_width}').reshape(-1,channels).astype(np.float64) / scale
    count = len(values)
    result = dict(sample_frames=count, sample_rate=sample_rate, valid_sample_bits=valid_sample_bits,
                  container_bits=sample_width*8,channel_count=channels,fft_size=0,channels=[],window='Hann; latest window; DC removed',
                  spectrum_units='single-sided peak amplitude dBFS, not PSD')
    n = min(fft_size,1 << (count.bit_length()-1)) if count else 0
    meter_values=values[-max(1,int(sample_rate*0.1)):]
    result['level_window_frames']=len(meter_values)
    for ch in range(channels):
        v=meter_values[:,ch]
        peak=float(np.max(np.abs(v))) if count else 0.0
        rms=float(np.sqrt(np.mean(v*v))) if count else 0.0
        dc=float(np.mean(v)) if count else 0.0
        clips=int(np.count_nonzero((v <= -1.0) | (v >= 1-2**(1-(valid_sample_bits or sample_width*8)))))
        result['channels'].append(dict(channel='M' if channels==1 else 'LR'[ch],peak=peak,rms=rms,dc=dc,
            peak_dbfs=dbfs(peak),rms_dbfs=dbfs(rms),full_scale_samples=clips,
            ac_rms_dbfs=None,spectral_trusted=False,strongest_bins=[]))
    visible = min(count,max(1,int(sample_rate*0.04)))
    result['waveform'] = values[-visible:].copy() if visible else np.empty((0,channels))
    result['time_ms'] = np.arange(visible,dtype=float)/sample_rate*1000
    result['frequencies_hz'] = np.empty(0)
    result['spectrum_dbfs'] = [np.empty(0) for _ in range(channels)]
    if n < 16:
        return result
    window=np.hanning(n)
    segment=values[-n:]
    centered=segment-segment.mean(axis=0)
    amplitude=np.abs(np.fft.rfft(centered*window[:,None],axis=0))*2/window.sum()
    amplitude[0] *= 0.5
    amplitude[-1] *= 0.5
    freqs=np.fft.rfftfreq(n,1/sample_rate)
    result.update(fft_size=n,frequency_bin_hz=sample_rate/n,frequencies_hz=freqs,
                  spectrum_dbfs=[20*np.log10(np.maximum(amplitude[:,i],1e-8)) for i in range(channels)])
    for ch,record in enumerate(result['channels']):
        ac=float(np.sqrt(np.mean(centered[:,ch]**2)))
        trusted=ac >= 10**(-70/20)
        bins=np.where((amplitude[1:-1,ch] > amplitude[:-2,ch]) &
                      (amplitude[1:-1,ch] >= amplitude[2:,ch]))[0]+1
        strongest=sorted(bins,key=lambda k:amplitude[k,ch],reverse=True)[:5]
        record.update(ac_rms_dbfs=dbfs(ac),spectral_trusted=trusted,
            strongest_bins=[dict(frequency_hz=float(freqs[k]),amplitude_dbfs=dbfs(float(amplitude[k,ch])))
                            for k in strongest] if trusted else [])
    return result


def report_analysis(snapshot):
    """JSON summary; the full spectrum is shown in the UI, never queued per frame."""
    return {k:v for k,v in snapshot.items()
            if k not in ('waveform','time_ms','frequencies_hz','spectrum_dbfs')}
