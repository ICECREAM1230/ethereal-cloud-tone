"""Capture/record and analysis workers. No Qt dependency and no GUI blocking."""
from datetime import datetime,timezone
import threading
import time
import numpy as np
from .analysis import RingBuffer,analyze_pcm,report_analysis,dbfs
from .recording import Recorder
from .stream import StreamDecoder


class CaptureSession:
    def __init__(self):
        self._thread=None
        self._lock=threading.RLock()
        self._stats={'status':'IDLE','sample_frames':0,'frames_valid':0,'raw_bytes':0}
        self._snapshot=None
        self.fft_size=8192

    @property
    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def stats(self):
        with self._lock:return dict(self._stats)

    def snapshot(self):
        with self._lock:return self._snapshot

    def set_fft_size(self,size):
        if size not in (2048,8192,32768):raise ValueError('FFT size must be 2048, 8192 or 32768')
        self.fft_size=size

    def start(self,source_factory,output=None,*,duration=0,require_pattern=False):
        if self.running:raise RuntimeError('已有采集正在运行')
        if duration<0 or not np.isfinite(duration):raise ValueError('duration must be finite and nonnegative')
        self._stop=threading.Event();self._analysis_stop=threading.Event()
        self._ring=RingBuffer(131072)
        self._snapshot=None
        self._rate=96008
        self._channels=2;self._width=4;self._bits=24;self._format_known=False
        self._analysis_failure=None
        with self._lock:
            self._stats=dict(status='CONNECTING',sample_frames=0,frames_valid=0,raw_bytes=0,
                trailing_bytes=0,hardware_capture=False,crc_verified=None,dropped=0,
                output=str(output) if output else None,started_utc=datetime.now(timezone.utc).isoformat(),
                device_flash_write=False,require_pattern=bool(require_pattern),usb_timeouts=0)
        self._thread=threading.Thread(target=self._run,args=(source_factory,output,duration,require_pattern),
                                      name='pcm-capture',daemon=False)
        self._thread.start()

    def stop(self):
        if self.running:
            self._stop.set()
            with self._lock:
                if self._stats['status'] in ('RUNNING','CONNECTING'):self._stats['status']='STOPPING'

    def wait(self,timeout=None):
        if self._thread:self._thread.join(timeout)
        return not self.running

    def _analyze(self):
        while not self._analysis_stop.is_set():
            try:self._publish_analysis()
            except Exception as exc:
                with self._lock:self._analysis_failure=str(exc)
                self._stop.set()
                return
            self._analysis_stop.wait(0.1)

    def _publish_analysis(self):
        with self._lock:
            ring,rate,channels,width,bits=self._ring,self._rate,self._channels,self._width,self._bits
        pcm=ring.latest(max(self.fft_size,int(rate*0.1)))
        snapshot=analyze_pcm(pcm,rate,self.fft_size,channels=channels,sample_width=width,valid_sample_bits=bits)
        with self._lock:
            if self._ring is ring:self._snapshot=snapshot

    def _configure_format(self,rate,channels,width,bits):
        with self._lock:
            if self._format_known:
                if (rate,channels,width,bits)!=(self._rate,self._channels,self._width,self._bits):
                    raise ValueError('PCM sample rate/channel count/width changed; start a new recording')
                return False
            self._rate,self._channels,self._width,self._bits=rate,channels,width,bits
            self._ring=RingBuffer(131072,channels=channels,sample_width=width)
            self._snapshot=None;self._format_known=True
            self._stats.update(sample_rate=rate,channel_count=channels,container_bits=width*8,valid_sample_bits=bits)
            return True

    def _run(self,factory,output,duration,require_pattern):
        source=None;rec=None;analyzer=None;decoder=None
        started=time.monotonic();last_payload=started
        sums=np.zeros(2);squares=np.zeros(2);peaks=np.zeros(2)
        full_scale=np.zeros(2,dtype=np.int64)
        terminal='FAIL'
        try:
            if output is not None:rec=Recorder(output)
            source=factory();source.open()
            self._rate=source.sample_rate
            if require_pattern and source.format!='hspi':
                raise ValueError('已知样本检查仅用于测试模式 HSPI 数据；WAV 没有构建信息元数据')
            decoder=StreamDecoder(require_pattern) if source.format=='hspi' else None
            if rec and decoder:rec.open_raw()
            with self._lock:
                self._stats.update(status='RUNNING',source=source.label,input_format=source.format,
                    sample_rate=None if decoder else self._rate,hardware_capture=source.hardware_capture,
                    crc_verified=False if decoder else None,usb_info=getattr(source,'usb_info',None))
            analyzer=threading.Thread(target=self._analyze,name='pcm-analysis',daemon=False)
            analyzer.start()
            # Duration counts from successful opening, rather than driver setup.
            started=time.monotonic();last_payload=started
            while not self._stop.is_set():
                if duration and time.monotonic()-started>=duration:break
                data=source.read(self._stop)
                if data is None:break
                if not data:
                    if source.hardware_capture:
                        with self._lock:self._stats['usb_timeouts']+=1
                        if time.monotonic()-last_payload>3:
                            raise RuntimeError('连续 3 秒没有收到 USB 数据：请检查 FPGA 采集配置、HSPI 连接和设备状态')
                    continue
                last_payload=time.monotonic()
                if decoder:
                    if rec:rec.write_raw(data)
                    with self._lock:self._stats['raw_bytes']+=len(data)
                    frames=decoder.feed(data)
                else:
                    frames=iter([{'pcm':data}])
                for frame in frames:
                    pcm=frame['pcm']
                    if not pcm:continue
                    rate=frame.get('sample_rate',source.sample_rate)
                    channels=frame.get('channels',getattr(source,'channels',2))
                    width=frame.get('sample_width',getattr(source,'sample_width',4))
                    bits=frame.get('valid_sample_bits',getattr(source,'valid_sample_bits',width*8))
                    if self._configure_format(rate,channels,width,bits):
                        sums=np.zeros(channels);squares=np.zeros(channels);peaks=np.zeros(channels)
                        full_scale=np.zeros(channels,dtype=np.int64)
                        if rec:rec.configure(rate,bool(decoder),channels=channels,sample_width=width)
                    if rec:rec.write_pcm(pcm)
                    self._ring.append(pcm)
                    values=np.frombuffer(pcm,dtype=f'<i{width}').reshape(-1,channels).astype(np.float64)/(1<<(width*8-1))
                    sums+=values.sum(axis=0);squares+=(values*values).sum(axis=0)
                    peaks=np.maximum(peaks,np.max(np.abs(values),axis=0))
                    full_scale+=np.count_nonzero((values<=-1)|(values>=1-2**(1-(bits or width*8))),axis=0)
                    with self._lock:
                        self._stats['sample_frames']+=len(pcm)//(channels*width)
                        if decoder:
                            if not self._stats['frames_valid']:self._stats['first_index']=frame['first_index']
                            self._stats.update(last_index=frame['last_index'],build_word=frame['build_word'],
                                dropped=frame['dropped'],crc_verified=frame['crc_verified'],
                                sample_index_verified=frame['sample_index_verified'],
                                protocol_version=frame['protocol_version'],
                                blocks_valid=self._stats.get('blocks_valid',0)+1,
                                frames_valid=self._stats['frames_valid']+frame['frames_count'])
            if not self._stats['sample_frames']:raise ValueError('没有收到有效 PCM 样本')
            terminal='INCOMPLETE' if decoder and decoder.pending_bytes else 'PASS'
        except Exception as exc:
            with self._lock:self._stats['error']=str(exc)
        finally:
            if source:
                try:source.close()
                except Exception as exc:
                    terminal='FAIL'
                    with self._lock:self._stats['error']='设备/文件关闭失败：'+str(exc)
            self._analysis_stop.set()
            if analyzer:analyzer.join()
            try:self._publish_analysis()
            except Exception as exc:
                terminal='FAIL'
                with self._lock:self._stats['error']='分析失败：'+str(exc)
            with self._lock:
                if self._analysis_failure:
                    terminal='FAIL';self._stats['error']='分析线程失败：'+self._analysis_failure
                count=self._stats['sample_frames']
                cumulative=[]
                for ch in range(self._channels if self._format_known else 0):
                    rms=float(np.sqrt(squares[ch]/count)) if count else 0.0
                    cumulative.append(dict(channel='M' if self._channels==1 else 'LR'[ch],peak=float(peaks[ch]),rms=rms,
                        peak_dbfs=dbfs(float(peaks[ch])),rms_dbfs=dbfs(rms),
                        dc=float(sums[ch]/count) if count else 0.0,full_scale_samples=int(full_scale[ch])))
                self._stats.update(status=terminal,ended_utc=datetime.now(timezone.utc).isoformat(),wall_seconds=time.monotonic()-started,
                    trailing_bytes=decoder.pending_bytes if decoder else 0,
                    audio_seconds=count/self._rate,channels=cumulative,
                    display_buffer_frames=self._ring.size,
                    valid_sample_bits=self._bits if self._format_known else None,
                    container_bits=self._width*8 if self._format_known else None,
                    channel_count=self._channels if self._format_known else None)
                if terminal=='INCOMPLETE':self._stats['error']='停止时存在不足 4096 字节的尾包；已保留 RAW 和有效 WAV 前缀'
                if rec:
                    try:rec.finish(self._stats,report_analysis(self._snapshot) if self._snapshot else None)
                    except Exception as exc:self._stats.update(status='FAIL',error='保存报告失败：'+str(exc))
