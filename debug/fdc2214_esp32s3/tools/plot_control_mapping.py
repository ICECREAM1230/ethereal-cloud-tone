#!/usr/bin/env python3
"""Optional host plot of saved C replay results; matplotlib is not an MCU dependency."""
import argparse
import csv
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='New PNG path')
    parser.add_argument('--profile', type=Path, help='Defaults to profile.json beside the replay CSV')
    args = parser.parse_args()
    if args.output.exists() or args.output.with_suffix('.svg').exists():
        parser.error('Output already exists')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    from matplotlib.ticker import FuncFormatter
    font = Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
    if font.exists():
        font_manager.fontManager.addfont(str(font))
        plt.rcParams['font.family'] = font_manager.FontProperties(fname=str(font)).get_name()
    plt.rcParams['axes.unicode_minus'] = False
    with args.csv.open() as stream:
        rows = list(csv.DictReader(stream))
    profile = json.loads((args.profile or args.csv.with_name('profile.json')).read_text())
    if not rows:
        parser.error('No replay rows')
    start = float(rows[0]['time_s'])
    t = [float(r['time_s']) - start for r in rows]
    pitch = [int(r['ch0']) for r in rows]
    volume = [int(r['ch1']) for r in rows]
    fig, axes = plt.subplots(2, 2, figsize=(13, 7.4), sharex=True, layout='constrained')
    for ax, data, near, far, name, color in (
        (axes[0,0], pitch, profile['pitch']['raw_near'], profile['pitch']['raw_far'], 'CH0 原始码与临时音高范围', '#286cb6'),
        (axes[0,1], volume, profile['volume']['raw_near'], profile['volume']['raw_far'], 'CH1 原始码与临时音量范围', '#188b82')):
        ax.plot(t, data, color='#8c99ac', lw=1)
        ax.axhspan(near, far, color=color, alpha=.13, label='中值处理后的P5～P95')
        ax.set_title(name, loc='left', weight='bold')
        ax.yaxis.set_major_formatter(FuncFormatter(lambda value, pos:f'{value/1e6:.3f}'))
        ax.set_ylabel('原始码 / 1,000,000')
        ax.legend(loc='lower left', fontsize=9)
    low, high = min(pitch), max(pitch)
    ax = axes[1,0]
    if high > low:
        naive = [220 * 2 ** (2 * (high - value) / (high - low)) for value in pitch]
        ax.plot(t, naive, color='#abb4c0', lw=1, label='全局极值直接映射（对照）')
    ax.plot(t, [float(r['frequency_hz']) for r in rows], color='#286cb6', lw=1.5, label='C算法输出')
    ax.set_yscale('log', base=2);ax.set_yticks([220,311.127,440,622.254,880],['220','311','440','622','880'])
    ax.set_ylabel('音高频率（Hz，对数间隔）');ax.set_title('连续滑音：220～880 Hz', loc='left', weight='bold')
    ax.legend(loc='upper right', fontsize=9)
    ax = axes[1,1]
    ax.plot(t, [float(r['gain_linear'])*100 for r in rows], color='#188b82', lw=1.5)
    ax.set_ylim(-3,103);ax.set_ylabel('线性幅度增益（%）')
    ax.set_title('音量：平方曲线 + 静音回差 + 斜率限制', loc='left', weight='bold')
    for ax in axes.flat:
        ax.set_xlim(0,t[-1]);ax.grid(alpha=.18);ax.spines[['top','right']].set_visible(False)
    for ax in axes[1]:ax.set_xlabel('相对记录起点的时间（秒）')
    fig.suptitle('AE350 C算法离线回放 · 幅度告警样本参与预览', fontsize=17, weight='bold')
    fig.supxlabel('当前600帧拟合并回放；尚未验证真实手距线性、AE350运行时间或实板演奏延迟。', fontsize=10)
    fig.savefig(args.output,dpi=160);fig.savefig(args.output.with_suffix('.svg'))
    plt.close(fig)
    print(args.output)


if __name__ == '__main__':
    main()
