# ESP32 MIDI 调试入口

2026-10-31。目标为现有 YD-ESP32-S3、ESP-IDF v6.1 与 AE350 prepared 控制工程。
本接口通过现有 Wi-Fi HTTP 接收 MIDI，沿唯一 SPI owner 送入 FPGA 的 Web RAW MIDI 源。
无需额外 USB、串口或 GPIO，不新增分区。新应用版本为 `ae350-midi-debug-20261031`。
代码和构建完成不代表已经更新板上应用，本任务不自动烧录或访问设备。

## 使用

电脑连接 ESP32 已有的 Wi-Fi AP，在本 `esp32s3` 目录运行 Python 3（仅标准库）。
先通过原控制页面选择要测的减法/KS/FM 引擎及音色，释放网页键盘，测试期间停止其他 Web 演奏。
脚本不会自动切换引擎、加载音色或修改音量、混响设置。

```bash
# 只读检查，无会话、无 MIDI 注入
python3 tools/midi_debug.py --report status.json status

# 通道1/C4/MIDI60，力度80，持续1000ms；自动发送对应 NoteOff
python3 tools/midi_debug.py --report single-note.json note 60 --velocity 80 --duration-ms 1000

# 原始十六进制 MIDI：通道1的 CC74=64；同样支持 PC、bend、SysEx、running status
python3 tools/midi_debug.py --report raw.json raw "B0 4A 40"

# 单音、低力度、和弦对比，回放前完整验证文件
python3 tools/midi_debug.py --report compare.json replay docs/midi-debug-example.json
```

默认地址为 `http://192.168.4.1`，可以在子命令前指定 `--url http://host:port`。
MIDI 通道参数为1..16，音高与力度为 MIDI 数值；原始流每次1..256字节。
原始 MIDI 的解释与支持范围由 FPGA 当前 MIDI 实现决定，HTTP 接收任意字节不保证其语义受支持。
例如 `raw "90 3C 50" --hold-ms 1000` 会持有会话约一秒；任意 RAW/回放应显式包含 NoteOff。

## HTTP 合同

- `GET /api/debug/midi`：只读 RAM 快照，含 `version=1`、`context_epoch`、`ready`、
  `reset_count`、全局 fault 和 Web 源的 session/queued/sent/dropped/flush/fault。
  不创建或续租会话、不发布事件、不从 HTTP task 操作 SPI。
- `POST /api/debug/midi`：直接复用原 `/api/v1/midi/raw` handler。
  原入口保持，两个名字共享序号、队列、会话与同一个 Web RAW 源；不是两个独立 MIDI 输入。
- 创建会话：`POST /api/v2/session` 空 body，header `X-FPGA-Control: 1`。
  保留返回的 token/client/context_epoch。会话创建后先发空键盘快照以等待 owner 就绪：
  `POST /api/v2/command`，纯文本 `TOKEN SEQUENCE CONTEXT 3 0 100 0`。
  命令与 RAW 共用严格递增 SEQUENCE。
- 注入示例：

```json
{"source":"web","bytes":[144,60,80],"token":77,"sequence":2,"context_epoch":7}
```

  上述77/7只是示意，必须替换为实际 session 值；所有 POST 都带 `X-FPGA-Control: 1`。
  最大2048字节 JSON/256 MIDI字节；复用原严格解析及上下文/租期/单源占用校验。
  202只表示 HTTP 入队。400/403/409/410/503需处理，不能当已经发声。
- Python 客户端一次只在途一个 RAW 请求，核对同 token 的 `sent_bytes` 模32位增量及
  dropped/reset/context，然后记录 `fpga_byte_ack`。该计数仅在 SPI owner 观察到 FPGA RAW
  命令确认后递增，不能证明 MIDI 已渲染或 PCM 已播放。
- 持音和回放间隔通过原空键盘快照约250ms续租，不注入 NoteOn，不自动接管别的 token。
  断连、Ctrl-C、进程退出后停止续租，沿原1200ms租期与服务/确认流程释放 Web 源。
  `note`命令在正常/异常退出时尝试对应 NoteOff；不会自动调用影响 USB/BLE 的全局 STOP。
- 上下文变化、占用、队列满、掉字节、链路复位或确认超时立即失败并保存记录；不自动重连或
  重发可能已执行的 NoteOn。收到失败时，保留报告，等待旧租期释放后明确重新启动测试。
  状态中的 `ready` 是 HTTP/RAW 快照可接收状态，实际 owner 仍会重新验证模板与路由。

## 回放与未来 PCM 对比

JSON为1..4096个对象，每个仅有 `at_ms` 和整数 `bytes`。时间从0开始允许相同时间，非递减，
上限600000ms。示例只发送单音和和弦，不自动改音色；所有事件在发送前完整校验。
脚本使用单调时钟，报告保留文件SHA、引擎上下文/初始状态、计划相对时间、实际 dispatch/ACK
时间及每次 HTTP 结果。报告用独占创建，拒绝覆盖旧文件；不指定路径时生成新的本地 JSON。

HTTP受Wi-Fi、排队和往返延迟影响，不承诺采样级时间或不同次运行逐样本相同。
当前 `pcm_capture=false`、`sample_accurate=false`；没有启用、配置或假定 USB PCM 采集。
后续应使用相同音色/参数/初始状态、MIDI顺序和复位条件，明确采样率、位宽、通道及采样点，
处理触发对齐和时钟漂移后，再分析峰值/削波、丢帧、波形差、频谱及逐样本一致性的适用范围。
此时的发送时间和 RAW byte ACK 不能当 FPGA 内部实际执行 MIDI 的采样编号。

## 验证边界

包内 `tests/run.sh` 包含实际 HTTP handler 的严格绑定、分段接收、队列背压与只读状态检查，
以及 Python 客户端的字节范围、租期、时序记录、counter回绕、重置/掉包/占用/超时/不重发测试。
主机测试模拟 SDK/HTTP 边界，配合官方 ESP-IDF v6.1 构建；不代表实板联网、听音或 PCM 对齐。
