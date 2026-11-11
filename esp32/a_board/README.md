2026-11-04：仅修复减法TOML限深P1，正确处理多行3/4/5结束引号和最终闭合状态，保留6层与16KiB worker。版本`ae350-dds-depth-20261104`，485回归与官方IDF6.1构建通过，未烧录；P2跨音源按键门控仅按用户要求记录。[修复与验证](docs/dds/TOML限深修复-20261104.md)。

# Tang Mega 138K 合成器控制器：YD-ESP32-S3

2026-11-04：已修复减法音色加载/ADSR的旧全局FM门控、客户端路由及加载会话问题。版本`ae350-dds-adsr-20261104`，官方IDF6.1构建、当前AE350与网页回归通过；应用已于2026-11-04按用户授权烧录，完整读回、启动身份及设备网页匹配通过；使用已测冻结构建，不含构建后FDC改动。导入仅保存，加载才采用，等待可用“全来源停止”中止。[修复与验收记录](docs/dds/导入与ADSR修复-20261104.md)列明产物及旧host基线失败。

2026-11-01：手机App私有BLE/Wi-Fi服务端适配已迁入当前AE350主工程配套固件，版本`ae350-app-control-20261101`；保留AE350 ABI2、SUB1、FDC、KS及网页。完整host、AE350专项、实际App核心376断言和S3 Wi-Fi开/关构建通过。开启版应用SHA256 `fa11461610fd21e9916e33f888763cf87930fb6b0543a70a9ab06030e84d2805`，未烧录。**App 0.6.0仍需识别AE350每客户端MIDI通道，当前切换可发生等待超时；不能视为端到端完成。** 详见[接入与客户端同步要求](docs/手机App适配接入AE350-20261101.md)及[恢复索引](snapshots/手机App适配接入AE350-20261101-index.md)。以下保留各阶段历史记录。

2026-10-31 新增 [MIDI 调试入口与电脑端回放脚本](docs/midi-debug.md)：`GET/POST /api/debug/midi`，复用现有 Web RAW 会话和唯一 SPI owner。支持直接注入 MIDI 和保存回放记录；USB PCM 采集尚未接入。


2026-10-24输入分类更新：生产工程用FPGA原生传感器source3替换DIN，内部按组选择FDC2214或双VL53L0X；USB0/Web1/BLE2保持现有入口。S3按MID1能力位NATIVE_SENSOR3避免向该来源发送RAW/清理命令，旧 `midi_in.c` 不再加入生产CMake源列表。传感器选择与标定目前通过FPGA SPI寄存器进行，未新增网页按钮，未部署到硬件；后续声部发声适配尚未接入。协议见[输入契约](contract/midi/README.md)。下方历史记录适用于各自日期的版本。


## 主工程整合状态（2026-10-24）

2026-10-24 已复核并确认参数独立更新版位于主工程 `/home/ice/Project/my_project/development/ESP32/ESP32_development/esp32s3` 的 `main` 工作区。574项候选输入中，整合前只有README与BUG记录发生文档更新，功能源码已与`e20290b6…`一致，沿用主工程现有内容。标准与需求两项审查均未发现阻止整合的问题；完整host（含12组参数独立场景和26项Node测试）、FM_ONLY及ESP-IDF v6.1的S3 Wi-Fi开启构建通过。构建应用SHA256仍为`e20290b6…`，1,331,904字节，factory余204,096字节。

**加法合成器：未完成、未接入。** `main/additive_link.*`、`main/additive_patch.*`、契约和历史测试继续保留供后续开发；源码接口及CMake已加醒目标记。当前只启用减法、KS、FM，加法不进入生产编译、SPI服务、HTTP能力／路由或网页演奏路径；实际编译清单无加法编译单元，ELF无加法符号。

本轮仅补充保留代码状态和文档，没有改动运行逻辑、暂存／提交或更新`.ai/`，没有操作设备。最近一次实板读回是下方2026-10-21的历史记录，包含RAW／FM链路异常；实际演奏、参数效果及当前FPGA配套仍需实板验收。详见[本轮逻辑复核与整合记录](docs/参数控制修复-20261021.md#主工程复核与整合2026-10-24)。

## 最近一次设备验证记录（2026-10-21）

2026-10-21 约17:36，用户更正此前问题出在FPGA端后，已按“你更新到新版本试一下”授权重新烧入参数独立更新版 **`e20290b6…`**（ELF `d9ab8e3c3`）。新应用及保留启动／分区区域独立校验通过，热点、BLE和启动SPI自检通过，20秒未见panic／栈溢出。**本次两次状态GET均200，但RAW输入未就绪（fault=1）；FM先报告capability_missing，后报告spi_readback_failed，第二次引擎状态处于切换／恢复中，尚不能验收FM演奏。** 不再将先前无声归因为已确认的ESP32故障。旧程序已备份，串口已释放；本轮未修改功能源码、FPGA或`.ai/`，未暂存／提交。见[重新烧录与复测记录](</home/ice/文档/ChatGPT/ESP32开发 2/artifacts/reflash-independent-parameters-20261021-173529/烧录与复测结果.md>)。

## 参数独立更新说明

已在主工程工作区完成减法、KS、FM参数模板的独立发送：只上传实际变化的引擎数据，减法／KS分别提交、分别核对各自ACK，普通FM音量／声像只发送`b400`。包络、LFO等只改变静态控制而没有改变模板内容时，不再附带上传其他模板。共用音量、声像保持原有共用含义；初始化或故障恢复仍完整重建必要状态。

Wi-Fi开启版应用SHA256 `e20290b6f04798a59d7e4735be18f857e1f2f2db5b86a8d5b1d142af5e25ee23`，ELF前缀`d9ab8e3c3`。完整host、12组新增UBSan、FM_ONLY、local／隔离UBSan及S3 Wi-Fi开／关构建通过；**此前16:31曾烧录并独立校验通过**；当次20秒启动观察未见panic／栈溢出，状态读回演奏输入ready=true、切换／恢复均为false，三个引擎均可用；这些是当时的历史观察，本次状态以上方记录为准。本轮未实测声音或参数操作，详见[烧录结果](</home/ice/文档/ChatGPT/ESP32开发 2/artifacts/flash-independent-parameters-20261021-163125/烧录结果.md>)。加法实现继续保留但不接入。行为、协议边界和验证见[参数控制修复记录](docs/参数控制修复-20261021.md#后续更新参数按引擎独立发送2026-10-21)，完整源码与产物恢复见[快照索引](snapshots/参数独立更新-20261021-index.md)。

## 参数独立更新前的已烧录基线（2026-10-21）

参数独立更新前的主工程 `main` 工作区采用**三引擎参数控制修复版**，保留减法（0）、KS（1）、FM（2）、混响、原始 MIDI 和 STOP 保护。应用 SHA256 `94c569e028ee58865da2d8ccd3e0e4d8fe828c2170a4f02ecff8f2a55a4a0bec`，ELF SHA256 前缀 `5422cbf24`；该版本已于2026-10-21烧录。当时核对主工程571项输入与该候选一致，代码原已位于本主工程，无需从旧副本覆盖或生成额外 Git 合并提交。

本版修复了 KS 空闲状态的重复 SPI 读回挤占参数模板更新时间，以及路由恢复后模板缓存未重新开放演奏许可的问题。加法源文件、契约和历史测试保留，当前不编译加法驱动、不探测或提交加法硬件、不开放加法网页入口。详情、验证范围及恢复入口见[参数控制 BUG 修复记录](docs/参数控制修复-20261021.md)。

用户于2026-10-21反馈：**初步验证未发现严重问题**。这条反馈作为当前阶段试用结果；此前一次启动观察仍出现 FM 恢复警告及输入未就绪的状态，保留原始记录，不据此宣称所有 FM 参数、长期联网及全部异常恢复场景均已完成实板验收。本轮仅核对主工程状态并更新文档，没有再次烧录或修改 FPGA。

## 历史版本记录

2026-10-19历史尝试：曾开发**加法合成器**接入代码；该功能**未完成、未接入当前交付**，以下为当时设计（逻辑引擎3，8声部×32分音、4组包络），沿用 FPGA 统一 USB/Web/BLE MIDI。新增完整音色 HTTP 提交、单 SPI owner COMMIT/ACK、取消补偿和真实就绪后原子发布。网页已接入 Web owner 最新产物，原“DDS”界面显示为“减法合成器”；内部兼容标识保留。当前仅本地测试/构建通过，**未烧录、未实板验声**。资源采用单份 gzip 与原文流式返回，保留1500KiB应用分区；完整说明、恢复索引及限制见[加法接入记录](docs/加法合成器接入-20261019.md)。

当前本地版本为2026-10-16 **FPGA统一MIDI演奏接入**：原生USB由FPGA接收，S3转发Web/BLE原始MIDI并保留参数／网页预置／静态模板。首版一个Web演奏token；DIN／面板UART演奏及键盘Program Change加载FAT预置暂未迁移，不影响新通路启用。Wi-Fi开／关S3构建和host回归已通过，**未烧录新版本**。行为、兼容边界和恢复入口见[原始MIDI交付说明](docs/原始MIDI接入-20261016.md)；下文是对应日期的历史基线。

本地源码已于2026-10-13增加 **DDS TOML预设管理**：Factory/User、Supersaw/Galaxy、启动默认和MIDI Bank/Program映射，通过异步HTTP与原USB控制台操作。保留原SPI事务、门控和恢复，新增2MiB数据分区，8MB镜像与1500KiB应用分区不变。本轮仅本地验证/构建，**未烧录**；设备证据仍属于下面的2026-10-12基线。完整入口、兼容策略、部署边界及验证见 [DDS预设说明](docs/dds/预设管理-20261013.md)。

当前音频反馈（截至2026-10-15）：用户已明确更正为 **仅FM音量偏小，DDS/KS正常**。现场音量问题尚未完成控制来源、配套版本和参数的对应验证，不能据此认定移植失败或某个模块已确认有错。下方实板结果是对应历史版本的记录，不代表当前新增DDS预设管理或FPGA修改已部署。

上一轮实板交付为2026-10-12 **FM握手与网页实时按键修复**：普通演奏发送不再等待逐条owner状态读回，接收／采用／FPGA确认继续在后台显示，在线空闲用2秒空位图保活。修复FM启动operator描述、在途Panic重复与历史完成标记竞争，以及HTTP v2状态响应碎片阻塞。主机C／包内26项Web及隔离网页367项回归、ESP-IDF v6.1构建通过；已按本轮授权烧录，应用读回一致、SPI启动自检通过，Chrome验证空闲后DDS首键和KS控制路径。当前开启版BIN为1,405,568字节，factory余130,432字节。当时FM诊断命令得到完整原生完成结果，但用户反馈未听到FM声音；这是2026-10-12的历史现象，当前反馈已由上方“仅FM音量偏小”取代，现场根因仍待确认。详细当前证据与历史版本见[现有交付记录](docs/fm/控制恢复与网页传输修复-20261012.md)。本包只维护ESP32-S3。

以下整合摘要记录第一阶段来源与验收范围，原manifest及归档继续保留：

> 2026-10-11 整合：本目录从实际工作树 `synth-review-work-20261003` 收纳第04版 S3 源码，接入稳定新版 Web 单 HTML（53,484 字节）。来源迁移基线见 [迁移来源](迁移来源.json)，本轮文件闭包、接口、工具、测试与产物 SHA256 见 [整合验证 manifest](docs/整合验证-20261011.json)。有效配置为 `config/sdkconfig.esp32s3`，源码、回归和构建入口均在本包内；旧 19,394 字节网页保留于 [历史页面](docs/history/web-v04.html)。下面的硬件结果与历史记录属于来源工作树，本次只做 host 回归和固件构建，没有实板验收或 FM 接入。

当前硬件：2026-10-05 用户新 PMOD 转接板与 GPIO38/2/1 低有效状态 LED。旧 GPIO45 MOSI/GPIO48 MISO/J13 七线记录是历史版本，不再作为当前接线依据。本工作树已将四路 SPI 按2026-10-11用户更正适配为 AB20/AA19/AA21/AA20（依次CS/SCLK/MOSI/MISO，Bank4 LVCMOS33），废弃原 FPGA 八路 LED 输出；其余12路转接板 GPIO 在 FPGA 上为无拉被动输入。详细来源、构建与时序证据见 [来源工作树迁移记录](../../../../docs/esp32-led-pmod-migration-20261010.md)。

本 ESP-IDF v6.1 工程的组件源码在 `main/`，从第04版共享源码迁入；未恢复工作树中已删除的 C3 工程目录。YD-ESP32-S3 V1.4 参考资料涵盖 N8R2/N8R8/N16R8，本配置继续按8MB镜像、DIO80MHz、1500KiB factory、无PSRAM/OTA构建，不因物理Flash容量猜测扩张。

| 信号 | ESP32 GPIO／方向 | 新 Dock／PMOD 针号 | 电阻 | ESP32 排针 |
|---|---|---|---|---|
| CS，低有效 | 21 输出 | J8/PMOD1 pin6（AB20） | 待复核 | J2.18 |
| SCLK | 47 输出 | J8/PMOD1 pin5（AA19） | 待复核 | J2.17 |
| MOSI | 13 输出 | J8/PMOD1 pin8（AA21） | 待复核 | J1.19 |
| MISO | 14 输入、输出关闭、无拉 | J8/PMOD1 pin7（AA20） | 待复核 | J1.20 |
| 红 LED1 | 38，低电平点亮 | 用户 LED 图 | R17 1kΩ到3.3V | GPIO38 |
| 绿 LED2 | 2，低电平点亮 | 用户 LED 图 | R18 1kΩ到3.3V | GPIO2 |
| 蓝 LED3 | 1，低电平点亮 | 用户 LED 图 | R19 1kΩ到3.3V | GPIO1 |
| DIN MIDI RX | 16 输入，UART1 | 现有 MIDI 输入电路 | 保留 | GPIO16 |
| 面板 MIDI RX | 42 输入，UART0 | 原面板输入电路从GPIO2迁到GPIO42 | 保留原输入电路 | GPIO42 |
| 滑杆 ADC | 8，ADC1_CH7 | 原滑杆分压输出从GPIO1迁到GPIO8 | 保留原分压电路 | GPIO8 |

2026-10-11用户确认GPIO到FPGA球脚的映射，见 [来源工作树引脚更正记录](../../../../docs/pmod-pin-correction-20261011.md)。原表的P/N顺序已被更正；SPI各串联电阻位号尚未重新核对，LED电阻位号保持原图记录。其余J9八路GPIO对应关系待用户确认，FPGA继续保持无拉输入。

SPI 使用同一 PMOD 上21/47与13/14两组完整走线对，仍是3.3V单端 mode0、10MHz、MSB-first、单线全双工协议。差分走线不等于差分收发电气接口。GPIO19/20保留原生USB，GPIO0保留BOOT；GPIO45/48不再分配SPI，不启用GPIO48 RGB输出。PMOD的1/2是+3.3V、3/4是GND，GPIO编号与排针位置不可混用，通信需共同参考地。

GPIO2/1专用LED；面板UART0 RX迁到GPIO42，保留31250 baud/8N1、独立解析器与原输入极性预检。滑杆迁到GPIO8/ADC1_CH7，恢复500Hz采样、8点滑动平均、滞回、CC71共振控制及两端NVS校准；USB console的1/2/r/p键均保留。GPIO42/8不占用用户表格中的PMOD线路。GPIO42的外部四线JTAG/MTMS复用不同时使用；原生USB/JTAG19/20保留。DIN/BLE/HTTP功能保留；USB MIDI没有实现。

LED由低优先级任务每50ms观察RAM状态，输出0点亮、1熄灭。蓝灯启动时每2秒短闪两次，运行后每秒短闪一次（两个owner必须持续进展）；绿灯每2秒短闪一次表示无线服务可连接，常亮表示BLE设备连接或Wi-Fi站点关联。红灯常亮表示启动SPI自检失败，4Hz快闪表示owner心跳停滞/初始化超时，2Hz闪表示当前引擎恢复，每2秒双闪表示无线服务错误。KS粘滞历史fault不阻止当前恢复后灭红灯，Wi-Fi编译关闭不算错误。LED任务不操作SPI、不编辑patch，不能证明DAC已经发声。

接线时将原GPIO2上的面板MIDI输入电路输出改接GPIO42，将原GPIO1上的滑杆分压输出改接GPIO8，原LED接法不变。GPIO42接3.3V逻辑的MIDI输入电路输出，不直接连接MIDI DIN电流环。滑杆保留原分压输入范围，迁移后用USB键1/2重新确认两端；r查看原始值，p查看BLE。

先按 ESP-IDF 安装说明设置 `IDF_PATH`，在本 S3 工程目录激活 ESP-IDF v6.1 后构建。编译输出目录使用 ASCII 路径，避免 GCC specs 对中文输出路径的解析问题：

```bash
source "$IDF_PATH/export.sh"
idf.py --version
ECT_BUILD_DIR="/tmp/ect-esp32s3-build"
idf.py -D SDKCONFIG="$PWD/config/sdkconfig.esp32s3" -B "$ECT_BUILD_DIR" build size
```

生效配置为本 S3 工程目录内的 `config/sdkconfig.esp32s3`，CMake 与根 `make fw` 使用同一配置。旧 S3 目录的 `sdkconfig` 仅作历史追溯。需要独立配置或构建目录时使用 ESP-IDF 的 `SDKCONFIG` 和 build-dir 选项。固定配置保持8MB DIO/80MHz、1500KiB factory、USB Serial/JTAG、Wi-Fi/BLE，无PSRAM/OTA；构建不操作硬件。

包内回归入口为 `NODE_BINARY=node bash tests/run.sh`，需要 GCC、Python 3 和 Node.js >=22；范围与边界见 [测试说明](tests/README.md)。本轮构建确认所有组件来自本包 `main/` 和 ESP-IDF v6.1，没有外部 managed 组件、`idf_component.yml` 或 `dependencies.lock`，无需修改 ignore 规则。网页自身不需要 Node 服务：测试副本仅在电脑上运行。

新转接板不连接 GPIO19/20/0 到 FPGA；旧七线/J13 跳线不属于当前方案。FPGA 实现脚本保持 `-use_cpu_as_gpio 1` 与 `-use_sspi_as_gpio 1`，释放转接板所用配置复用引脚，并保留 JTAG。不要同时接入共享这些线路的摄像头/SDRAM1扩展件。两板独立USB供电、共地，PMOD的3.3V电源针不并接。

硬件使用顺序仍是先配置 FPGA，再启动/复位 ESP32，让启动自检和参数初始化针对已配置的 FPGA 进行。重新配置时先停止 ESP32 SPI 驱动；当前转接板没有自动隔离器。最终配置的输入/OE/偏置由 PnR 报告核对；未配置、单板断电、热插拔和链路波形未实测，不能由 CST 推断通过。获准设备操作后，先确认 `RESULT: ALL OK (0 fails)`，再验收灯光、面板/滑杆、手机及音频。

共享源码来自Ethereal Cloud Tone `open_resource` commit `4940a08`，后续合成器控制逻辑与原许可保留。

## 手机本地 Wi-Fi 控制

S3 默认启用本地热点，SSID 为 **FPGA_Project**，演示 WPA2 密码为
**Fpga-Synth-2026**。手机连接热点后，在浏览器打开
[http://192.168.4.1](http://192.168.4.1)；如手机提示无互联网，选择保持连接。
页面资源全部由 ESP32 提供，无 CDN、外部 Wi-Fi 凭据或互联网需求。
BLE MIDI 同时保留，完整设备名为 **FPGA_Bluetooth**；标准 MIDI service UUID
不变。名称放在 BLE scan response，主广播保留 MIDI UUID。

页面提供全部 38 个 HTTP 白名单参数：双振荡器波形、声部结构、混合、音量、声像、滤波、两组 ADSR、力度响应、失谐及
立体声宽度、两组 LFO 和 C3–C5 的 25 键多指键盘。冻结接口见 [HTTP 约定](docs/冻结HTTP接口.md)。第一次加载只读取真实 RAM 音色，
不会把滑块的近似 CC 数值写回默认音色。“已接收”表示 HTTP 命令已进入事件
总线；“ESP32 已处理”表示声部任务已经采用该请求。状态及原始值属于 ESP32
控制侧。配套 KS01 位流提供 DDS / 四弦 KS 模式切换及 FPGA SPI 状态读回，
包括 requested / applied 模式、接受序号、active / init / release 掩码与每弦
started 序号；初始化完成不代表 DAC 或实物发声已经确认。旧位流禁用 KS，
DDS 仍可使用。DDS的Flash音色库与Bank/Program Change已在本地新固件中提供（入口见DDS预设说明，尚未部署）；USB MIDI Host未提供；第二阶段FM的Program Change使用匹配profile的生成preset ID，详见运行期交付。

默认 DDS（32 声部）保持原音色与 bend。KS 使用独立四弦声部池，所有 MIDI
来源与页面 token 的同音高音符独立归属；CC64 延音、CC7 音量、CC10 声像
及 200–8000 ms 的 T60 衰减可调。KS 只在 NoteOn 调音，不做实时 bend。
切换模式会清除全部来源按键、延音及网页 held bitmap，并等待 FPGA applied
模式读回与 DDS 硬静音 bank 提交；完成后必须重新按键。KS 页面隐藏 DDS
参数但保留 `g_patch`，刷新页面只读取参数。

KS COMMIT 的接受与 MODE 应用确认有 50 ms 上限，初始化完成不使用这个
超时。SPI 读回失败、接受/模式序号或帧计数倒退会取消旧 On，发布
`EVT_PANIC` 清除全部来源及网页按键，并重建 DDS 静音双 bank；只有 owner
清键与 FPGA 安全 DDS 模式均确认后才恢复弹奏，旧 held 音符不会自动重放。
页面显示 `recovering`、`fault`（1=确认超时、2=读回失败、3=FPGA 复位）、
`recoveries` 和 `timeouts`，最后故障原因保留用于诊断。恢复仍依赖 FPGA
时钟及 SPI 返回有效；硬件持续故障时保持切换/恢复状态，不能声称已静音。

按键使用 250 ms 心跳和 1.2 秒租约；失焦、隐藏和卸载尝试发送松键，断网后
由 ESP32 自行释放该页面持有的声部。Release 尾音仍可能延续；点击“停止所有
声音”会关闭 bus 1023 的诊断测试音并立即 panic 所有来源的声部。最多同时
使用四个页面，各页面的按键 owner 独立，音色参数共享。

在 `idf.py menuconfig` 的 `FPGA local web control` 中可修改 SSID、WPA2 密码和
信道，或关闭 `CONFIG_ECT_WIFI_WEB`。演示密码适合首次本地试用；可换成
自己的 8–63 字符密码。留空会首次生成随机密码并保存 NVS，重启保持不变，
可在 USB 日志读取。固定密码会编译进固件，勿填写其他网络的私人凭据。
本工作树仅维护 S3；Wi-Fi关闭验证使用独立配置，不改变默认配置。

Wi-Fi + NimBLE + 页面已超过原 1 MiB app 分区，因此 S3 默认改用 ESP-IDF 官方
原先使用`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`（factory 1500 KiB）；DDS版使用`config/partitions.dds.csv`保持该应用分区并增加数据区。部署时需要配套
新的分区表和应用，不能只用旧 1 MiB 分区刷应用。这里只构建，不进行烧录。
硬件验收仍需确认 `RESULT: ALL OK`，并检查手机多触点、BLE/Wi-Fi 同时使用、
拔网、紧急停止及实际音频延迟。
