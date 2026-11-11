# CH569 PCM 采集工具

这个目录保存 PC 端工具：从 CH569 USB Bulk 或串口读取 FPGA 输出的带帧头 PCM，解析为原始流、纯 PCM、WAV 和统计文件；还提供假数据模式、GUI，以及 PCM/WAV 的统一 A/B 和参数突变测量。代码位于 `pcm_capture/`，双击启动器为 `launch_pcm_capture_gui.cmd`。

以下命令从**仓库根目录**运行。USB Bulk 模式默认面向 VID/PID `359f:3001`、Bulk IN `0x82`；实际设备和固件配置以采集时的记录为准。

```powershell
python -m host.pcm_measurement_reference.pcm_capture fake --duration 2 --pattern counter
python -m host.pcm_measurement_reference.pcm_capture usb --duration 10 --vid 0x359f --pid 0x3001 --ep-in 0x82 --label board_check
python -m host.pcm_measurement_reference.pcm_capture serial --port COM16 --baud 115200 --duration 10
python -B -m host.pcm_measurement_reference.pcm_capture.app
```

当前仓库只在 PC 端 `FakeReader` 中生成这种帧，尚未找到 CH569/HSPI 固件侧的真实帧生产者。`usb` 和 `serial` 模式因此只是读取入口，不能据此声称硬件链已经实现下述 CRC 契约。

## 帧完整性契约

帧头保持 `<HBBHIHH`（小端）：magic、version、flags、seq、sample_rate、payload_len、crc16。

- v1 是兼容格式，要求按“未校验”处理；`crc16` 不提供可信完整性证据，不能把 v1 采集称为 bit-perfect。
- v2 必须置 `FLAG_CRC16_CCITT=0x80`。CRC 使用 CRC-16/CCITT-FALSE：多项式 `0x1021`、初值 `0xffff`、无反射、`xorout=0`。
- v2 CRC 覆盖完整 14 字节帧头（计算时把头内 `crc16` 两字节清零）和 payload。CRC 错误或 v2 未置 CRC flag 的帧会被拒绝，不写入 `payload.pcm` 或 `audio.wav`。
- `stats.json` 分开记录 `protocol_valid_frames`、`frames_written`、`frames_crc_valid`、`frames_unverified`、`crc_errors`、`protocol_frames_rejected`、会话格式/采样率拒收、`seq_frames_missing` 以及重复/退序。
- 只有通过 v2 CRC 的序号才更新可信连续性状态；CRC 已损坏帧中的 `seq` 不参与后续缺口计算。v1 仍可写入，但其序号不作为可信连续性证据。
- `dropped_frames` 的语义是 `conservative_lower_bound`：`max(protocol_frames_rejected, seq_frames_missing) + session_rejected`。这样不会把同一个 CRC 坏帧和随后暴露出的序号缺口重复相加；各原因的原始计数仍单独保留。

从资源管理器双击 `host/pcm_measurement_reference/launch_pcm_capture_gui.cmd` 也可启动 GUI。启动器使用当前 `PATH` 中的 Python，优先启动 PySide6，缺失时回退到 Tkinter；USB/串口模式仍分别需要安装 PyUSB/pyserial。直接双击 `pcm_capture_gui.pyw` 会使用 Windows 当前关联的 Python。

每次运行默认在仓库根目录的 `captures/` 下建立带时间戳的目录：

- `raw.bin`：接收到的原始字节流，包含帧头。
- `payload.pcm`：解析后的单声道 signed 16-bit little-endian PCM。
- `audio.wav`：带 WAV 头的 PCM 音频。
- `stats.json`：帧数、丢帧、重同步、采样率和音频统计。

采集后检查文件结构和连续性：

```powershell
python -m host.pcm_measurement_reference.verify_capture captures\<本次目录> --expect-rate 48000 --require-audio --require-crc
```

`--require-audio` 用于预期有声音的测试；静音测试省略该参数。`--require-crc` 要求所有输出帧都通过 v2 CRC；不加时仍兼容 v1，但会明确打印“未校验、不能作为 bit-perfect 证据”的警告。此检查会核对 CRC、帧、丢失/重同步计数、PCM 字节数、WAV 格式与样本数，不代替音质或板级时序验证。

`wav_container_verified` 只说明 WAV 头、采样率和样本数与实际写入 payload 一致；`stream_integrity_verified` 才说明本次流没有协议错误、重同步、可信序号异常、格式切换或采样率切换。`wav_verified` 是两者都成立时才为真的兼容汇总字段。当前输出只支持单声道 s16le；STEREO/S32 或首个可写帧之后的采样率变化会被拒绝，拒收 payload 不会混入 WAV，且本次流完整性验收失败。

## 统一音频测量

单文件测量和 A/B 比较都接受 16-bit WAV；裸 `.pcm` 必须明确给出采样率和声道数。比较会拒绝采样率或声道数不同的输入。结果包含 peak、RMS、crest factor、clipping、DC offset、低/中/高频带相对能量和 spectral centroid，并可保存为 JSON：

```powershell
python -m host.pcm_measurement_reference.measure_audio captures\take_a\audio.wav --json captures\take_a\measurement.json
python -m host.pcm_measurement_reference.measure_audio captures\tilt_on\audio.wav --reference captures\tilt_off\audio.wav --json captures\tilt_ab.json
python -m host.pcm_measurement_reference.measure_audio captures\zipper_candidate\audio.wav --reference captures\zipper_reference\audio.wav --transition-index 24000 --window-ms 20 --json captures\zipper_ab.json
python -m host.pcm_measurement_reference.measure_audio take.pcm --sample-rate 96008 --channels 1 --json take.json
```

`--transition-index` 是每声道样本序号；参数突变窗口报告一阶差分的高频代理 RMS 和相对变化，用于定位 zipper/click 候选。它不是自动音质裁决。默认静音门槛为 `-70 dBFS`，低于门槛时 `spectral_trusted=false` 且 centroid 留空，防止静音或底噪产生假通过。频带和 centroid 使用最长 4096 个居中样本的 Hann 窗标准库 DFT，适合可重复对比，不替代完整实验室频响扫频。

`captures/` 已被 Git 忽略。需要在 Issue/PR 中引用一次实测时，记录 commit SHA、板卡与固件版本、命令参数、采集目录、`stats.json` 摘要，以及音频检查结果。只在确有必要时分享经过检查的样本，避免上传过大的原始采集文件。

当前工具尚需按真实 CH569 数据流完善；一次能生成 PCM 文件只能证明采集链路有数据，质量和连续性仍要核对 `stats.json` 与音频内容。
