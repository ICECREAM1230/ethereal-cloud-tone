> 2026-11-12 公开源码入口：`sim/config/run_control_latency.py --host-only --output <新的结果目录> --work-dir <新的外部临时目录>` 执行实际 C 与 AT04 解码回归，明确跳过 RV32 检查；完整检查通过 `--rv32-prefix` 选择已安装工具链。N20 与 AT04 的纯离线入口为 `python3 -B sim/tb/test_note20_uart.py -v` 和 `python3 -B sim/tb/test_ae350_t0t3.py -v`，临时位置可用 `TMPDIR` 指定。可选 RTL 仿真入口从 PATH（或 `VERILATOR`）取工具，支持 `ECT_RESULTS_DIR` 与 `ECT_SIMULATION_TMP`；公开前此次只执行了主机 C/解码测试，没有新 RTL 仿真或硬件采集。D2XX 读取工具须显式选择已安装库或配置系统库搜索路径，见 `scripts/read_debug_uart.py --help`。

# AE350 串口延迟测量代码包（v2：数据描述已更新）

整理日期：2026-11-04（JST）。来源：独立实验工程 ae350_midi_pcm_latency_lab_20261030。

**最新数据入口：`latest_data/uart.txt`。本更新包的所有数据示例、计数说明和统计均以用户指定的这份文件为准。** 它是N20/N201计数减半派生文本；当前代码仍为T04，两者按各自版本解析。

## 1. 包的用途

这是当前 T04 的测量组件、AE350 C 固件相关代码、接收/解码/报告工具、单元测试及集成接线参考。保留原工程目录层级，原文件按字节复制，没有为打包修改生产代码。

本包不是完整可独立综合的 FPGA 工程：未包含三种合成器的全部业务源码、完整官方 GPRJ、AE350 生成/保护 IP、工具安装、位流或 Programmer。integration_reference_not_standalone 分组用于理解如何接入原工程，不能将几个顶层文件直接当成独立工程构建。

目录和每个文件的用途分组见 MANIFEST.json；SHA256SUMS 覆盖本包文件（校验清单自身除外）。

## 2. 先看哪些代码

| 内容 | 入口 |
|---|---|
| 硬件 T0/T1/T2/T3、输入编号、静音与异常 | rtl/ae350_latency_capture.sv |
| 原始结果 APB 窗口、UART byte mailbox | rtl/ae350_latency_apb.sv |
| 事件跨域封装 | rtl/midi_pcm_latency_probe.sv 中 midi_pcm_latency_event |
| held-data 跨域握手 | src/common/cdc/held_bus_cdc.sv |
| 实际串口 TX | src/midi_decode/debug/midi_diag_tx.sv |
| AE350 结果表、差值、CRC、空闲发送 | firmware/control_latency.h 和 control_latency.c |
| 主循环接入/空闲资格 | firmware/control_main.c 和 control_service.c |
| 首个 ON BEGIN 打点 | control_migration/rtl/control_link_apb.sv |
| 真正播放 PCM24 观测点 | src/pcm_post/pipeline/pcm_request_audio.sv |
| 顶层接线 | rtl/control_lab_top.sv、src/board/synth_system.sv |
| 原始串口接收 | scripts/read_debug_uart.py |
| 当前 AT04 严格解码及均值 | scripts/read_ae350_t0t3.py |
| 原始计数/原文/均值页面 | scripts/render_t04_report.py |

完整原理说明：docs/latency/current-measurement-principle-and-path-20261104.md。
本文档及原理说明的数据段落均已改用本次uart.txt；不携带此前其它数据的截图和样例状态文件。

## 3. 当前测量定义

- T0：实体 USB cable0、CIN9、velocity>0 的完整 Note On 在 USB Host 前端被接纳。
- T1：该音符首组 ON job 的 NEW BEGIN 成功进入真实 APB 发送队列，事件被 control48 观察。
- T2：同 generation/revision/voice/engine 的 COMMIT 执行成功并返回确认，事件被 control48 观察。
- T3：真实播放装载的首个完整 PCM24 样本对满足 L!=0 或 R!=0，事件被 control48 观察。+1/-1 都触发，早于 DAC16 截断。
- 四点统一 48,000,000Hz、uint64 计数。时间(us)=差值/48。T1/T2/T3 含 CDC 观察开销；分辨率不等于实板绝对精度。
- AE350 保存20条，每条26word，其中前18word保留硬件原始信息，后8word为四组u64差值。
- 前20条按输入编号，不补号；失败占位、过快可loss并缺号。前置256个有效双零播放帧；1秒超时。
- T3-T2 可为负，不能改为0；缺失时间戳的0字段不是零延迟。
- 正常 UART 帧为 AT04 + 208hex + 空格 + 4hex CRC + CRLF，共220B，200000/8N1。
- CRC16/CCITT-FALSE，初FFFF/poly1021，对26word各自小端4字节计算。flags=0x1f且所有一致性/因果检查通过才是单条成功。

## 4. 解压后的离线验证（不访问设备）

先进入解压出来的顶层目录，然后校验文件：

```bash
sha256sum -c SHA256SUMS
python3 -B sim/tb/test_ae350_t0t3.py -v
```

Python 测试只使用标准库，当前预期27项。

以下命令编译并执行真实 control_latency.c 的 host 单元测试：

```bash
mkdir -p sim/results/package_host_check
gcc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -Ifirmware firmware/control_latency.c sim/tb/test_control_latency.c \
  -o sim/results/package_host_check/test_control_latency
./sim/results/package_host_check/test_control_latency sim/results/package_host_check
```

测试生成的 engine-0/1/2-first20.bin 分别是 SUB/KS/FM 的测试 UART 字节，不是实板数据。例如：

```bash
python3 -B scripts/read_ae350_t0t3.py \
  --input sim/results/package_host_check/engine-0-first20.bin \
  --output sim/results/package_host_check/sub-decoded.json --engine SUB
```

也可验证真实控制服务的空闲判据：

```bash
gcc -std=c11 -O1 -Wall -Wextra -Werror \
  -Ifirmware -Icontrol_migration/include \
  firmware/control_service.c firmware/control_compile.c \
  control_migration/firmware/control_runtime.c \
  control_migration/firmware/midi_parser.c \
  control_migration/firmware/voice_manager.c sim/tb/test_t04_idle.c \
  -o sim/results/package_host_check/test_t04_idle
./sim/results/package_host_check/test_t04_idle
```

上述 host C 不提供 AE350 指令周期/WCET 模型。

## 5. 最新数据、原计数与统计

使用 `latest_data/uart.txt`，11,214B / 42条完整N20帧，20唯一ID、22相同重放、19有效、ID13无效。有效记录均为RAW1→SUB，KS/FM没有有效数据。逐帧CRC和格式通过，无冲突。

按文件50MHz和现有计数重新计算：

| 区间 | 有效数 | 平均 |
|---|---|---|
| M0→M1 | 19 | 1.837270526316 ms |
| M1→PCM | 19 | 5.016738947368 ms |
| M0→PCM | 19 | 6.854009473684 ms |

这些M0/M1/PCM及timeout已在此前导出中整数÷2并重算CRC，Hz字段仍为50MHz；本次没有再次改写。N201采用abs(PCM16)>1终点，不能当作新的T04/48MHz首非零PCM24测量。

原文、原始计数CSV、解码JSON和来源/精确均值位于 `latest_data/`。解码命令：

```bash
python3 -B scripts/read_note20_uart.py \
  --input latest_data/uart.txt --output latest_data/redecoded.json
```

预期exit2，CAPTURED_WITH_INVALID_MEASUREMENTS，stream_integrity=PASS，19 qualified。ID13保留而不计入有效平均；不能改成20条全有效。不要用AT04解码器读取这份N201文件。

本包保留T04报告生成器作为代码组件，但它的T0..T3布局不适用于本次N201；最新数据请查看本目录的原文、CSV和JSON。没有用其它数据的截图替换本文件证据。

## 6. 接收新的真实数据

先有身份已核对的对应实验位流和独占板卡，再持续接收，然后从实体USB键盘逐条触发。每批固定一个engine，释放前音并等完整PCM归零；三路分批，当前无rearm，模式切换不会清空前20条。

本包没有一运行就烧录的会话脚本。scripts/read_debug_uart.py 只是可复用的D2XX原字节接收入口，需本机已有FTDI库/连接条件和独占接收；它不取得工程共享板锁、不修改驱动绑定。库路径写在代码中，不能直接假设另一台机器路径相同。

原机器60秒接收示例，输出目录须不存在：

```bash
python3 -B scripts/read_debug_uart.py \
  --seconds 60 --baud 200000 --output sim/results/t04_manual_sub_01
```

该脚本单次上限60秒，慢按完整20条可能需要更长的持续采集流程；不是完整批次保证。
只有 capture.bin 是字节级原件；capture.txt 对非ASCII错误字节会replace。
接收器内置诊断解析只识别旧AE35，显示NO_DIAGNOSTIC_RECORDS不能判断AT04失败；必须再用当前解码器：

```bash
python3 -B scripts/read_ae350_t0t3.py \
  --input sim/results/t04_manual_sub_01/capture.bin \
  --output sim/results/t04_manual_sub_01/decoded.json --engine SUB
```

KS/FM改 --engine KS/FM 并使用独立批次目录。解码器exit0表示完整批次PASS，exit2表示未通过/缺项。
不要把旧N20 reader、旧N201的50MHz或指定100MHz离线换算套到当前AT04。

## 7. RTL仿真、固件构建与集成边界

包内小范围capture/APB/USB-padding TB及其生产依赖齐全。原runner保留实际代码，包括本机oss-cad入口、带分号的仿真盘目录和结果路径；它们不是跨平台通用启动器。在原环境可运行：

```bash
python3 -B sim/config/run_ae350_latency_capture.py
python3 -B sim/config/run_ae350_latency_apb.py
python3 -B sim/config/run_t04_usb_padding.py --label check
```

sim/config/run_control_latency.py 除host测试外还从原 firmware/build/control-build.json 定位RV32编译器；本包不附build目录，跨机器使用请采用第4节host命令，RV32检查需另行提供实际交叉工具链。
scripts/build_control_firmware.py 可重编C及ITCM初值，依赖实际RV32工具链。编译C后还必须经官方AE350 IP重生成及完整工程重新实现，不能只换bin就当成板上更新。本包不含完整IP/业务工程，不能独立走完这一流程。

通用 control_lab_top 的 ENABLE_MIDI_NOTE20_TEST 默认0；专用 midi_pcm_latency_lab_top 默认1，旧LAT1 observer关闭，UART200000。constraints和check_note20_sdc.py仅供完整官方工程集成参考，依赖真实综合网表对象；不能在缺少工程时声称checker/STA通过。

## 8. 本更新包状态

本次按用户提供的uart.txt替换全部数据描述，最新文件为42帧/20唯一ID/19有效/ID13无效，统计见第5节与latest_data/summary.json。数据字节不改、无效记录不删，来源与格式明确标注。

当前代码仍为T04；保存的构建交付状态为padding修复综合验收PASS、PnR队列WAITING。新的SUB/KS/FM各20条有效T04测量并未由这份N201文件补齐，STA仍须对应实现验收。

本次只更新包内数据、说明及其配套解码入口，不修改生产RTL/C、配置或冻结输入，不启动综合/PnR/烧录。此前原包及历史证据在工程中保留，本更新包不再携带被替换的数据样例、截图、历史数据验收JSON和两份阶段历史文档。
