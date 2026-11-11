# ESP32 音量标定与四线 I²S 测试

2026-11-01。新增可选音量标定与独立测试音，默认原始采集配置保持不输出音频。本轮只修改/构建独立ESP32-S3工程，没有烧录，也没有修改AE350/FPGA工程。当前板上此前的raw固件不具备下面的新命令，需要后续安装本次音频版本。

## 接线与格式

四线指四根数字音频信号，**不包含电源和GND**；外部DAC与ESP32还需共地。ESP32-S3不直接输出模拟耳机信号。

| ESP32-S3建议GPIO | 信号 | 接外部DAC |
|---|---|---|
| GPIO15 | MCLK | MCLK/SCK（如DAC要求） |
| GPIO5 | BCLK | BCK/BCLK |
| GPIO6 | LRCLK/WS | LCK/LRCK/WS |
| GPIO7 | DOUT | DIN/DATA输入 |

保留FDC的SCL8/SDA18/ADDR17/SD4/INT16及UART0 TX43/RX44。GPIO可在 `main/Kconfig.projbuild` 的 `FDC_AUDIO_*` 中修改；MCLK可设-1以不引出。运行前检查引脚，拒绝互相重复及FDC、UART、USB、Flash/PSRAM和当前已知INT16等冲突。

格式为**标准Philips I²S主机发送，48kHz、双声道相同的16位有符号PCM，每槽32位**。LRCLK=48kHz，BCLK=3.072MHz，MCLK=12.288MHz（256Fs），WS宽度32。DMA内存是交错的int16左右样本，不是把16位数据手工左移32位；ESP32-S3外设负责槽内左对齐和一位延迟。外部DAC须支持该格式；未知codec所需的I²C配置、功放使能或模拟电源不在本次通用输出内，未进行DAC板测。

## 先区分音量小的来源

本程序固定音高440Hz，专门测音量。数字音量有两层：传感器映射 `gain_q16=0..65536`，以及测试音总电平 `audio level=0..100`。正弦本身保留20%数字峰值余量，因此实际峰值约为 `0.8×gain×level/100` 满量程。

启动模式为off、level=25，PCM为0。`audio reference`忽略传感器，输出指定level的固定测试音；`audio sensor`才使用所选通道的映射增益。如果reference也很小，应检查DAC/功放/扬声器及其音量；如果reference正常、sensor很小，先看 `gain_q16` 和远近端点，避免只把下游增益放大。

默认通道CH1、临时最大音量原码13246586、静音原码13174639，继承旧数据的拟合范围，**不是当前手势的正式标定**。归一化 `u=clamp((raw-loud)/(mute-loud),0,1)`；平方曲线增益 `(1-u)^2`，也可切线性 `1-u`。例如u=0.6时平方增益只有0.16（16%），即使读数一直变化也可能一直觉得声音小。方向相反时两个端点仍可正常工作，不强制loud大于mute。

## 串口调试步骤

使用当前FT232R串口，115200波特。**先在8766 GUI点击断开**，避免GUI与命令终端同时读串口。GUI仍支持原始曲线，但不发送标定命令、也不显示新增音频遥测。

终端使用支持交互输入的串口工具，例如在本目录执行：

```bash
/home/ice/.espressif/python_env/idf6.1_py3.12_env/bin/python -m serial.tools.miniterm \
  '/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_A5069RR4-if00-port0' 115200 --eol CR --rts 0 --dtr 0
```

1. 输入 `stream off`，减少滚动输出，四路采集与音频仍继续。输入 `audio show` 检查 `error=ESP_OK` 与引脚配置。
2. 输入 `audio level 25`、`audio reference`。确认440Hz测试音和外部DAC输出，再按需逐步调整level。`audio off`渐变停止发声；I²S时钟保持。
3. 输入 `vol channel 1`，该命令会停止发声、清除待定标定和旧采样历史。将手放在希望**最大音量**的位置，输入 `vol capture loud` 并保持约1.3～2秒；`vol show` 中capture_result=2、capture_count=128表示完成。
4. 将手放在希望**静音**的位置，输入 `vol capture mute` 并保持。两个端点都成功后，pending_mask=3。
5. 输入 `vol apply`，再 `vol show`。`command_ok=1`、calibrated=1表示生效；跨度小于64码或小于稳定段噪声估计20倍时拒绝，现用参数不变。失败时重新采稳定端点。
6. 输入 `audio sensor`，移动音量手观察/试听；`vol show` 查看u、gain_q16与signal_gain。`vol curve linear`可对比平方曲线手感，参考模式则用于继续核对音频增益。
7. 输入 `vol export`，保存当前生效的端点、噪声估计和通道/曲线。参数只在RAM，重启恢复默认；旧AE350核心默认平方曲线，若选linear后迁移需同步其曲线实现。测试音level不等于AE350传感器标定参数。

命令经过队列在标定任务应用；紧接着show可能还是上一拍，请约几十毫秒后再看command_ok。端点采集只计命令开始后的128个新测量，不重复计算同一帧；5秒不足会失败，I²C故障/重新初始化也会使当前采集失败。建议分别重复远/近稳定段，检查另一只手和天线串扰。

| 命令 | 作用 |
|---|---|
| `audio show` / `vol show` | 显示音频错误、模式、参数、待定标定、最新命令结果和遥测 |
| `audio off` / `audio reference` / `audio sensor` | 静音 / 固定增益参考 / 传感器控制 |
| `audio level 0..100` | 测试音总电平（默认25%） |
| `audio tone 20..2000` | 固定测试频率，默认440Hz |
| `vol channel 0..3` | 选择标定通道；先停止声音并清旧历史 |
| `vol capture loud` / `vol capture mute` | 分别采集最大音量/静音端点中值和MAD噪声 |
| `vol range LOUD_RAW MUTE_RAW` | 手工暂存一对端点；仍须apply |
| `vol apply` | 校验并应用两端点，清滤波历史/增益，从静音恢复 |
| `vol curve linear` / `vol curve square` | 线性/平方音量曲线 |
| `vol export` | 输出当前生效参数；不写Flash/NVS |

## 质量与遥测

原 `sample` / `range` CSV字段完全保留。新增 `volume` 记录按原节流间隔输出：

```text
volume,time_us,channel,raw,u,gain_q16,level_percent,signal_gain,mode,quality,capture_result,capture_count,capture_aw,calibrated,driver_error
```

mode：0=off、1=reference、2=sensor。capture_result：0=idle、1=active、2=done、3=failed。AW允许参与试验但质量仍标amplitude_warning，capture_aw记录告警数；WD、频率比越界、非法或陈旧原码不进入标定。STATUS跨扫描归属不确定时保守保留AW/WD诊断。sensor模式343ms无有效样本后淡出，硬传输故障也使增益向零；reference模式明确绕过传感器，不因其断开停止。I²S写入错误锁存并停止TX，show/volume的driver_error会报告失败。

原始采集、10ms标定处理、I²S DMA输出及串口输出分别运行。原始帧通过单槽覆盖队列提交，不让串口或DMA等待阻塞I²C；这不是全帧无损记录。标定中值与噪声来自实际被处理的新测量，不能把旧CSV极值自动当最终端点。

## 构建与验证

`CONFIG_FDC_AUDIO_ENABLE`默认n，旧原始模式不启用I²S任务/引脚；音频配置片段见 `config/audio-i2s.defaults`。本轮将已确认接线的UART配置复制到新的verification目录，再合并该片段编译，不覆盖旧USB/默认配置。实际构建结果路径与SHA见本轮报告。

主机验证：

```bash
python3 -B tools/test_volume_audio.py --output verification/volume-audio-new-test
```

每次须使用不存在的新输出目录。覆盖纯C映射、AW/WD、超时、重复/陈旧数据、端点捕获/失败保留、命令、引脚冲突、PCM频率/双声道/峰值/淡出，以及原驱动/CSV/GUI回归。ESP-IDF实际编译分别检查音频开/关；主机测试或编译不能证明DAC硬件发声、引脚波形/时钟精度或声音大小。

API依据：Context7当前未列出ESP-IDF6.1，使用本机 `/home/ice/esp/esp-idf-v6.1/components/esp_driver_i2s/include/driver/i2s_std.h`、`i2s_common.h` 与 `docs/en/api-reference/peripherals/i2s.rst` 核对；尤其槽宽改32后WS宽度同步设32。未将其他版本示例视为6.1已验证实现。
