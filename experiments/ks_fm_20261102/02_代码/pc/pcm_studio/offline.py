#!/usr/bin/env python3
"""Mono/stereo PCM levels and a bounded Hann-window FFT."""
import argparse,cmath,json,math,struct,wave
from pathlib import Path
from .protocol import levels
from .sources import wav_info,expand_pcm

def fft(values):
    n=len(values)
    if n<2:return list(values)
    even=fft(values[0::2]);odd=fft(values[1::2])
    roots=[cmath.exp(-2j*math.pi*k/n)*odd[k] for k in range(n//2)]
    return [even[k]+roots[k] for k in range(n//2)]+[even[k]-roots[k] for k in range(n//2)]

def analyze(path,size=8192):
    with wave.open(str(path),'rb') as stream:
        rate,width=wav_info(stream);channels=stream.getnchannels();count=stream.getnframes()
        pcm=expand_pcm(stream.readframes(count),width,strict=False,channels=channels)
    container_width=2 if width==2 else 4
    if len(pcm)!=count*channels*container_width:raise ValueError('truncated WAV')
    pairs=list(struct.iter_unpack('<'+('h' if container_width==2 else 'i')*channels,pcm))
    if not pairs:raise ValueError('empty WAV')
    n=1<<min(size.bit_length()-1,len(pairs).bit_length()-1)
    if n<16:raise ValueError('at least 16 sample frames required for FFT')
    start=(len(pairs)-n)//2
    result=levels(pcm,channels=channels,sample_width=container_width)
    result.update(sample_rate=rate,fft_size=n,frequency_bin_hz=rate/n,
                  window='Hann; centered analysis window',valid_sample_bits=width*8,
                  container_bits=container_width*8,channel_count=channels)
    window=[0.5-0.5*math.cos(2*math.pi*i/(n-1)) for i in range(n)]
    for channel,record in enumerate(result['channels']):
        values=[pair[channel]/(1<<(container_width*8-1)) for pair in pairs[start:start+n]]
        dc=sum(values)/n
        spectrum=fft([(v-dc)*w for v,w in zip(values,window)])
        normalizer=2/sum(window)
        amplitudes=[abs(v)*normalizer for v in spectrum[:n//2+1]]
        ac_rms=math.sqrt(sum((v-dc)**2 for v in values)/n)
        trusted=ac_rms>=10**(-70/20)
        peaks=sorted(range(1,len(amplitudes)-1),key=lambda k:amplitudes[k],reverse=True)[:5]
        record.update(ac_rms_dbfs=20*math.log10(ac_rms) if ac_rms else None,spectral_trusted=trusted,
            strongest_bins=[dict(frequency_hz=k*rate/n,amplitude_dbfs=20*math.log10(amplitudes[k]) if amplitudes[k] else None) for k in peaks] if trusted else [])
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('wav',type=Path)
    p.add_argument('--fft-size',type=int,default=8192);p.add_argument('--out',type=Path)
    a=p.parse_args()
    if a.fft_size<16 or a.fft_size & (a.fft_size-1):p.error('FFT size must be a power of two >=16')
    text=json.dumps(analyze(a.wav,a.fft_size),ensure_ascii=False,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
