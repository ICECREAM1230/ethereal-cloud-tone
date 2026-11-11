"""Incremental, exclusive recordings. Never buffer a full capture in memory."""
from pathlib import Path
import json
import time
import wave

MAX_WAV_PCM_BYTES=0xffffffff-36


class Recorder:
    def __init__(self,path):
        self.path=Path(path)
        self.path.mkdir(parents=True,exist_ok=False)
        self.raw=None;self.pcm=None;self.audio_file=None;self.wav=None
        self.pcm_bytes=0;self._last_flush=time.monotonic()

    def open_raw(self):
        if self.raw is None:self.raw=(self.path/'raw.bin').open('xb')

    def configure(self,rate,raw,*,channels=2,sample_width=4):
        if raw:self.open_raw()
        self.pcm=(self.path/'payload.pcm').open('xb')
        self.audio_file=(self.path/'audio.wav').open('xb')
        self.wav=wave.open(self.audio_file,'wb')
        self.wav.setnchannels(channels);self.wav.setsampwidth(sample_width);self.wav.setframerate(rate)

    def write_raw(self,data):
        if self.raw:self.raw.write(data)

    def write_pcm(self,data):
        if self.pcm_bytes+len(data)>MAX_WAV_PCM_BYTES:
            raise ValueError('达到标准 RIFF WAV 的 4 GiB 上限，请开始新录制')
        self.pcm.write(data);self.wav.writeframesraw(data)
        self.pcm_bytes+=len(data)
        if time.monotonic()-self._last_flush>=1:
            # Keep WAV header useful if a subsequent external interruption occurs.
            self.wav.writeframes(b'')
            for f in (self.raw,self.pcm,self.audio_file):
                if f:f.flush()
            self._last_flush=time.monotonic()

    def finish(self,stats,analysis=None):
        errors=[]
        if self.wav:
            try:self.wav.close()
            except Exception as exc:errors.append(str(exc))
        for f in (self.raw,self.pcm,self.audio_file):
            if f:
                try:f.close()
                except Exception as exc:errors.append(str(exc))
        if errors:
            stats.update(status='FAIL',error='文件收尾失败：'+'; '.join(errors))
        if analysis is not None:
            (self.path/'analysis.json').write_text(json.dumps(analysis,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
        (self.path/'stats.json').write_text(json.dumps(stats,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
