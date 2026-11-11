#!/usr/bin/env python3
"""Offline Chinese report figures from unchanged KS/FM PCM recordings.

The only writes are the selected figure directory. No hardware interfaces are
imported. PNG uses 4800x3000 pixels at 600 dpi; PDF contains vector plot paths
and searchable embedded Chinese text.
"""
from pathlib import Path
import argparse
import hashlib
import html
import re
import json
import math
import os
import sys
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
PC = Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
FS24 = 8388608.0
WIDTH, HEIGHT, DPI = 4800, 3000, 600
NFFT, HOP = 16384, 8192
FONT = 'Noto Sans CJK SC'
BLUE, ORANGE = '#1769aa', '#d66822'
CASES = [
    ('01', 'ks_single_c4', 'board_20261102Tksfm01', 'KS 拨弦合成器', '单音 C4', 'C4（MIDI 60）'),
    ('03', 'ks_chord_ceg', 'board_20261102Tksfm01', 'KS 拨弦合成器', 'CEG 和弦', 'C4、E4、G4（MIDI 60、64、67）'),
    ('05', 'fm_single_c4', 'board_20261102Tksfm01', 'FM 合成器', '单音 C4', 'C4（MIDI 60）'),
    ('07', 'fm_chord_ceg', 'board_20261102Tksfm01', 'FM 合成器', 'CEG 和弦', 'C4、E4、G4（MIDI 60、64、67）'),
    ('09', 'ks_poly16_retrigger', 'board_poly_report_20261102', 'KS 拨弦合成器', '16 音请求，三轮重复触发', 'MIDI 48至63，同时请求 16 音'),
    ('11', 'fm_poly8_retrigger', 'board_poly_report_20261102', 'FM 合成器', '8 音请求，三轮重复触发', 'MIDI 60至67，同时请求 8 音'),
]


def zh(text):
    # Explicit Latin runs keep numeric PDF text mappings independent of the
    # CJK TTC's compatible glyph aliases, while Chinese remains Noto CJK SC.
    return re.sub(r'([A-Za-z0-9][A-Za-z0-9 ./%+\-]*)',
                  lambda m: '<span style="font-family: DejaVu Sans">'+html.escape(m.group(0))+'</span>', text)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_case(spec):
    number, name, directory, engine, condition, notes = spec
    folder = ROOT/'sim/results/ks_fm_pcm_check_20261102'/directory/name
    paths = [folder/'capture/audio.wav', folder/'run.json', folder/'midi.json']
    hashes = {str(p): sha(p) for p in paths}
    run, midi = [json.loads(p.read_text()) for p in paths[1:]]
    assert midi['status'] == 'PASS' and all(e['stage'] == 'fpga_byte_ack' for e in midi['events'])
    assert run['source_released'] and run['final_capture_stats']['status'] == 'PASS'
    with wave.open(str(paths[0]), 'rb') as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getcomptype()) == (2, 4, 'NONE')
        rate = w.getframerate()
        raw = np.frombuffer(w.readframes(w.getnframes()), '<i4').reshape(-1, 2)
    assert rate == 96008 and np.all((raw & 255) == 0)
    audio = (raw.astype(np.int64) >> 8).astype(float)/FS24
    events = []
    for event in midi['events']:
        t = run['midi_launch_monotonic']-run['capture_started_monotonic']+(event['dispatch_ms']-midi['replay_start_ms'])/1000
        events.append({'host_capture_seconds': t, 'relative_seconds': t,
                       'kind': '发音' if event['bytes'][0]&0xf0 == 0x90 else '松音',
                       'bytes': event['bytes'], 'host_estimated_frame': round(t*rate)})
    origin = events[0]['host_capture_seconds']
    for event in events:
        event['relative_seconds'] -= origin
    # Only search near the first requested attack; this avoids any earlier tail
    # being mistaken for the new performance. The existing 96-sample AC-RMS
    # criterion is retained, with three consecutive blocks above 0.0005 FS.
    block = 96
    lo = max(run['ready_preroll']['sample_frames'], round((origin-.10)*rate))
    hi = min(len(audio), round((origin+.25)*rate))
    start = (lo//block)*block
    count = (hi-start)//block
    y = audio[start:start+count*block].reshape(count, block, 2)
    energy = np.sqrt(np.mean((y-y.mean(axis=1, keepdims=True))**2, axis=(1, 2)))
    good = np.flatnonzero(np.convolve((energy>.0005).astype(int), np.ones(3, dtype=int), 'valid') == 3)
    assert len(good), 'No substantial first attack: '+name
    onset = start+int(good[0])*block
    flo, fhi = onset+round(.12*rate), onset+round(.38*rate)
    assert fhi < events[1]['host_estimated_frame']+round(.05*rate), 'Frequency window must be within the first sounding hold'
    window = np.hanning(NFFT)
    powers = []
    for at in range(flo, fhi-NFFT+1, HOP):
        part = audio[at:at+NFFT].copy();part -= part.mean(axis=0)
        amplitude = np.abs(np.fft.rfft(part*window[:, None], axis=0))*2/window.sum()
        powers.append(amplitude**2)
    assert powers
    power = np.mean(powers, axis=0)
    spectrum = 10*np.log10(np.maximum(power, 1e-30))
    return dict(number=number, name=name, folder=folder, engine=engine,
                condition=condition, notes=notes, rate=rate, audio=audio,
                events=events, origin=origin, onset=onset, input_hashes=hashes,
                frequency=np.fft.rfftfreq(NFFT, 1/rate), spectrum=spectrum,
                fft_start=flo, fft_end=fhi, fft_segments=len(powers), run=run)


def init_qt():
    os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    sys.path.insert(0, str(PC))
    from pcm_studio.qt_runtime import configure_qt
    configure_qt()
    from PySide6.QtGui import QFont
    from PySide6.QtWidgets import QApplication
    import pyqtgraph as pg
    app = QApplication.instance() or QApplication([])
    app.setFont(QFont(FONT, 15))
    pg.setConfigOptions(background='w', foreground='#253449', antialias=True)
    return app, pg


def base_layout(case, domain, pg):
    widget = pg.GraphicsLayoutWidget()
    widget.resize(1600, 1000)
    widget.ci.layout.setContentsMargins(28, 18, 28, 16)
    widget.ci.layout.setSpacing(16)
    widget.addLabel(zh(case['engine']+' · '+case['condition']+' · '+domain),
                    row=0, col=0, colspan=2, size='28pt', bold=True)
    widget.addLabel(zh(case['notes']+'　｜　力度 100　｜　'+('基础拨弦，衰减设置 3000 ms' if case['name'].startswith('ks_') else '当前默认单算子音色')),
                    row=1, col=0, colspan=2, size='17pt')
    widget.addLabel(zh('采样率 96 008 Hz　｜　24 位 PCM　｜　最终低通后输出　｜　原始幅度，未归一化'),
                    row=2, col=0, colspan=2, size='17pt', color='#4e5969')
    return widget


def style(plot, pg, bottom, left, height=310):
    from PySide6.QtGui import QFont
    plot.setMinimumHeight(height)
    plot.showGrid(x=True, y=True, alpha=.18)
    plot.setLabel('bottom', bottom, **{'font-size': '20pt'})
    plot.setLabel('left', left, **{'font-size': '20pt'})
    for which in ('left', 'bottom'):
        font = QFont(FONT)
        font.setPixelSize(23)
        plot.getAxis(which).setTickFont(font)
        plot.getAxis(which).enableAutoSIPrefix(False)
    plot.getAxis('left').setWidth(110)
    plot.getAxis('bottom').setHeight(78)
    plot.setMenuEnabled(False)


def display_extrema(audio, indices, maximum=12000):
    # Preserve extrema rather than calculating an averaged/resampled signal.
    if len(indices) <= maximum:
        return indices, audio[indices]
    step = math.ceil(len(indices)/(maximum//2))
    selected = []
    for at in range(0, len(indices), step):
        group = indices[at:at+step]
        values = audio[group]
        selected.extend(sorted({int(group[np.argmin(values)]), int(group[np.argmax(values)])}))
    selected = np.asarray(selected, dtype=np.int64)
    return selected, audio[selected]


def time_figure(case, pg):
    from PySide6.QtCore import Qt
    widget = base_layout(case, '时域波形', pg)
    poly = len(case['events']) > 2
    t0, t1 = -.10, (3.10 if poly else 1.20)
    rate, audio, origin = case['rate'], case['audio'], case['origin']
    lo = max(0, round((origin+t0)*rate));hi = min(len(audio), round((origin+t1)*rate))
    limit = max(.10, math.ceil(np.abs(audio[lo:hi]).max()*1.12/.05)*.05)
    zoom_start = case['onset']+round(.18*rate)
    zoom_indices = np.arange(zoom_start, zoom_start+round(.008*rate))
    for ch, label, color in [(0, '左声道', BLUE), (1, '右声道', ORANGE)]:
        row = 3+ch
        overview = widget.addPlot(row=row, col=0, title=label+' · 演奏全过程')
        zoom = widget.addPlot(row=row, col=1, title=label+' · 局部波形（8 ms）')
        overview.setTitle(label+' · 演奏全过程', size='18pt')
        zoom.setTitle(label+' · 局部波形（8 ms）', size='18pt')
        style(overview, pg, '相对首轮发音请求的时间（s）', '幅度（满量程比例）')
        style(zoom, pg, '相对首轮发音请求的时间（ms）', '幅度（满量程比例）')
        overview.setXRange(t0, t1, padding=0);overview.setYRange(-limit, limit, padding=0)
        zoom.setYRange(-limit, limit, padding=0)
        indices, values = display_extrema(audio[:, ch], np.arange(lo, hi))
        overview.plot(indices/rate-origin, values, pen=pg.mkPen(color, width=1.2))
        zoom.plot((zoom_indices/rate-origin)*1000, audio[zoom_indices, ch], pen=pg.mkPen(color, width=2.2))
        for i, event in enumerate(case['events']):
            is_on = event['kind'] == '发音'
            marker = pg.InfiniteLine(pos=event['relative_seconds'], angle=90,
                pen=pg.mkPen('#287b58' if is_on else '#aa4e4e', width=1.3,
                             style=Qt.PenStyle.SolidLine if is_on else Qt.PenStyle.DashLine),
                label=event['kind']+(str(i//2+1) if poly else ''),
                labelOpts={'position': .96 if is_on else .86, 'color': '#40505e',
                           'anchors': [(0, 0), (1, 0)]})
            overview.addItem(marker)
    widget.addLabel(zh('竖线：主机发音／松音请求时刻估计，非 FPGA 原生采样标签。局部窗：检测起音后 180至188 ms。'),
                    row=5, col=0, colspan=2, size='16pt', color='#4e5969')
    widget.addLabel('概览按区间保留原始样本极值以保证瞬态可见；局部绘制逐样本波形，音频没有重采样。',
                    row=6, col=0, colspan=2, size='16pt', color='#4e5969')
    return widget, {'overview_seconds_relative_to_first_host_NoteOn': [t0, t1],
                    'zoom_frame_range': [int(zoom_indices[0]), int(zoom_indices[-1])+1],
                    'overview_display': 'interval min/max original samples; no signal averaging/resampling',
                    'amplitude_y_range': [-limit, limit]}


def frequency_figure(case, pg):
    from PySide6.QtCore import Qt
    widget = base_layout(case, '频域幅度谱', pg)
    main = widget.addPlot(row=3, col=0, colspan=2, title='首轮保持段幅度谱 · 0至12 kHz')
    detail = widget.addPlot(row=4, col=0, colspan=2, title='低频局部 · 0至1.5 kHz')
    main.setTitle(zh('首轮保持段幅度谱 · 0至12 kHz（蓝实线：左声道；橙虚线：右声道）'), size='18pt')
    detail.setTitle(zh('低频局部 · 0至1.5 kHz（蓝实线：左声道；橙虚线：右声道）'), size='18pt')
    for plot, end, height in [(main, 12000, 350), (detail, 1500, 260)]:
        style(plot, pg, '频率（Hz）', '幅度（dBFS）', height)
        plot.setXRange(0, end, padding=0);plot.setYRange(-120, 0, padding=0)
        for ch, label, color, linestyle in [(0, '左声道', BLUE, Qt.PenStyle.SolidLine),
                                           (1, '右声道', ORANGE, Qt.PenStyle.DashLine)]:
            plot.plot(case['frequency'], case['spectrum'][:, ch],
                      pen=pg.mkPen(color, width=1.9, style=linestyle), name=label)
    start = (case['fft_start']-case['onset'])/case['rate']*1000
    end = (case['fft_end']-case['onset'])/case['rate']*1000
    widget.addLabel(zh(f'分析窗：首轮检测起音后 {start:.0f}至{end:.0f} ms　｜　汉宁窗　｜　16 384 点 FFT　｜　50% 重叠，{case["fft_segments"]} 段功率平均'),
                    row=5, col=0, colspan=2, size='16pt', color='#4e5969')
    widget.addLabel(zh('按满量程正弦峰值标定绝对幅度；逐窗去直流，不进行增益拟合。低于 -120 dBFS 的谱线不在显示范围内。'),
                    row=6, col=0, colspan=2, size='16pt', color='#4e5969')
    return widget, {'FFT_points': NFFT, 'hop_points': HOP,
                    'window': 'Hann', 'power_averaged_segments': case['fft_segments'],
                    'window_frame_range': [case['fft_start'], case['fft_end']],
                    'window_seconds_relative_to_detected_attack': [start/1000, end/1000],
                    'frequency_bin_spacing_Hz': case['rate']/NFFT,
                    'amplitude_calibration': 'full-scale sine peak=0dBFS; coherent Hann gain corrected',
                    'display_dBFS_range': [-120, 0]}


def export(widget, png, pdf, app):
    from PySide6.QtCore import QRectF, QSizeF, QMarginsF
    from PySide6.QtGui import QPainter, QPdfWriter, QPageSize, QPageLayout
    import pyqtgraph.exporters
    widget.show()
    for _ in range(5):app.processEvents();widget.ci.layout.activate()
    exporter = pyqtgraph.exporters.ImageExporter(widget.ci)
    exporter.parameters()['width'] = WIDTH
    exporter.parameters()['height'] = HEIGHT
    image = exporter.export(toBytes=True)
    # DPI is metadata only after drawing; setting it before painting makes
    # Qt point-sized ticks depend on the export device's physical DPI.
    image.setDotsPerMeterX(round(DPI/.0254));image.setDotsPerMeterY(round(DPI/.0254))
    assert image.save(str(png))
    writer = QPdfWriter(str(pdf));writer.setResolution(96)
    writer.setTitle(png.stem);writer.setCreator('离线 PCM 报告绘图')
    writer.setPageSize(QPageSize(QSizeF(WIDTH/DPI*25.4, HEIGHT/DPI*25.4), QPageSize.Unit.Millimeter))
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout.Unit.Millimeter)
    painter = QPainter(writer)
    target = QRectF(0, 0, writer.width(), writer.height())
    source = QRectF(exporter.getSourceRect())
    try:
        exporter.setExportMode(True, {'antialias': True, 'background': 'w', 'painter': painter,
                                     'resolutionScale': target.width()/exporter.getTargetRect().width()})
        painter.setRenderHints(QPainter.RenderHint.Antialiasing | QPainter.RenderHint.TextAntialiasing)
        exporter.getScene().render(painter, target, source)
    finally:
        exporter.setExportMode(False);painter.end()
    sizes = [[item.vb.width(), item.vb.height()] for item in widget.ci.items if hasattr(item, 'vb')]
    widget.close();app.processEvents()
    return sizes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--sample', action='store_true')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = [read_case(spec) for spec in (CASES[:1] if args.sample else CASES)]
    app, pg = init_qt()
    entries = []
    for case in cases:
        for domain, factory, offset in [('时域', time_figure, 0), ('频域', frequency_figure, 1)]:
            filename = f'{int(case["number"])+offset:02d}_{case["name"]}_{domain}'
            png, pdf = args.output/(filename+'.png'), args.output/(filename+'.pdf')
            widget, parameters = factory(case, pg)
            sizes = export(widget, png, pdf, app)
            assert sizes and all(w>500 and h>140 for w,h in sizes), sizes
            entries.append({'number': int(case['number'])+offset, 'case': case['name'],
                            'title': case['engine']+' · '+case['condition']+' · '+domain,
                            'domain': domain, 'PNG': str(png.resolve()), 'PDF': str(pdf.resolve()),
                            'PNG_SHA256': sha(png), 'PDF_SHA256': sha(pdf),
                            'pixels': [WIDTH, HEIGHT], 'dpi': DPI, 'PDF_vector': True,
                            'font_family': FONT, 'plot_view_sizes': sizes,
                            'source_inputs_SHA256': case['input_hashes'],
                            'detected_attack_frame': case['onset'], 'events': case['events'],
                            'parameters': parameters, 'normalization': False, 'gain_applied': 1,
                            'channel_order': 'L,R unchanged', 'resampling': False,
                            'alignment_scope': 'Host dispatch time estimate, not native sample tags'})
            print(json.dumps({'exported': filename, 'PNG': str(png), 'PDF': str(pdf)}, ensure_ascii=False), flush=True)
    for case in cases:
        assert all(sha(Path(p))==digest for p,digest in case['input_hashes'].items()), 'An input changed'
    manifest = {'status': 'EXPORTED_OFFLINE', 'figure_count': len(entries), 'sample_only': args.sample,
                'script_SHA256': sha(__file__), 'hardware_access': False, 'figures': entries,
                'scope': 'Polyphony count denotes simultaneous MIDI note requests; no native voice-counter proof'}
    (args.output/'figure-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')


if __name__ == '__main__':
    main()
