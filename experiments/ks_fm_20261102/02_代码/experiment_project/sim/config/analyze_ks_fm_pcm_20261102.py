#!/usr/bin/env python3
"""Offline measurements of existing KS/FM captures, without a SUB reference.

Read original PCM24/S32 and raw records. No gain fitting, normalization,
channel swap, resampling, hardware access or modification of capture files.
"""
from pathlib import Path
import argparse
import csv
import importlib.util
import json
import math
import os
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
PC = Path('/media/ice/D;/PROJECT_improve/pcm24_subtractive_ks_20261025T185127/pc')
EXPECTED = {f'{engine}_{case}' for engine in ('ks', 'fm')
            for case in ('single_c4', 'chord_ceg', 'single_c4_repeat1', 'single_c4_repeat2')}


def load_analyzer():
    path = ROOT/'sim/config/analyze_upstream_pcm_recheck.py'
    spec = importlib.util.spec_from_file_location('ksfm_common_analyzer', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def pitch_measurement(x, rate, start, end, trusted, a):
    """C4-centered normalized ACF plus independently interpolated FFT peak.

    The prior is explicit: this is a commanded C4 diagnostic, not a general
    polyphonic pitch detector. No audio data is changed by these estimators.
    """
    expected = 440.0 * 2**((60-69)/12)
    y = x[start:end].astype(float).mean(axis=1)/a.FS24
    y -= y.mean()
    answer = {'expected_Hz': expected, 'nominal_rate_Hz': rate,
              'window_start_frame': start, 'window_end_frame': end,
              'method': 'normalized mean-removed ACF; C4 prior +/-20%, quadratic lag interpolation',
              'FFT_method': '16384-point Hann; log-power peak interpolation; independent global 20-2000Hz peak',
              'native_clock_frequency_verified': False, 'trusted': False}
    if len(y) < 16384 or not np.any(y):
        answer['reason'] = 'Insufficient/nonzero window'
        return answer
    n = len(y)
    size = 1 << (2*n-1).bit_length()
    acf = np.fft.irfft(np.abs(np.fft.rfft(y, size))**2, size)[:n]
    squares = np.r_[0.0, np.cumsum(y*y)]
    lo, hi = int(rate/(expected*1.2)), int(rate/(expected*.8))
    lags = np.arange(lo, hi+1)
    denom = np.sqrt((squares[n-lags]) * (squares[n]-squares[lags]))
    score = np.divide(acf[lags], denom, out=np.zeros_like(denom), where=denom>0)
    at = int(np.argmax(score)); lag = float(lags[at])
    if 0 < at < len(score)-1:
        left, peak, right = score[at-1:at+2]
        divisor = left-2*peak+right
        if divisor:
            lag += float(np.clip(.5*(left-right)/divisor, -.5, .5))
    hz = rate/lag
    nfft = 16384
    win = np.hanning(nfft)
    powers = []
    for pos in range(0, n-nfft+1, nfft//2):
        segment = y[pos:pos+nfft];segment = segment-segment.mean()
        powers.append((np.abs(np.fft.rfft(segment*win))*2/win.sum())**2)
    power = np.mean(powers, axis=0);frequencies = np.fft.rfftfreq(nfft, 1/rate)
    mask = (frequencies>=20)&(frequencies<=2000)
    dominant = int(np.flatnonzero(mask)[np.argmax(power[mask])])
    near = (frequencies>=expected*.8)&(frequencies<=expected*1.2)
    k = int(np.flatnonzero(near)[np.argmax(power[near])]);offset = 0.0
    if 0 < k < len(power)-1:
        left, peak, right = np.log(np.maximum(power[k-1:k+2], 1e-30))
        divisor = left-2*peak+right
        if divisor:
            offset = float(np.clip(.5*(left-right)/divisor, -.5, .5))
    fft_hz = (k+offset)*rate/nfft
    credible = bool(trusted and score[at]>=.5 and 0<at<len(score)-1)
    answer.update(trusted=credible, ACF_coefficient=float(score[at]),
                  frequency_Hz=float(hz) if credible else None,
                  cents_from_C4=float(1200*math.log2(hz/expected)) if credible else None,
                  FFT_near_C4_peak_Hz=float(fft_hz),
                  FFT_global_strongest_bin_Hz=float(frequencies[dominant]),
                  FFT_bin_spacing_Hz=rate/nfft,
                  diagnostic_ACF_frequency_Hz=float(hz))
    if not credible:
        answer['reason'] = 'No substantial attack/level or insufficient periodicity; diagnostic values are not accepted pitch'
    return answer


def noteoff_metrics(x, rate, frame, a):
    values = []
    for offset in (-.10, 0, .25, .5, 1, 2, 4):
        lo = max(0, frame+round(offset*rate));hi = min(len(x), lo+round(.1*rate))
        channels = []
        for c in range(2):
            segment = x[lo:hi,c].astype(float)/a.FS24
            level = a.rms(segment) if len(segment) else None
            ac_level = float(np.std(segment)) if len(segment) else None
            channels.append({'channel': 'LR'[c],
                             'digital_zero': bool(len(segment) and np.all(segment==0)),
                             'RMS_dBFS': a.db(level) if level else None,
                             'AC_RMS_dBFS': a.db(ac_level) if ac_level else None})
        values.append({'seconds_relative_to_host_NoteOff': offset,
                       'start_frame': lo, 'end_frame': hi, 'channels': channels})
    base = values[0]['channels']
    for value in values:
        value['RMS_difference_from_preOff_dB'] = [
            ch['RMS_dBFS']-base[c]['RMS_dBFS']
            if ch['RMS_dBFS'] is not None and base[c]['RMS_dBFS'] is not None else None
            for c,ch in enumerate(value['channels'])]
    return {'coordinate': 'Actual HTTP dispatch NoteOff mapped to WAV via host monotonic timestamps; no native sample tags',
            'estimated_NoteOff_frame': frame, 'window_seconds': .1, 'windows': values,
            'zero_level_representation': 'digital_zero=true and RMS_dBFS=null means exact zero (-infinity dBFS), not a finite attenuation',
            'not_a_T60_measurement': True}


def analyze_case(folder, a):
    run = json.loads((folder/'run.json').read_text())
    midi = json.loads((folder/'midi.json').read_text())
    selected = json.loads((folder/'selected-state.json').read_text())
    channel = 1 if folder.name.startswith('ks_') else 2
    engine = 'ks' if channel==1 else 'fm'
    owner = selected['clients'][midi['session']['client']]
    assert owner['token']==midi['session']['token'] and owner['midi_channel']==channel
    assert owner['result_code']==0 and owner['result_context_epoch']==midi['session']['context_epoch']
    assert selected['cpu_mode']['channel_routes'] and selected['cpu_mode']['valid'] and not selected['cpu_mode']['fault']
    events = midi['events'];assert len(events)==2 and midi['status']=='PASS'
    for event,status in zip(events, (0x90|channel,0x80|channel)):
        assert event['stage']=='fpga_byte_ack'
        expected = sum(([status,n,100 if status&0xf0==0x90 else 0] for n in run['notes']), [])
        assert event['bytes']==expected
    setup = midi['setup_events'];assert setup[0]['bytes']==[0xb0|channel,120,0]
    assert all(e['stage']=='fpga_byte_ack' for e in setup)
    assert setup[1]['bytes']==[0xb0|channel,7,127,0xb0|channel,10,64,0xb0|channel,11,127,
                               0xb0|channel,1,0,0xb0|channel,64,0,0xe0|channel,0,64]
    x,rate,raw,info = a.read_wav(folder/'capture/audio.wav')
    assert rate==96008 and info['container_bits']==32
    transport = a.verify_transport(folder, raw, 4);assert transport['status']=='PASS'
    event_seconds = [run['midi_launch_monotonic']-run['capture_started_monotonic']+
                     (e['dispatch_ms']-midi['replay_start_ms'])/1000 for e in events]
    event_frames = [round(t*rate) for t in event_seconds]
    preroll = run['ready_preroll']['sample_frames'];assert preroll>=24000
    onset,detected = a.detect_onset(x, rate, preroll)
    anchor = onset if onset is not None else event_frames[0]
    metrics,spec = a.audio_metrics(x,rate,anchor,'single_c4' if len(run['notes'])==1 else 'chord_ceg')
    trusted = bool(detected['substantial_attack_detected'] and all(ch['spectral_trusted'] for ch in metrics['channels']))
    if not detected['substantial_attack_detected']:
        for ch in metrics['channels']:ch['spectral_trusted']=False
    pitch = pitch_measurement(x,rate,metrics['held_start_frame'],metrics['held_end_frame'],trusted,a) if len(run['notes'])==1 else None
    result = {'case':folder.name,'engine':engine,'notes':run['notes'],'velocity':100,
              'MIDI_channel_zero_based':channel,'owner_metadata_verified':True,
              'raw_channel_bytes_verified':True,'ack_scope':midi['ack_scope'],
              'capture':info,'transport':transport,'onset':detected,
              'detected_onset_frame':onset,'host_event_seconds':event_seconds,
              'host_event_frame_estimates':event_frames,
              'onset_minus_host_NoteOn_seconds':(onset-event_frames[0])/rate if onset is not None else None,
              'planned_hold_ms':450,'actual_dispatch_hold_ms':(events[1]['dispatch_ms']-events[0]['dispatch_ms']),
              'ks_decay_ms':run['ks_decay_ms'],'routes':run['routes'],'reverb':run['reverb'],
              'audio':metrics,'pitch':pitch,'noteoff':noteoff_metrics(x,rate,event_frames[1],a),
              'left_right_PCM_exact':bool(np.array_equal(x[:,0],x[:,1])),
              'normalization':False,'gain_applied':1,'resampling':False,
              'matching_RTL_reference':None,'native_execution_identity_verified':False}
    return result,x,spec


def plot(out, results, arrays, spectra, a):
    os.environ['QT_QPA_PLATFORM']='offscreen'
    sys.path.insert(0,str(PC))
    from pcm_studio.qt_runtime import configure_qt
    configure_qt()
    from PySide6.QtWidgets import QApplication
    import pyqtgraph as pg
    import pyqtgraph.exporters
    app=QApplication.instance() or QApplication([])
    pg.setConfigOptions(background='w',foreground='#263449',antialias=True)
    widget=pg.GraphicsLayoutWidget();widget.resize(1800,1100)
    widget.addLabel('KS / FM measured PCM | original dBFS, gain=1, no normalization',row=0,col=0,colspan=2)
    colors=['#1f77b4','#d15c37','#2d9476']
    byname={r['case']:r for r in results}
    for row,engine in enumerate(('ks','fm'),1):
        ax=widget.addPlot(row=row,col=0,title=engine.upper()+' C4 repeats | stereo RMS')
        ax.setMinimumHeight(350);ax.addLegend();ax.setLabel('left','RMS','dBFS');ax.setLabel('bottom','Time from detected attack','s')
        ax.setXRange(-.05,3.8);ax.setYRange(-110,0);ax.showGrid(x=True,y=True,alpha=.2)
        bx=widget.addPlot(row=row,col=1,title=engine.upper()+' C4 repeats | held absolute FFT')
        bx.setMinimumHeight(350);bx.addLegend();bx.setLabel('left','Spectral amplitude','dBFS');bx.setLabel('bottom','Frequency','Hz')
        bx.setXRange(20,6000);bx.setYRange(-130,0);bx.showGrid(x=True,y=True,alpha=.2)
        for i,suffix in enumerate(('single_c4','single_c4_repeat1','single_c4_repeat2')):
            name=engine+'_'+suffix;r=byname[name];x=arrays[name];rate=r['capture']['WAV_rate_Hz'];anchor=r['audio']['onset_frame']
            block=960;lo=max(0,anchor-round(.05*rate));hi=min(len(x),anchor+round(3.8*rate));n=(hi-lo)//block
            y=x[lo:lo+n*block].astype(float).reshape(n,block,2)/a.FS24
            level=np.sqrt(np.mean(y*y,axis=(1,2)));seconds=(lo+(np.arange(n)+.5)*block-anchor)/rate
            ax.plot(seconds,20*np.log10(np.maximum(level,1e-15)),pen=pg.mkPen(colors[i],width=2),name=f'take {i+1}')
            f,p=spectra[name];mag=10*np.log10(np.maximum(p.mean(axis=1),1e-30))
            bx.plot(f,mag,pen=pg.mkPen(colors[i],width=1.5),name=f'take {i+1}')
    widget.show();app.processEvents();app.processEvents()
    exporter=pg.exporters.ImageExporter(widget.ci);exporter.parameters()['width']=2400
    exporter.export(str(out/'ks-fm-measured.png'));widget.close();app.processEvents()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture-root',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args();a=load_analyzer()
    session=json.loads((args.capture_root/'session.json').read_text())
    assert session['status']=='CAPTURED_PENDING_ANALYSIS' and set(session['scenarios'])==EXPECTED
    assert session['production_tools_unchanged'] and session['shared_lock_released']
    files=[args.capture_root/'session.json']
    for name in sorted(EXPECTED):
        files += [args.capture_root/name/p for p in ('run.json','midi.json','events.json','selected-state.json',
                                                     'capture/raw.bin','capture/payload.pcm','capture/audio.wav')]
    before={str(p.resolve()):a.sha(p) for p in files}
    if args.out.exists():
        previous=json.loads((args.out/'summary.json').read_text())
        assert previous['status']=='MEASURED_BOARD_ONLY' and previous['input_SHA256']==before, 'Refuse unrelated output replacement'
    else:
        args.out.mkdir(parents=True,exist_ok=False)
    results=[];arrays={};spectra={}
    for name in sorted(EXPECTED):
        result,x,spec=analyze_case(args.capture_root/name,a);results.append(result);arrays[name]=x;spectra[name]=spec
        (args.out/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2,allow_nan=False)+'\n')
        with(args.out/(name+'-spectrum.csv')).open('w',newline='')as stream:
            writer=csv.writer(stream);writer.writerow(['frequency_Hz','L_dBFS','R_dBFS'])
            for f,power in zip(*spec):writer.writerow([f,*[a.db(v)for v in np.sqrt(power)]])
    repeat={}
    for engine in ('ks','fm'):
        cases=[r for r in results if r['engine']==engine and len(r['notes'])==1]
        repeat[engine]={'takes':len(cases),'substantial_attacks':sum(r['onset']['substantial_attack_detected']for r in cases),
                        'peak_dBFS_range_LR':[[min(r['audio']['channels'][c]['peak_dBFS']for r in cases),max(r['audio']['channels'][c]['peak_dBFS']for r in cases)]for c in range(2)],
                        'hold_RMS_dBFS_range_LR':[[min(r['audio']['channels'][c]['hold_RMS_dBFS']for r in cases),max(r['audio']['channels'][c]['hold_RMS_dBFS']for r in cases)]for c in range(2)],
                        'C4_frequency_Hz_range':[min(r['pitch']['frequency_Hz']for r in cases if r['pitch']['trusted']),max(r['pitch']['frequency_Hz']for r in cases if r['pitch']['trusted'])]if any(r['pitch']['trusted']for r in cases)else None}
    totals={'takes':len(results),'raw_bytes':sum(r['transport']['raw_bytes']for r in results),
            'valid_frames':sum(r['transport']['parser']['frames_valid']for r in results),
            'stereo_pairs':sum(r['capture']['frames']for r in results)}
    for k in ('frames_invalid','seq_jumps','resync_count','sample_rate_changes','bytes_discarded'):
        totals[k]=sum(r['transport']['parser'][k]for r in results)
    summary={'status':'MEASURED_BOARD_ONLY','totals':totals,'cases':results,'repeat_summary':repeat,
             'normalization':False,'gain_applied':1,'resampling':False,'channel_order':'L,R unchanged',
             'matching_RTL_reference':None,'bit_exact_against_RTL_not_tested':True,
             'native_sample_identity_proven':False,'CRC_verified':False,'FPGA_internal_drop_counter_verified':False,
             'scope':{'KS':session['KS'],'FM':session['FM'],'KS_timbre_readback':session['ks_timbre']},
             'script_SHA256':a.sha(__file__),'common_analyzer_SHA256':a.sha(ROOT/'sim/config/analyze_upstream_pcm_recheck.py'),
             'input_SHA256':before}
    with(args.out/'measurements.csv').open('w',newline='')as stream:
        writer=csv.writer(stream);writer.writerow(['case','attack','peak_L_dBFS','peak_R_dBFS','held_L_dBFS','held_R_dBFS','actual_dispatch_hold_ms','C4_Hz','C4_cents','L_R_PCM_exact','gt2k_L_pct','gt2k_R_pct','gt5k_L_pct','gt5k_R_pct','gt10k_L_pct','gt10k_R_pct'])
        for r in results:
            ch=r['audio']['channels'];p=r['pitch']or{}
            writer.writerow([r['case'],r['onset']['substantial_attack_detected'],*[v['peak_dBFS']for v in ch],*[v['hold_RMS_dBFS']for v in ch],r['actual_dispatch_hold_ms'],p.get('frequency_Hz'),p.get('cents_from_C4'),r['left_right_PCM_exact'],*[v['band_power_fraction_gt_Hz'][edge]*100 for edge in ('2000','5000','10000')for v in ch]])
    plot(args.out,results,arrays,spectra,a)
    assert all(a.sha(Path(p))==v for p,v in before.items()),'Input changed during analysis'
    summary['input_hashes_unchanged']=True
    (args.out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2,allow_nan=False)+'\n')
    lines=['# KS 拨弦和 FM 实测分析','','2026-11-02。只读离线分析当前6A5B的8段实际录音。未修改录音/参数/固件，原始增益、左右声道和96008Hz名义采样率保持。',
           '',f"共{totals['raw_bytes']:,}字节、{totals['valid_frames']:,}有效帧、{totals['stereo_pairs']:,}组左右样本。所有RAW/payload/WAV逐字一致，非法帧/跳号/重同步/采样率变化/丢弃均0；S32低8位全零。v1仍无native sample index/CRC/FPGA内部drop计数。",'',
           '## 参数、通道与回执','','KS为Legacy KS（ENABLE_KS_TIMBRES=0），保留现有decay3000ms；不是Harp/Guzheng物理音色测试。FM为当前CPU启动默认音色（algorithm7、仅一个keyed operator），没有测试复杂多算子调制音色。',
           'KS raw状态字91/81/B1/E1、FM 92/82/B2/E2已逐批核对；op5仅修改client metadata，client midi_channel分别为1/2，并非改变全局applied mode。每批音符均有fpga_byte_ack，仍不证明CPU逐条native提交内容。',
           'vel100、CC7/11=127、pan64、bend居中；PCM-B=SUM，reverb wet/damp=0。计划hold450ms，实际dispatch间隔见表（HTTP主机时序，不是native采样标签）。', '',
           '| 场景 | 显著起音 | 峰值 L/R dBFS | 保持段 L/R dBFS | 实际hold ms | C4估计 Hz / cents |',
           '|---|---|---|---|---|---|']
    for r in results:
        ch=r['audio']['channels'];pitch=r['pitch'];freq=f"{pitch['frequency_Hz']:.3f} / {pitch['cents_from_C4']:+.2f}"if pitch and pitch['trusted']else'不适用/不可信'
        lines.append(f"| {r['case']} | {'有'if r['onset']['substantial_attack_detected']else'未检出'} | {ch[0]['peak_dBFS']:.2f} / {ch[1]['peak_dBFS']:.2f} | {ch[0]['hold_RMS_dBFS']:.2f} / {ch[1]['hold_RMS_dBFS']:.2f} | {r['actual_dispatch_hold_ms']:.2f} | {freq} |")
    lines+=['','## 频谱和衰减窗口','','保持段固定使用检测起音后0.13–0.40秒；8192点Hann、逐窗去均值、功率平均。以下百分比相对于20Hz以上总谱功率，不是归一化音频。', '', '| 场景 | >2kHz L/R % | >5kHz L/R % | >10kHz L/R % |', '|---|---|---|---|']
    for r in results:
        values=[]
        for edge in ('2000','5000','10000'):
            bands=[ch['band_power_fraction_gt_Hz'][edge]*100 for ch in r['audio']['channels']];values.append(f'{bands[0]:.6f} / {bands[1]:.6f}')
        lines.append('| '+r['case']+' | '+' | '.join(values)+' |')
    lines+=['', 'NoteOff前后使用midi.json实际HTTP dispatch时刻映射到WAV的host估计帧，100ms窗，记录Off前0.1s及Off后0/0.25/0.5/1/2/4s的绝对RMS、AC RMS和相对衰减，详见各段JSON。精确数字零以digital_zero=true、RMS_dBFS=null表示（数学上负无穷），不制造有限的−300dBFS或−270dB衰减结论。USB排队/native事件时刻未知，不能把这些窗当精确native释放包络。短held后主动NoteOff也不能用于宣称KS自然T60就是3000ms。', '',
            '单C4音高估计：在同一保持窗，对左右平均的去均值信号作C4预期±20%范围归一化ACF，局部二次插值；另保留16384点Hann FFT的独立插值近C4峰和20–2000Hz全局最强峰。仅在有显著起音且周期相关系数≥0.5时接受；名义采样时钟未独立校准，cents属于基于96008Hz标签的估计。和弦不使用单音F0算法。', '',
            '## 重复性与边界', '',json.dumps(repeat,ensure_ascii=False,indent=2), '',
            'KS噪声激励seed含generation，重复幅度/波形不同不自动判故障；FM当前单算子且中置可能左右完全相同，不能用该音色独立验证FM立体声声像。两路均通过公共最终输出低通。',
            '当前没有匹配的KS/FM整段仿真WAV，本报告仅判断板上起音、幅度、音高、谱形和重复范围，不宣称与RTL bit-exact、也不把默认FM当复杂FM调制全覆盖。', '',
            '保留summary.json、measurements.csv、逐段JSON及绝对谱CSV、ks-fm-measured.png。所有原始输入SHA前后保持，未生成仿真scratch。']
    (args.out/'report.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps({'status':summary['status'],'totals':totals,'repeats':repeat,'out':str(args.out.resolve())},ensure_ascii=False))


if __name__=='__main__':
    main()
