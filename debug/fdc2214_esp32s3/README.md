> 2026-11-12 公开源码：离线检查不访问设备。`tools/test_host.py --output <新的结果目录> --work-dir <外部临时目录>`，`tools/test_control_mapping.py --output <新的结果目录>` 和 `tools/test_volume_audio.py --output <新的结果目录>` 可重跑 C、PCM、CSV 和 GUI 模型检查。后两个入口使用 Python 临时目录，可通过 `TMPDIR` 指定外部位置。`tools/build.py` 从已激活的 `IDF_PATH` 或显式 `--idf` 选择 ESP-IDF 6.1，不再依赖维护者本机安装路径。公开源码不包含下文提到的历史 verification 构建产物。

# ESP32-S3 独立 FDC2214 四通道原始数据调试

2026-10-29建立的独立工程，默认采集 CH0、CH1、CH2、CH3 的28位原始值、诊断与数值范围；不依赖或修改主工程。

2026-11-01新增可选**音量标定和四线I²S测试音**，见 [调试说明](docs/volume_i2s.md)：MCLK15/BCLK5/LRCLK6/DOUT7可配置、CH1默认音量、固定440Hz、启动静音，支持参考/传感器模式、端点采集/手动设置/导出。通过 `CONFIG_FDC_AUDIO_ENABLE` 启用；原raw默认不启用。此次只准备代码/构建，不烧录，不增加FPGA通信、无线、OTA或PSRAM功能。

**通用默认配置仍保留未接线门禁；本轮已按用户确认的GPIO和40MHz外部参考构建并烧录实板配置，结果见下一节。** 默认镜像只提供USB诊断和命令，不初始化FDC GPIO/I²C。USB控制台使用ESP32-S3原生USB引脚；烧录、软件启动、FDC通信及有效采样分别验证。

## 2026-10-29实板配置与当前结果

用户已接SCL=GPIO8、SDA=GPIO18、ADDR=GPIO17、SD=GPIO4、INT=GPIO16，确认模块外部40MHz及3.3V外部上拉。接线片段见 `config/wiring-gpio8-18.defaults`；它是defaults片段，完整可重建配置在 `verification/bringup_20261029T213113/sdkconfig.usb.wired`。检测到本板16MiB Flash，使用 `partitions.16mb.csv`；下面通用默认8MiB/未接线配置仍保留。

已备份原Flash、构建、烧录并验证应用启动。用户修复供电并确认共地后，**通信和四路原始数据输出已经恢复**，实测完整帧采集率约142.6帧/秒，USB观察记录约10帧/秒。仍有大量DATA幅度告警，CH0在该次12秒记录中没有有效范围；当前结果只证明能通信和持续读数，不能认为LC传感器已经调好。复测证据见 [ground_retest_20261029T222633](verification/ground_retest_20261029T222633/serial.log)。INT暂未用于中断。

供电异常期间的0x7E读失败、0x2A/0x2B超时及SCL/SDA为低仍保留在[此前报告](verification/bringup_20261029T213113/summary.json)，作为历史失败基线。构建时可使用 `--output <新的结果目录>` 保留每次结果。

## 本机实时GUI

`gui/` 提供独立浏览器界面，显示CH0～CH3的28位原始整数、各路曲线、观测最小/最大/跨度、DATA告警、设备有效样本累计范围及采集/显示速率。不计算电容、距离、音高或音量。

在本目录启动（使用已安装pyserial的IDF Python）：

```bash
/home/ice/.espressif/python_env/idf6.1_py3.12_env/bin/python -B gui/app.py \
  --serial '/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_84:C7:BB:79:D2:14-if00' \
  --log-dir verification/gui-session
```

然后打开 [http://127.0.0.1:8765](http://127.0.0.1:8765)。省略 `--serial` 时只启动界面，需在侧栏选择串口并连接。服务器仅监听本机。串口由唯一后台读取线程独占，使用期间不要同时打开其他串口监视器。

- 曲线最多保留600帧，可选择最近10/30/60秒。观测范围从连接或清空时开始累计，包含告警原始值；设备累计统计来自固件 `range`，仅包含设备认定的有效样本，二者不可混用。
- “暂停显示”仅暂停页面读数和曲线显示，后台继续收集。“清空本窗口”只清GUI历史和观测范围，保留设备有效范围，不向ESP32发送 `clear` 或任何其他命令。“导出CSV”保存当前最多600帧及其告警/有效标记。
- 串口断开或超过2秒无样本会标记为过期，保留最后读数供查看。设备重启、epoch改变或sequence回退会清除旧会话数据；重新连接需点击连接按钮。
- 打开串口前将DTR/RTS设为False，程序不主动发送复位/配置命令；USB驱动或开发板仍可能在端口打开时重新枚举或重启，不能保证打开完全没有硬件副作用。GUI会识别启动信息并清除旧会话。
- 串口原始日志在 `--log-dir` 内轮转保存，避免无限增长；日志是节流观察数据，不是全部采样帧的无损记录。

GUI主机测试：

```bash
/home/ice/.espressif/python_env/idf6.1_py3.12_env/bin/python -B -m unittest discover -s gui -p 'test_*.py'
```

## AE350上的音高/音量算法

用户指定的独立C99算法位于 `ae350/theremin_control/theremin_control.c` 和同目录 `.h`，调用示例为 `ae350/ae350_theremin_example.c`。默认CH0控制连续音高、CH1控制平方音量，采用当前数据的临时固定范围、中值去尖峰、回差和1€自适应低通；幅度告警保留参与预览。核心不使用动态内存、OS或设备接口，不作为ESP-IDF组件注册。

算法、默认参数、AE350接口、显式基线校准和主机回放见 [control_mapping.md](docs/control_mapping.md)。未接入主工程或现有GUI，未交叉编译/烧录AE350。

## 工程和硬件边界

- 固定目标：ESP32-S3。通用默认为8MB Flash、DIO/80MHz；本轮esptool实测16MiB，已增加并使用相应硬件配置和分区。
- 默认输出：USB Serial/JTAG，连接开发板原生USB口，使用GPIO19/20；该口与USB转串口桥的UART口不同。UART0版本可选，默认115200波特、S3 TX43/RX44；切换后须使用对应的USB-UART桥或3.3V串口连接。
- 单一factory应用覆盖8MB镜像可用应用区域，无OTA/NVS分区。没有新增第三方组件或网络依赖；使用本机 `/home/ice/esp/esp-idf-v6.1`。
- SDA、SCL、SD、ADDR全部默认-1。SDA/SCL必须指定不同的有效输出GPIO；SD/ADDR可保持-1，分别表示模块外部固定SD低、ADDR固定到对应地址电平。若由GPIO控制ADDR，必须同时指定SD，以便只在Shutdown期间设置ADDR。
- 软件拒绝USB19/20、Flash/PSRAM26..37、未提供的22..25、启动/输入限制0/3/45/46及重复引脚；UART模式额外拒绝控制台使用的引脚。软件检查不能代替后续实际板级连线核对。
- 需要确认3.3V兼容逻辑、共地、外部I²C上拉和独占总线后才设置 `FDC_WIRING_CONFIRMED=y`。模块电源端允许的电压取决于模块自身稳压/电平转换，不能把模块5V电源直接当作SCL/SDA上拉电压；FDC裸芯片推荐VDD为2.7..3.6V。
- 参考时钟默认“未确认”。后续明确选择模块提供的2..40MHz外部CLKIN（配置示例40MHz、FREF÷1），或内部振荡器（35..55MHz，FREF默认÷2，确保最坏情况下不超过40MHz）。内部模式的CLKIN接法须按模块电路和TI手册确认，不要将已有时钟输出短接到地。本工程不产生外部参考时钟。

## 配置、构建与后续运行

默认离线构建和测试：

```bash
python3 -B tools/test_host.py
python3 -B -m unittest discover -s tests/host -p 'test_*.py'
python3 -B tools/build.py --console usb
python3 -B tools/build.py --console uart
```

`build.py` 激活指定IDF，使用自己新建的ASCII临时构建目录与sdkconfig，复制必要结果到 `verification/build-usb/artifacts/` 或 `verification/build-uart/artifacts/`，成功后删除自己创建的编译中间目录；失败目录保留供诊断。它只运行build，不自动访问串口、复位或烧录设备。保存的 `flasher_args.json`/`compile_commands.json` 仍引用原构建临时路径，属于证据，不能直接当作可重定位烧录命令。

接线明确以后，用户可在本工程目录激活IDF，并通过 `idf.py -B build menuconfig` 的 **FDC2214 standalone raw debug** 配置实际GPIO、确认项、时钟、地址与测量参数。随后 `python3 -B tools/build.py --console usb --config sdkconfig` 可用配置副本构建，不修改原配置。参考模板 `config/wiring.defaults.example` 只有注释，没有猜测的接线。默认串口选择也可以在ESP-IDF控制台配置项中更改；提供已有sdkconfig时应核对其实际console配置。

构建脚本本身不执行烧录。本板已在用户明确授权后完成独立镜像烧录；后续设备写入需有对应授权。GUI不重编译或烧录固件，不能把编译PASS当成硬件数据。

启动后默认显示 `UNCONFIGURED`，`status` 会说明缺少的配置。把GPIO填好但未确认时钟/接线仍不会访问FDC。全部门禁通过后才配置GPIO/I²C、检查 `0x7E=0x5449`、`0x7F=0x3055`，逐项写配置并读回比较，然后读取四通道。DEVICE_ID与FDC2212共用，读ID不能单独证明四通道封装。

## 控制台命令

| 命令 | 行为 |
|---|---|
| `help` | 查看命令及补全提示 |
| `status` | 门禁、当前状态、故障寄存器/读写值、I²C返回码、帧新鲜度、累计帧和输出丢弃计数 |
| `stats` | 打印CH0..CH3累计有效数、错误数、最小/最大/跨度；没有有效样本时三个数值列留空 |
| `clear` | 请求采集任务清零统计并更新epoch；观察新epoch事件后再开始记录新的远/近区间 |
| `stream off` / `stream on` | 关闭/开启周期CSV和自动范围输出；采集与统计继续运行 |
| `restart` | 请求唯一采集任务重新检查门禁、初始化；成功后清零统计并进入新epoch |

故障后进入FAULT并停止采集，不静默重试或继续使用旧值。确认原因后手工 `restart`。UNCONFIGURED时 `restart` 仍只检查配置，不绕过门禁；编译期配置不能靠串口动态改成任意引脚。

## 输出格式与范围统计

完整帧CSV每行一组4路最新值，四通道顺序扫描，**不是同时采样，也不保证来自同一扫描轮**。没有FIFO。主机按CH0 MSB→LSB、CH1 MSB→LSB、CH2 MSB→LSB、CH3 MSB→LSB，独立寻址读取8个寄存器；任何一次读失败或帧超时都不发布半帧。

```text
sample,epoch,sequence,time_us,valid_mask,status,status_errors,error_channels,ratio_errors,output_skipped,ch0,flags0,ch1,flags1,ch2,flags2,ch3,flags3
range,epoch,channel,valid_count,error_count,has_range,min,max,span
```

- `sample` 中RAW按 `{MSB[11:0],LSB[15:0]}` 拼接；`flags` bit1为watchdog、bit0为幅度告警。错误样本的RAW仍可观察，但其valid bit为0，不进入范围。
- DATA自身的错误只排除该通道。STATUS的WD/AH/AL读清诊断会跨轮询累积；因为不能把跨轮的错误可靠对应到正在读取的四路最新值，发生该类错误时保守排除整帧统计。保留错误并集、出现过的错误通道位图以及最后错误通道信息。
- FIN固定按单端配置÷2。TI要求 `fIN < fREF/4`；RAW达到 `0x04000000` 的通道标记 `ratio_errors` 并排除，防止将工作范围之外的读数当作有效端点。是否满足LC传感器频率/幅度要求仍需硬件验证。
- `has_range=0` 的min/max/span为空，表示尚无有效样本，**不表示测得0**。统计清零后保留新epoch标记，旧帧缓存同时清除，首次sample必须来自清零后的完整读取。
- 采集统计每个完整帧；CSV默认每100ms最多输出一帧，自动范围每秒一次，故输出不是全帧无损记录。`output_skipped` 是跳过输出的完整帧数，不是I²C丢帧；可用两条记录的sequence差除以time_us差估计实际完整帧率。
- 单独输出任务处理串口；采集任务从不printf。USB应用记录采用整行缓冲，stdio锁繁忙时立即丢弃该记录；USB驱动互斥锁、发送环形缓冲区的等待分别传入20ms tick超时，并非整个函数严格20ms截止。部分写/锁竞争计入 `output_dropped_lines`，不会因主机停止读取而无限等待。UART0没有硬件流控，按115200波特发送固定长度记录，输出仍与采集分离。不承诺主机一定收到每行；观察计数和解析后的记录数。控制台交互会带提示符，记录CSV时不要同时输入命令；需要交互时先 `stream off`。

可保存终端日志，再提取完整记录（仅处理文件，不接触设备）：

```bash
python3 -B tools/extract_csv.py raw.log --samples samples.csv --ranges ranges.csv
```

提取器忽略提示符、ANSI和不完整/交织记录。`ranges.csv` 保存设备对全部采集帧的统计；不要将节流后的samples.csv重新统计结果误当作全部帧的全局范围。

## 现场测量顺序

每个活动通道都需要有效LC谐振电路。某路未接/失振可能持续触发错误；在当前保守STATUS策略下可能使全部范围有效计数为0，要先看错误字段，不要误判为“所有通道RAW=0”。用户要求本工程读取全部四路，当前不自动屏蔽未用通道。

先固定天线、走线和供电连接，在无手靠近时记录几秒，检查抖动和漂移。随后对每个通道分别做远、中、近位置测试，其他手和天线保持固定，观察单调性及串扰。每个位置可用 `stream off`、`clear`、确认epoch、`stream on` 开始独立区间。取稳定区间的分布与重复性，不用偶发极值作为演奏边界。本阶段不计算电容、距离、音高或音量。

## 文件与验证边界

`components/fdc2214/` 是无ESP-IDF依赖的寄存器状态/范围核心，`main/` 是配置门禁、I²C适配和单所有者任务，`tests/host/` 是故障注入主机测试，`tools/` 是构建/测试/CSV提取脚本。来源与寄存器表见 [docs/sources.md](docs/sources.md)，实现和验证状态见独立 [.ai/progress.md](../../docs/status/fdc_debug/progress.md)。

主机模拟不能验证真实I²C电气、LC起振、USB主机行为或实测数值范围；默认未接线镜像不能产生传感器数据。独立实板镜像已烧录并取得四路原始数据，但幅度告警尚未解决。主工程未改动；GUI不写设备配置或Flash。
