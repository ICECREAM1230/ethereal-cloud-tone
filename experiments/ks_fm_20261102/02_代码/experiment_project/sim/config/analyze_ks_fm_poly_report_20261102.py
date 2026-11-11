#!/usr/bin/env python3
"""Offline validation and timing windows for recorded KS/FM poly retriggers.

Reads original files only. No device, USB, HTTP, serial, hardware lock, reset,
firmware write, gain fitting, normalization, channel swap or resampling.
"""
from pathlib import Path
import argparse
import csv
import importlib.util
import json
import math
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    'ks_poly16_retrigger': ('ks', 1, list(range(48, 64))),
    'fm_poly8_retrigger': ('fm', 2, list(range(60, 68))),
}


def load_common():
    path = ROOT/'sim/config/analyze_upstream_pcm_recheck.py'
    spec = importlib.util.spec_from_file_location('poly_report_common', path)
    obj = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = obj
    spec.loader.exec_module(obj)
    return obj


def nullable_db(value):
    return None if value == 0 else 20*math.log10(float(value))


def level_metrics(x, start, end, a):
    assert 0 <= start < end <= len(x)
    z = x[start:end]
    result = {'start_frame': int(start), 'end_frame': int(end), 'frames': int(len(z)), 'channels': []}
    for ch in range(2):
        integers = z[:, ch].astype(np.int64)
        values = integers.astype(float)/a.FS24
        zero = bool(np.all(integers == 0))
        rms = float(np.sqrt(np.mean(values*values)))
        peak = float(np.max(np.abs(values)))
        result['channels'].append({
            'channel': 'LR'[ch], 'digital_zero': zero,
            'RMS_FS': rms, 'RMS_dBFS': nullable_db(rms),
            'AC_RMS_dBFS': nullable_db(float(np.std(values))),
            'peak_PCM24': int(np.max(np.abs(integers))), 'peak_dBFS': nullable_db(peak),
            'DC_FS': float(values.mean()),
            'rail_samples': int(np.count_nonzero((integers == -8388608) | (integers == 8388607))),
        })
    return result


def spectrum_metrics(x, rate, start, end, a):
    frequencies, power = a.spectrum(x, rate, start, end)
    channels = []
    for ch in range(2):
        total = float(power[frequencies >= 20, ch].sum())
        channels.append({
            'channel': 'LR'[ch],
            'band_power_fraction_gt_Hz': {str(edge): float(power[frequencies > edge, ch].sum()/total)
                                         if total else None for edge in (2000, 5000, 10000, 20000)},
            'spectral_centroid_Hz': float((frequencies[frequencies >= 20]*power[frequencies >= 20, ch]).sum()/total)
                                    if total else None,
        })
    return {'start_frame': int(start), 'end_frame': int(end), 'NFFT': a.NFFT,
            'window': 'Hann, 50% overlap, per-window mean removed, power averaged',
            'channels': channels}, (frequencies, power)


def detect_round_onset(x, rate, begin, end, threshold, block=96):
    """First three consecutive 1ms AC-RMS windows above a fixed noise floor."""
    start = max(0, int(begin)//block*block)
    end = min(len(x), int(end))
    count = (end-start)//block
    assert count >= 3
    windows = x[start:start+count*block].astype(float).reshape(count, block, 2)/8388608.0
    windows -= windows.mean(axis=1, keepdims=True)
    ac = np.sqrt(np.mean(windows*windows, axis=(1, 2)))
    good = ac > threshold
    indices = np.flatnonzero(np.convolve(good.astype(int), np.ones(3, dtype=int), 'valid') == 3)
    onset = start+int(indices[0])*block if len(indices) else None
    return onset, {'method': '96-frame AC-RMS, three consecutive windows, fixed preroll threshold',
                   'search_start_frame': start, 'search_end_frame': end,
                   'threshold_FS': threshold, 'substantial_attack_detected': onset is not None}


def analyze_case(folder, out, a):
    engine, channel, notes = CASES[folder.name]
    run = json.loads((folder/'run.json').read_text())
    midi = json.loads((folder/'midi.json').read_text())
    selected = json.loads((folder/'selected-state.json').read_text())
    plans = json.loads((folder/'events.json').read_text())
    assert midi['status'] == 'PASS' and len(midi['events']) == 6 and len(plans) == 6
    assert run['trigger_rounds'] == 3 and run['requested_simultaneous_voices'] == len(notes)
    assert run['engine'] == engine and run['notes'] == notes and run['velocity'] == 100
    assert run['MIDI_channel_zero_based'] == channel and run['source_released']
    assert run['capture_stats']['status'] == run['final_capture_stats']['status'] == 'PASS'
    assert run['routes']['valid'] and run['routes']['ready'] and run['routes']['pcm_b'] == 3
    assert run['reverb']['valid'] and run['reverb']['wet_q15'] == run['reverb']['damp_q15'] == 0
    owner = selected['clients'][midi['session']['client']]
    assert owner['token'] == midi['session']['token'] and owner['midi_channel'] == channel
    assert owner['result_code'] == 0 and owner['result_context_epoch'] == midi['session']['context_epoch']
    cpu = selected['cpu_mode']
    assert cpu['valid'] and cpu['channel_routes'] and cpu['ready'] and not cpu['fault'] and not cpu['busy']
    assert midi['setup_events'][0]['bytes'] == [0xb0 | channel, 120, 0]
    assert all(event['stage'] == 'fpga_byte_ack' for event in midi['setup_events'])
    x, rate, raw, info = a.read_wav(folder/'capture/audio.wav')
    assert rate == 96008 and info['container_bits'] == 32 and info['low8_zero_verified']
    transport = a.verify_transport(folder, raw, 4)
    assert transport['status'] == 'PASS'
    events = []
    last_dispatch = -1
    for index, event in enumerate(midi['events']):
        on = index % 2 == 0
        status = (0x90 if on else 0x80) | channel
        expected = sum(([status, note, 100 if on else 0] for note in notes), [])
        assert event['bytes'] == expected == plans[index]['bytes']
        assert event['stage'] == 'fpga_byte_ack' and event['planned_ms'] == plans[index]['at_ms']
        assert event['dispatch_ms'] > last_dispatch and event['acknowledged_ms'] >= event['dispatch_ms']
        last_dispatch = event['dispatch_ms']
        seconds = (run['midi_launch_monotonic']-run['capture_started_monotonic']+
                   (event['dispatch_ms']-midi['replay_start_ms'])/1000)
        events.append({'kind': 'NoteOn' if on else 'NoteOff', 'round': index//2+1,
                       'requested_notes': notes, 'planned_ms': event['planned_ms'],
                       'dispatch_ms': event['dispatch_ms'], 'acknowledged_ms': event['acknowledged_ms'],
                       'host_seconds_from_capture_start': seconds,
                       'host_frame_estimate': round(seconds*rate)})
    preroll = run['ready_preroll']['sample_frames']
    assert preroll >= 24000
    block = 96
    nz = preroll//block*block
    pre = x[:nz].astype(float).reshape(-1, block, 2)/a.FS24
    noise = float(np.median(np.sqrt(np.mean((pre-pre.mean(axis=1, keepdims=True))**2, axis=(1, 2)))))
    threshold = max(4/a.FS24, .0005, 8*noise)
    rounds = []
    for index in range(3):
        on, off = events[2*index:2*index+2]
        begin = max(preroll, on['host_frame_estimate']-round(.04*rate))
        finish = off['host_frame_estimate']+round(.10*rate)
        onset, detection = detect_round_onset(x, rate, begin, finish, threshold)
        assert onset is not None, folder.name+' round '+str(index+1)+' no substantial attack'
        held_start = onset+round(.10*rate)
        held_end = onset+round(.30*rate)
        assert held_end <= finish
        spectrum, values = spectrum_metrics(x, rate, held_start, held_end, a)
        active_end = min(len(x), off['host_frame_estimate']+round(.20*rate))
        rounds.append({'round': index+1, 'onset_detection': detection,
                       'detected_onset_frame': onset, 'detected_onset_seconds': onset/rate,
                       'host_NoteOn_frame_estimate': on['host_frame_estimate'],
                       'host_NoteOff_frame_estimate': off['host_frame_estimate'],
                       'actual_dispatch_hold_ms': off['dispatch_ms']-on['dispatch_ms'],
                       'active_interval': level_metrics(x, onset, active_end, a),
                       'held_interval': level_metrics(x, held_start, held_end, a),
                       'spectrum': spectrum,
                       'postOff_250ms': level_metrics(x, off['host_frame_estimate']+round(.25*rate),
                                                     off['host_frame_estimate']+round(.35*rate), a)})
        if index == 0:
            with (out/(folder.name+'-first-round-spectrum.csv')).open('w', newline='') as stream:
                writer = csv.writer(stream);writer.writerow(['frequency_Hz', 'L_dBFS', 'R_dBFS'])
                for frequency, power in zip(*values):
                    writer.writerow([frequency, *[nullable_db(math.sqrt(v)) for v in power]])
    crop_start = max(0, rounds[0]['detected_onset_frame']-round(.10*rate))
    crop_end = min(len(x), events[-1]['host_frame_estimate']+round(.75*rate))
    result = {'case': folder.name, 'engine': engine, 'capture': info, 'transport': transport,
              'requested_simultaneous_voices': len(notes), 'requested_notes': notes,
              'native_active_voice_count_verified': False, 'velocity': 100,
              'MIDI_channel_zero_based': channel, 'all_six_event_batches_byte_ACK': True,
              'ack_scope': midi['ack_scope'], 'host_timing_sample_accurate': False,
              'preroll_sample_pairs': preroll, 'preroll_AC_RMS_FS': noise,
              'events': events, 'rounds': rounds, 'whole_capture': level_metrics(x, 0, len(x), a),
              'plot_suggestions': {
                  'time_crop_start_frame': crop_start, 'time_crop_end_frame': crop_end,
                  'time_origin_frame': rounds[0]['detected_onset_frame'],
                  'time_crop_seconds_relative_to_first_detected_onset': [
                      (crop_start-rounds[0]['detected_onset_frame'])/rate,
                      (crop_end-rounds[0]['detected_onset_frame'])/rate],
                  'spectrum_start_frame': rounds[0]['spectrum']['start_frame'],
                  'spectrum_end_frame': rounds[0]['spectrum']['end_frame'],
                  'spectrum_scope': 'First detected round, onset+0.10..0.30s, original amplitude',
                  'NFFT': a.NFFT,
              },
              'normalization': False, 'gain_applied': 1, 'resampling': False,
              'channel_order': 'L,R unchanged', 'CRC_verified': False,
              'native_sample_index_verified': False, 'FPGA_internal_drop_counter_verified': False,
              'parameter_scope': {'KS': '基础KS拨弦；保留现有decay3000ms，不代表测得自然T60',
                                  'FM': '按现有CPU代码定义的默认单算子音色；未逐寄存器读回或覆盖复杂FM'}}
    (out/(folder.name+'.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)+'\n')
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture-root', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    a = load_common()
    session_path = args.capture_root/'session.json'
    session = json.loads(session_path.read_text())
    assert session['status'] == 'CAPTURED_PENDING_ANALYSIS' and session['shared_lock_released']
    assert set(session['scenarios']) == set(CASES)
    inputs = [session_path]
    for case in CASES:
        inputs += [args.capture_root/case/name for name in ('run.json', 'midi.json', 'selected-state.json',
                   'events.json', 'capture/raw.bin', 'capture/payload.pcm', 'capture/audio.wav')]
    hashes = {str(path.resolve()): a.sha(path) for path in inputs}
    args.out.mkdir(parents=True, exist_ok=False)
    cases = [analyze_case(args.capture_root/name, args.out, a) for name in CASES]
    totals = {'takes': len(cases), 'rounds': sum(len(case['rounds']) for case in cases),
              'raw_bytes': sum(case['transport']['raw_bytes'] for case in cases),
              'valid_frames': sum(case['transport']['parser']['frames_valid'] for case in cases),
              'stereo_sample_pairs': sum(case['capture']['frames'] for case in cases)}
    for key in ('frames_invalid', 'seq_jumps', 'resync_count', 'sample_rate_changes', 'bytes_discarded'):
        totals[key] = sum(case['transport']['parser'][key] for case in cases)
    assert all(a.sha(Path(path)) == digest for path, digest in hashes.items()), 'Input changed during analysis'
    summary = {'status': 'MEASURED_OFFLINE_ALL_SIX_ROUNDS_DETECTED', 'totals': totals, 'cases': cases,
               'capture_root': str(args.capture_root.resolve()), 'input_SHA256': hashes,
               'input_hashes_unchanged': True, 'script_SHA256': a.sha(__file__),
               'common_analyzer_SHA256': a.sha(ROOT/'sim/config/analyze_upstream_pcm_recheck.py'),
               'hardware_access': False, 'normalization': False, 'gain_applied': 1, 'resampling': False,
               'native_voice_count_verified': False,
               'limits': ['声部数是同时发送的MIDI音符请求数，不是直接观测到的native active voice计数',
                          'HTTP dispatch映射到WAV仅是主机估计，无native采样时刻标签',
                          'v1无CRC验证和FPGA内部丢样计数；零跳号仅证明接收到的帧连续',
                          '基础KS和当前默认单算子FM；未与匹配整段仿真作逐点比较']}
    (args.out/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2, allow_nan=False)+'\n')
    lines = ['# KS 与 FM 多声部重复触发离线分析', '', '2026-11-02。仅读取已经释放硬件后保留的原始记录，未访问任何板子、USB、串口、HTTP或共享硬件锁。', '',
             f"两段共 {totals['raw_bytes']:,} 字节、{totals['valid_frames']:,} 个有效帧、{totals['stereo_sample_pairs']:,} 组左右样本。RAW/payload/WAV逐字一致，解析错误、跳号、重同步、采样率变化和丢弃字节均0；S32低8位全零。", '',
             '| 场景 | 请求音符数 | 轮次 | 检测起音 / s | 实际主机hold / ms | 活动段峰值 L/R dBFS | 保持段RMS L/R dBFS |',
             '|---|---:|---:|---:|---:|---|---|']
    for case in cases:
        for record in case['rounds']:
            peak = [channel['peak_dBFS'] for channel in record['active_interval']['channels']]
            rms = [channel['RMS_dBFS'] for channel in record['held_interval']['channels']]
            lines.append(f"| {case['case']} | {case['requested_simultaneous_voices']} | {record['round']} | {record['detected_onset_seconds']:.5f} | {record['actual_dispatch_hold_ms']:.2f} | {peak[0]:.2f} / {peak[1]:.2f} | {rms[0]:.2f} / {rms[1]:.2f} |")
    lines += ['', 'KS 每轮同时请求 MIDI 48–63（16音），FM 每轮同时请求 MIDI 60–67（8音），力度100。三轮NoteOn/Off六批都获得fpga_raw_byte ACK，三个PCM起音均被检测到；这不等于已经直接读取并验证16/8个native声部全部活动。',
              'CPU在选路观察时ready/valid=true、busy/fault=false；PCM-B为SUM，混响wet/damp为0，选路metadata与原始状态字节一致。', '',
              '## 绘图坐标', '',
              'summary.json各case的plot_suggestions给出完整三轮裁剪范围、首轮检测起音时间原点、首轮保持窗（起音后0.10–0.30s）及频谱CSV。时域可展示三个起音、释放与间隔；主机NoteOn/Off标线应明确为估计时刻。',
              '频谱使用8192点Hann、50%重叠、逐窗去均值后功率平均。保持原始dBFS幅度，不归一化、不调增益、不换声道、不重采样。', '',
              '数字零窗口以digital_zero=true和RMS_dBFS=null记录，不使用dB显示下限制造不存在的衰减；逐轮Off后0.25秒的100ms窗见case JSON。', '',
              '## 边界', '', *['- '+text for text in summary['limits']], '',
              '所有原始输入SHA前后保持。新增脚本和分析JSON/CSV/报告保留，无仿真scratch产生。']
    (args.out/'report.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps({'status': summary['status'], 'totals': totals, 'out': str(args.out.resolve())}, ensure_ascii=False))


if __name__ == '__main__':
    main()
