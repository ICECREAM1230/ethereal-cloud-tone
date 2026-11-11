"""Chinese desktop front end. Widgets stay on the Qt main thread."""
from pathlib import Path
import json
import math
import numpy as np
import queue
import shutil
import threading
from datetime import datetime
from uuid import uuid4

from .qt_runtime import configure_qt
configure_qt()

from PySide6.QtCore import QTimer,QUrl,QCoreApplication,QEvent
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import QApplication,QMainWindow,QFileDialog

from .ui.layout import build_window
from .ui.theme import COLORS,stylesheet
from .ui.metrics import derive_metrics

from .session import CaptureSession
from .analysis import report_analysis
from .sources import UsbSource,RawSource,WavSource,DemoSource,list_usb_devices
from .comparison import compare_wavs

ROOT=Path(__file__).resolve().parents[1]
STATUS={'IDLE':'待机','CONNECTING':'连接中','RUNNING':'采集中','STOPPING':'正在保存并停止',
        'PASS':'完成','INCOMPLETE':'数据不完整','FAIL':'失败'}


def text_db(value):
    return '−∞' if value is None else f'{value:.1f}'


class MainWindow(QMainWindow):
    def __init__(self,output_root=None,probe_on_start=True):
        super().__init__()
        self.setWindowTitle('PCM Studio · 音频测量台')
        available=self.screen().availableGeometry()
        self.setMinimumSize(min(860,available.width()-48),min(540,available.height()-70))
        self.resize(min(1260,available.width()-48),min(820,available.height()-70))
        self.move(available.x()+24,available.y()+12)
        self.session=CaptureSession()
        self._queue=queue.SimpleQueue()
        self._last_snapshot=None;self._last_status=None;self._last_log_second=-1
        self._last_meter_snapshot=None;self._peak_holds=[None,None]
        self._close_requested=False;self._comparing=False;self._probing=False
        self._input_wav=None
        self._last_format=None;self._sidebar_collapsed=False
        self._build(output_root or ROOT/'captures')
        self._style()
        self._sync_source()
        self.timer=QTimer(self);self.timer.timeout.connect(self.refresh);self.timer.start(100)
        if probe_on_start:QTimer.singleShot(0,self.probe_usb)

    def _build(self,output_root):
        build_window(self,output_root)
        self.reset_plots()

    def _style(self):
        self.setStyleSheet(stylesheet())

    def toggle_sidebar(self):
        self._sidebar_collapsed=not self._sidebar_collapsed
        collapsed=self._sidebar_collapsed
        self.sidebar.setFixedWidth(48 if collapsed else 162)
        for index,(control,title) in enumerate(zip(self.nav_buttons,self.nav_titles)):
            control.setText(f'{index+1:02d}' if collapsed else f'{index+1:02d}   {title}')
            control.setShortcut(f'Ctrl+{index+1}')
        for widget in self.sidebar_details:widget.setVisible(not collapsed)
        text='展开侧栏' if collapsed else '收起侧栏'
        self.sidebar_toggle_button.setText(text)
        # QAbstractButton.setText clears shortcuts when the text has no mnemonic.
        self.sidebar_toggle_button.setShortcut('Ctrl+B')
        self.sidebar_toggle_button.setAccessibleName(text)
        self.sidebar_toggle_button.setToolTip(text+'，调整绘图可用宽度 · Ctrl+B')
        self.sidebar_toggle_button.setChecked(collapsed)

    def set_frequency_axis(self):
        self.fft_plot.setLogMode(x=self.fft_axis_combo.currentData()=='log',y=False)
        if self._last_snapshot is not None:self._update_spectrum(self._last_snapshot)
        self.reset_plots()

    def _update_spectrum(self,snapshot):
        frequencies=snapshot['frequencies_hz']
        mask=np.isfinite(frequencies)
        if self.fft_axis_combo.currentData()=='log':mask=mask & (frequencies>0)
        for ch,spectrum in enumerate(snapshot['spectrum_dbfs']):
            self.fft_curves[ch].setData(frequencies[mask],spectrum[mask])
        self.update_fft_cursor()

    def update_fft_cursor(self,value=None):
        snapshot=self._last_snapshot
        if snapshot is None or not len(snapshot['frequencies_hz']):
            self.fft_cursor.setVisible(False);self.cursor_label.setText('游标 —');return
        frequencies=snapshot['frequencies_hz']
        valid=np.flatnonzero(np.isfinite(frequencies) &
            ((frequencies>0) if self.fft_axis_combo.currentData()=='log' else (frequencies>=0)))
        if not len(valid):
            self.fft_cursor.setVisible(False);self.cursor_label.setText('游标 —');return
        index=int(valid[np.argmin(np.abs(frequencies[valid]-self.fft_cursor_spin.value()))])
        frequency=float(frequencies[index])
        position=math.log10(frequency) if self.fft_axis_combo.currentData()=='log' else frequency
        self.fft_cursor.setValue(position);self.fft_cursor.setVisible(True)
        channels=len(snapshot['channels'])
        readings=[f"{'M' if channels==1 else 'LR'[ch]} {spectrum[index]:.1f} dBFS"
                  for ch,spectrum in enumerate(snapshot['spectrum_dbfs'])]
        self.cursor_label.setText(f'{frequency:,.1f} Hz · '+ ' / '.join(readings))

    def fft_mouse_moved(self,event):
        position=event[0]
        if not self.fft_plot.sceneBoundingRect().contains(position):return
        x=self.fft_plot.getViewBox().mapSceneToView(position).x()
        frequency=10**max(-12,min(12,x)) if self.fft_axis_combo.currentData()=='log' else x
        self.fft_cursor_spin.setValue(frequency)

    def reset_peak_hold(self):
        self._peak_holds=[None,None]
        snapshot=self.session.snapshot()
        if snapshot is not None and snapshot['sample_frames']:self._update_levels(snapshot)

    def _update_levels(self,snapshot):
        channels=len(snapshot['channels']);derived=derive_metrics(snapshot)
        layout=(channels,snapshot['container_bits'],snapshot['sample_rate'])
        if layout!=self._last_format:
            self._last_format=layout
            name='单声道' if channels==1 else '双声道'
            self.format_label.setText(f"{name} · {snapshot['container_bits']} 位容器 · {snapshot['sample_rate']:,} Hz")
            bits=snapshot.get('valid_sample_bits')
            self.format_label.setToolTip(f"源有效位数：{bits}" if bits else '源有效位数未声明；保留全部容器位。')
            self.channel_names[0].setText('M 单声道' if channels==1 else 'L 左声道')
            self.metric_channel_cards[0].setTitle('M 单声道' if channels==1 else 'L 左声道')
            self.wave_title_label.setText('实时波形 · '+name)
            for ch in range(2):
                visible=ch<channels
                for widget in (self.channel_summary_frames[ch],self.channel_names[ch],self.level_bars[ch],
                               self.level_labels[ch],self.metric_channel_cards[ch]):widget.setVisible(visible)
                self.wave_curves[ch].setVisible(visible);self.fft_curves[ch].setVisible(visible)
                if not visible:self.wave_curves[ch].clear();self.fft_curves[ch].clear()
            for plot in (self.wave_plot,self.fft_plot):plot.plotItem.legend.setVisible(channels==2)
            self.fft_cursor_spin.setMaximum(snapshot['sample_rate']/2)
            self.reset_plots()
        for ch,record in enumerate(snapshot['channels']):
            rms=record['rms_dbfs']
            self.level_bars[ch].setValue(round(max(0,min(100,(rms if rms is not None else -100)+100))*10))
            self.level_labels[ch].setText(f"RMS {text_db(rms)} / Peak {text_db(record['peak_dbfs'])} dBFS · DC {record['dc']:+.5f}")
            self.rms_value_labels[ch].setText(f"{record['rms']:.6f} FS · {text_db(rms)} dBFS")
            self.peak_value_labels[ch].setText(f"{record['peak']:.6f} FS · {text_db(record['peak_dbfs'])} dBFS")
            self.dc_labels[ch].setText(f"{record['dc']:+.6f}")
            self.clip_labels[ch].setText(f"{record['full_scale_samples']:,}")
            crest=derived['channels'][ch]['crest_db']
            self.crest_labels[ch].setText('—' if crest is None else f'{crest:.2f}')
            self._peak_holds[ch]=max(self._peak_holds[ch] or 0,record['peak'])
            hold=self._peak_holds[ch]
            self.hold_labels[ch].setText(f'{20*math.log10(hold):.2f}' if hold else '—')
            bins=record['strongest_bins']
            self.tone_labels[ch].setText(f"{bins[0]['frequency_hz']:,.2f}" if bins and record['spectral_trusted'] else '—')
            self.tone_labels[ch].setToolTip('FFT 主峰频点中心，未插值；AC RMS ≥ −70 dBFS 时可用。')
        self.level_window_label.setText(f"实时电平：最近 {derived['level_window_ms']:.1f} ms（含 DC）；波形：最近 {derived['wave_window_ms']:.1f} ms。滚轮缩放 / 左键拖移。")
        self.metric_window_label.setText(f"电平与峰均比：最近 {derived['level_window_ms']:.1f} ms（含 DC）。峰值保持只覆盖已刷新的电平窗；每次新采集或点击重置时清零。满量程计数不跨重叠窗口累加。")
        self.stereo_window_label.setText(f"最近 {derived['wave_window_ms']:.1f} ms 波形，去 DC；两声道 AC RMS 均须 ≥ −70 dBFS。+1 同相 / −1 反相；电平差正值为右声道较强。")
        correlation,balance=derived['correlation'],derived['balance_db']
        unavailable='不适用（单声道）' if channels==1 else '—（静音 / DC / 低信号）'
        self.correlation_label.setText(f'{correlation:+.4f}' if correlation is not None else unavailable)
        self.balance_label.setText(f'{balance:+.2f}' if balance is not None else unavailable)

    def _update_plots(self,snapshot):
        self._last_snapshot=snapshot
        for ch in range(len(snapshot['channels'])):
            self.wave_curves[ch].setData(snapshot['time_ms'],snapshot['waveform'][:,ch])
        self._update_spectrum(snapshot)
        n=snapshot['fft_size']
        if n:
            channels=len(snapshot['channels']);peaks=[]
            for ch,record in enumerate(snapshot['channels']):
                name='M' if channels==1 else 'LR'[ch];bins=record['strongest_bins']
                peaks.append(f"{name} {bins[0]['frequency_hz']:.1f} Hz（可用）" if bins else f'{name} —（低信号 / 纯 DC）')
            self.fft_label.setText(f"Hann · 去 DC · 单边峰值 dBFS · N={n:,} · Δf={snapshot['frequency_bin_hz']:.2f} Hz\n主峰频点中心："+' / '.join(peaks))
        else:self.fft_label.setText('不足 16 帧样本，暂不计算 FFT。')

    def _sync_record(self):
        recording=self.record_check.isChecked()
        enabled=recording and not self.session.running
        self.output_edit.setEnabled(enabled);self.output_button.setEnabled(enabled)
        self.record_hint.setText('自动保存 RAW、PCM、WAV 和报告。' if recording else '仅实时显示；不保存整段 PCM，也不能事后导出整段 WAV。')

    def _sync_source(self):
        key=self.source_combo.currentData()
        self.source_mode_label.setText({'usb':'USB 实时','raw':'RAW 回放','wav':'WAV 回放','demo':'演示 · 非实板'}[key])
        self.usb_devices.setVisible(key=='usb');self.refresh_button.setVisible(key=='usb')
        self.input_edit.setVisible(key in ('raw','wav'));self.file_button.setVisible(key in ('raw','wav'))
        self.pattern_check.setEnabled(key in ('usb','raw'))
        if key not in ('usb','raw'):self.pattern_check.setChecked(False)

    def probe_usb(self):
        if self._probing:return
        self._probing=True;self.refresh_button.setEnabled(False)
        def run():
            try:self._queue.put(('devices',list_usb_devices()))
            except Exception as exc:self._queue.put(('probe_error',str(exc)))
        threading.Thread(target=run,name='usb-discovery',daemon=True).start()

    def select_input(self):
        filt='HSPI RAW (*.bin *.raw)' if self.source_combo.currentData()=='raw' else 'PCM WAV (*.wav)'
        path,_=QFileDialog.getOpenFileName(self,'选择输入文件','',filt)
        if path:self.input_edit.setText(path)

    def select_output(self):
        path=QFileDialog.getExistingDirectory(self,'选择结果目录',self.output_edit.text())
        if path:self.output_edit.setText(path)

    def select_reference(self):
        path,_=QFileDialog.getOpenFileName(self,'选择仿真参考 WAV','','PCM WAV (*.wav)')
        if path:self.reference_edit.setText(path)

    def start_capture(self):
        if self.session.running:return
        key=self.source_combo.currentData();path=Path(self.input_edit.text())
        device=self.usb_devices.currentData()
        if key=='usb':factory=lambda:UsbSource(**(device or {}))
        elif key=='raw':factory=lambda:RawSource(path)
        elif key=='wav':factory=lambda:WavSource(path)
        else:factory=lambda:DemoSource()
        self._input_wav=path if key=='wav' else None
        output=None
        if self.record_check.isChecked():
            output=Path(self.output_edit.text())/f'{datetime.now():%Y%m%d_%H%M%S}_{key}_{uuid4().hex[:6]}'
        self._last_snapshot=None;self._last_status=None;self._last_log_second=-1
        self._last_meter_snapshot=None;self._last_format=None;self._peak_holds=[None,None]
        for labels in (self.rms_value_labels,self.peak_value_labels,self.dc_labels,self.clip_labels,
                       self.crest_labels,self.hold_labels,self.tone_labels):
            for widget in labels:widget.setText('—')
        for bar in self.level_bars:bar.setValue(0)
        for widget in self.level_labels:widget.setText('RMS — dBFS · Peak — dBFS · DC —')
        self.correlation_label.setText('—');self.balance_label.setText('—')
        self.format_label.setText('等待输入格式');self.format_label.setToolTip('')
        self.wave_title_label.setText('实时波形')
        self.fft_label.setText('等待足够样本；静音或纯 DC 不报告可信音调。')
        self.fft_cursor.setVisible(False);self.cursor_label.setText('游标 —')
        self.freeze_check.setChecked(False)
        for curve in self.wave_curves+self.fft_curves:curve.clear()
        self.session.start(factory,output,duration=self.duration_spin.value(),require_pattern=self.pattern_check.isChecked())
        self.refresh()

    def reset_plots(self):
        rate=self.session.stats().get('sample_rate') or 96008
        self.wave_plot.setXRange(0,40,padding=0);self.wave_plot.setYRange(-1.05,1.05,padding=0)
        upper=rate/2 if self.full_band.isChecked() else min(24000,rate/2)
        if self.fft_axis_combo.currentData()=='log':
            snapshot=self._last_snapshot or self.session.snapshot()
            frequencies=snapshot['frequencies_hz'] if snapshot is not None else np.empty(0)
            positive=frequencies[frequencies>0]
            lower=float(positive[0]) if len(positive) else 1.0
            self.fft_plot.setXRange(math.log10(min(lower,upper/2)),math.log10(upper),padding=0)
        else:self.fft_plot.setXRange(0,upper,padding=0)
        self.fft_plot.setYRange(-140,3,padding=0)

    def _capture_wav(self):
        output=self.session.stats().get('output')
        if output:
            path=Path(output)/'audio.wav'
            if path.is_file():return path
        return self._input_wav if self._input_wav and self._input_wav.is_file() else None

    def open_output(self):
        output=self.session.stats().get('output')
        if output and Path(output).is_dir():QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(output).resolve())))

    def export_wav(self):
        source=self._capture_wav()
        if not source or self.session.running:return
        path,_=QFileDialog.getSaveFileName(self,'另存本次完整 WAV',str(source),'PCM WAV (*.wav)')
        if not path:return
        try:
            target=Path(path)
            if target.resolve()!=source.resolve():shutil.copyfile(source,target)
            self.log.appendPlainText('已导出 WAV：'+str(target))
        except Exception as exc:self.log.appendPlainText('导出 WAV 失败：'+str(exc))

    def export_analysis(self):
        snapshot=self.session.snapshot()
        if snapshot is None:return
        path,_=QFileDialog.getSaveFileName(self,'导出当前分析','analysis_snapshot.json','JSON (*.json)')
        if not path:return
        try:
            report=report_analysis(snapshot)
            report.update(source=self.session.stats().get('source'),
                frequencies_hz=snapshot['frequencies_hz'].tolist(),
                spectrum_dbfs=[v.tolist() for v in snapshot['spectrum_dbfs']])
            Path(path).write_text(json.dumps(report,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
            self.log.appendPlainText('已导出当前窗口分析：'+path)
        except Exception as exc:self.log.appendPlainText('分析导出失败：'+str(exc))

    def compare_reference(self):
        actual=self._capture_wav();reference=Path(self.reference_edit.text());offset=self.offset_spin.value()
        if not actual or self.session.running or self._comparing:return
        self._comparing=True;self.compare_button.setEnabled(False);self.compare_result.setPlainText('正在逐样本比较…')
        def run():
            try:self._queue.put(('comparison',compare_wavs(actual,reference,offset)))
            except Exception as exc:self._queue.put(('comparison',dict(status='FAIL',error=str(exc))))
        threading.Thread(target=run,name='pcm-comparison',daemon=True).start()

    def refresh(self):
        while not self._queue.empty():
            kind,value=self._queue.get()
            if kind in ('devices','probe_error'):
                self._probing=False;self.usb_devices.clear()
                if kind=='devices' and value:
                    for d in value:self.usb_devices.addItem(d['label'],dict(bus=d['bus'],address=d['address']))
                else:
                    self.usb_devices.addItem('未找到 CH569 359f:3001',None)
                    self.log.appendPlainText('USB：'+(value if kind=='probe_error' else '未发现设备'))
            elif kind=='comparison':
                self._comparing=False;self.compare_result.setPlainText(json.dumps(value,ensure_ascii=False,indent=2))
                output=self.session.stats().get('output')
                if output:
                    try:(Path(output)/'comparison.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                    except Exception as exc:self.log.appendPlainText('对照报告保存失败：'+str(exc))
        running=self.session.running;stats=self.session.stats();status=stats['status']
        self.start_button.setEnabled(not running and not self._comparing)
        self.stop_button.setEnabled(running and status!='STOPPING')
        for control in (self.source_combo,self.input_edit,self.file_button,self.usb_devices,
                        self.duration_spin,self.record_check,self.pattern_check):control.setEnabled(not running)
        self.refresh_button.setEnabled(not running and not self._probing)
        if not running:self._sync_source()
        self._sync_record()
        self.status_label.setText(STATUS.get(status,status))
        self.status_label.setStyleSheet(f"color:{COLORS['error'] if status in ('FAIL','INCOMPLETE') else COLORS['text']};")
        self.source_label.setText(stats.get('source','选择输入后开始；演示和文件回放不会访问 USB。'))
        rate=stats.get('sample_rate');count=stats.get('sample_frames',0)
        for widget,text in zip(self.metric_values,(f'{rate:,} Hz' if rate else '—',f'{count/rate:.3f} s' if rate else '0.000 s',f'{count:,}',f"{stats.get('frames_valid',0):,}")):
            widget.setText(text)
        second=int(stats.get('wall_seconds',0)) if not running else count//max(1,rate or 1)
        if status!=self._last_status or second!=self._last_log_second:
            self.log.setPlainText(json.dumps(stats,ensure_ascii=False,indent=2))
            self._last_status=status;self._last_log_second=second
        if stats.get('error'):
            self.quality_label.setText('异常：'+stats['error'])
        elif stats.get('input_format')=='hspi':
            origin='USB 实采' if stats.get('hardware_capture') else '回放／演示帧'
            if stats.get('protocol_version')==1:
                self.quality_label.setText(f'{origin} · v1 序号连续性已校验 · 应用 CRC、FPGA 样本索引及丢样计数未提供')
            elif stats.get('protocol_version')==2:
                self.quality_label.setText(f"{origin} · v2 CRC / 序号 / 样本索引已校验 · 丢样 {stats['dropped']} · 构建标识 0x{stats['build_word']:02X}")
            else:self.quality_label.setText(f'{origin} · 等待首个完整 PCM 数据块')
        elif stats.get('input_format')=='pcm':self.quality_label.setText('WAV 文件回放：不包含 USB / HSPI 传输 CRC，不能据此验证实板传输。')
        snapshot=self.session.snapshot()
        if snapshot is not None and snapshot['sample_frames'] and snapshot is not self._last_meter_snapshot:
            self._last_meter_snapshot=snapshot
            self._update_levels(snapshot)
        if snapshot is not None and snapshot['sample_frames'] and snapshot is not self._last_snapshot and not self.freeze_check.isChecked():
            self._update_plots(snapshot)
        self.display_state_label.setText('图形已暂停 · 电平仍实时更新' if self.freeze_check.isChecked() else '图形实时更新')
        self.open_button.setEnabled(bool(stats.get('output')))
        self.wav_button.setEnabled(not running and self._capture_wav() is not None)
        self.analysis_button.setEnabled(snapshot is not None)
        self.compare_button.setEnabled(not running and not self._comparing and self._capture_wav() is not None)
        if self._close_requested and not running:
            self.timer.stop();self.close()

    def closeEvent(self,event):
        if self.session.running:
            event.ignore();self._close_requested=True;self.session.stop()
        else:
            self.timer.stop();event.accept()


def main(output_root=None,demo=False,duration=10):
    app=QApplication.instance() or QApplication([])
    app.setApplicationName('PCM Studio')
    app.setStyle('Fusion')
    font=app.font();font.setPointSize(10);app.setFont(font)
    window=MainWindow(output_root=output_root,probe_on_start=not demo);window.show()
    window.duration_spin.setValue(duration)
    if demo:
        window.source_combo.setCurrentIndex(window.source_combo.findData('demo'))
        QTimer.singleShot(0,window.start_capture)
    app.aboutToQuit.connect(window.session.stop)
    try:
        return app.exec()
    finally:
        dispose_window(window,app)


def dispose_window(window,app):
    # Destroy graphs while QApplication still owns a valid Qt runtime.
    window.session.stop();window.session.wait()
    window.timer.stop();window.deleteLater()
    QCoreApplication.sendPostedEvents(None,QEvent.Type.DeferredDelete)
    app.processEvents()
