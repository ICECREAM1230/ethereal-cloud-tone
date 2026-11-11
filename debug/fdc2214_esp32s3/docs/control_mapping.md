# AE350用CH0音高/CH1音量算法

2026-10-30。核心为独立C99实现；只复制 `ae350/theremin_control/theremin_control.c` 和同目录 `.h` 即可加入已有AE350 SDK工程。`ae350/ae350_theremin_example.c` 是不绑定BSP的调用示例。本次不接入FPGA/ESP32主工程，不写设备，不更改现有GUI。

## 先看默认参数

这些值由 `/home/ice/下载/fdc2214-raw-session-17 (1).csv` 的600帧离线计算：因果三点中值后P5/P95作为临时边界，原始差分MAD估算噪声。SHA256：`8458b352465caf6078c000575d95127db6707d9c9323ed9764d154092070d1ae`。用了同一份数据拟合和回放，没有独立测试集或真实手距标签。

| 控制 | 假定远端原始码 | 假定近端原始码 | 噪声估计 |
|---|---:|---:|---:|
| CH0音高 | 13,360,095 | 13,309,071 | 约321码 |
| CH1音量 | 13,246,586 | 13,174,639 | 约985码 |

它们表示这份记录的**临时观测范围**，不是已证明的物理远近端。默认“手靠近原始码下降”，CH0靠近升音，CH1靠近减音。方向不对时交换对应通道 `raw_far/raw_near`，不改公式。更换天线/走线后需重新确认两个端点。

## 每路独立处理

```text
新的原始数据及诊断
    → 硬错误筛选（幅度告警可接收，标记保留）
    → 因果三点中值，抑制单点尖峰
    → 固定端点归一化并钳位至0..1
    → 小回差，抑制静止抖动
    → 1€自适应低通，静止平滑、运动提高响应
    → 连续音高 / 平方音量、静音回差、输出斜率限制
```

归一化：`u=clamp((raw_median-raw_far)/(raw_near-raw_far),0,1)`。先做整数有符号差，再转float，避免无符号下溢和大基数相减精度损失。

回差宽度是 `min(0.015,1.5*noise_sigma/span)`。跨出小回差时连续跟随，不量化到半音；端点可精确达到0/1。过期后重新接收数据，会清该路旧中值/滤波历史。

自适应低通独立实现[1€作者公开公式](https://gery.casiez.net/1euro/)：`alpha=dt/(dt+1/(2*pi*cutoff))`，`cutoff=min_cutoff+beta*abs(filtered_velocity)`；使用实际微秒间隔，单位是归一化位置。本次未复制第三方源码；[作者Python实现](https://github.com/casiez/OneEuroFilter/tree/main/python)用于核对接口和方法。ACM论文页面返回403，未声称读取全文。

## 输出含义

- `pitch_code = round(4864+2048*u_pitch)`，默认连续两个八度，参考5888。当前96kHz音源查表约对应220..880Hz，中点约440Hz；`pitch_delta=pitch_code-reference` 是有符号偏移。
- 音高每1024码一个八度，保留滑音。核心不计算pow/exp/sin，不生成PCM；频率展示关系为 `f≈440*2^((pitch_code-5888)/1024)`。
- `gain_linear=(1-u_volume)^2`；`gain_q16=round(gain_linear*65536)`，0..65536需要17位，不能放进16位变量。
- 靠近到 `u_volume>=0.985` 静音；退回 `u_volume<=0.95` 才重新开启，减少临界点反复开关。增益默认最大10/秒变化，完整0..1斜坡约100ms。
- 音高最大24八度/秒。下游FPGA/软件音源仍应在音频帧时基插值或限制斜率，不能将低速控制值直接当作音频采样。

## 告警与丢数据

`flags=(DATA_MSB>>12)&3`，bit0幅度告警，bit1看门狗。`accept_amplitude_warning=true` 是当前试验策略，接收后质量仍是 `THEREMIN_AMPLITUDE_WARNING`，不冒充无告警数据。

I²C失败、已知超时和其他硬错误通过 `hard_error=true` 传入；看门狗、非法原始码及越界仍拒绝，旧值年龄不会刷新。核心沿用原工程 `raw<0x04000000` 的频率比检查，它不是28位编码最大值，也不能检查所有模拟电气条件。

CH0/CH1不必同帧接受。短期缺某一路，继续用它最近未过期的值，另一通道照常更新；默认343000us超时后保持音高，将音量向0衰减。默认超时是按10Hz导出间隔估计，AE350直接采集时应结合实测周期调整。调度本身长时间中断后重新进入，先置静音，再从后续tick重新淡入。

短GUI导出没有完整STATUS/I²C诊断：回放中mask清零且带AW按宽松策略接收；mask清零但没有AW则视为原因不明而拒绝。不能从短CSV恢复已经丢失的诊断信息。AE350实时接入时应直接使用驱动的完整错误信息，勿把“仅因AW置位的旧valid_mask=0”一概传成硬故障。

## AE350调用契约

1. 用 `theremin_default_config()` 取得配置，可修改端点、音域和参数，再 `theremin_init(&state,&config)`。坏配置拒绝并保留原有实例；合法跨度至少64码且不小于20倍噪声估计。
2. 使用SDK计时器提供的单调64位微秒时间戳。CPU时钟计数需先换算，不假设AE350频率等于FPGA的50MHz。时间相等/回退会返回false；系统重新起算时间时先 `theremin_reset()`。
3. 只有新测量才把该通道样本设 `fresh=true`；不要重复提交同一读数刷新年龄。两路独立，没有新值可传NULL或 `fresh=false`。
4. 即使无新传感数据，也要周期调用 `theremin_update()`，让超时和输出斜坡运行。`hard_error=true` 即使 `fresh=false` 也保留故障质量标记。
5. 一个任务/主循环拥有一个实例；中断可通过队列/缓冲交付数据，不并发改同一状态。没有malloc、线程、外设寄存器或OS接口。
6. 把 `pitch_code` 或有符号 `pitch_delta`，以及 `gain_q16` 作为一组控制载荷交给发声通道；跨处理器/FPGA时按既有协议打包，不直接依赖此C结构体的内存布局。

保留AE350 SDK原有编译器、ISA/ABI和浮点设置。本模块使用float和标准C头文件，不假定硬件FPU；运行时间和软浮点开销需实机测量。不使用 `-ffast-math` 绕过有限值检查。主机回放工具的pow只用于显示Hz，不进入AE350核心。

## 杜邦线漂移怎么处理

三点中值、回差和低通能抑制孤立尖峰与小抖动，不能区分持续电干扰与真实手势。演奏过程中不自动改基线/量程，防止长音被“校正”掉。

把双手明确放到远端，再用 `theremin_rebaseline(&state,new_pitch_far,new_volume_far)` 做显式回零；它保持两个跨度，清旧样本并从静音重新开始。此操作只修正小偏移；接线或天线明显变化仍要重测远近跨度。不要在演奏中自动调用。

| 感受 | 优先调整 |
|---|---|
| 手静止时音高抖动 | 降低 `min_cutoff_hz`，或稍增 `deadband_sigma` |
| 快速动作跟随慢 | 增大 `beta`；再检查中值窗口和采样率 |
| 一小段手距变化太大 | 增大实测近远跨度，或缩小 `pitch_max-pitch_min` |
| 音量接近静音时反复开关 | 拉开 `mute_close/mute_open`，确认近端能稳定达到 |
| 相同手位整体偏移 | 双手远离后显式回零，检查接线；不在演奏中追踪基线 |

## 主机验证与查看曲线

```bash
python3 -B tools/test_control_mapping.py \
  --input '/home/ice/下载/fdc2214-raw-session-17 (1).csv' \
  --output verification/my-c-mapping-test

uv run --no-project --with matplotlib python -B tools/plot_control_mapping.py \
  verification/my-c-mapping-test/mapped_controls.csv \
  --output verification/my-c-mapping-test/controls.png
```

输出目录必须是新的，避免覆盖已有结果。Python仅编译/测试/绘图，AE350只需要C文件。测试包含普通C99、ASan/UBSan、调用示例编译、独立通道、告警、硬错误、端点、尖峰/持续动作、静止抖动、超时/重入、基线移动及时间/配置边界。临时主机二进制测试后删除，日志/CSV/图保留。

目前证据是主机功能和600帧离线回放；尚未用AE350 SDK交叉编译、测CPU周期或实板验收。10Hz导出的回放不能证明约142Hz实际输入下的延迟；没有改动设备、主工程或现有GUI。
