# 当前状态

2026-11-03 16:34 JST 已修复ESP32断电后网页监控不能恢复：旧采集PID203397因SerialException退出而HTTP仍存活，现已替换为版本化入口host/b_board_debugger/web_monitor.py及web_monitor.html。网页http://127.0.0.1:8765/已连接实板并保留标签2，服务及唯一UART owner PID333294（线程采集），FT232R A5069RR4按by-id每秒重试；无4小时退出限制，手动停止会释放并停止重试，可页面恢复。MCU uptime回退会重新握手并重建事件会话；35项上位机测试通过，新增5项含真实PTY断开/重插、无UART断连的MCU重启、静默恢复、时间回绕及HTTP停止/恢复。实板已验证页面停止后无串口owner、恢复后唯一owner/配置回读正确，未人为物理拔插、未做新光学/MIDI音频验收。本次证据.ai/evidence/web-monitor-reconnect-20261103-162629，运行记录同目录runtime/；旧证据及原固件/Flash备份保留。本轮仅改主机监控/README及.ai四份记录，无固件/NVS写入，未暂存提交推送；电脑重启后需重新启动本机服务。

2026-11-03 14:53 JST 最新串口调试/短遮光更响固件已实物烧录并验收：ESP32-S3 84:c7:bb:79:d2:14，app523104字节/SHA256 e2bb487f90c352001f87e16a42016c0c83268befbad4e05ef52c72fa5ef26d71，32项生产输入及有效sdkconfig与镜像一致。FT232R A5069RR4（ttyUSB2）、460800/ROM no-stub，覆盖区0x0～0x90000备份与设备MD5一致；Bootloader/分区/app写入及独立verify通过，NVS/PHY逐字节不变。回读curve=[20,500,127,16]、config.channel=2/报文编号1，harp channel及harp get查询实际成功；无需apply/save，重开监控后的再次回读仍正确。实板running、USB MIDI在线、EC11 enabled=15，未发送软件strike或做新的受控光路/音频实测。网页http://127.0.0.1:8765/已恢复并保留标签1，HTTP PID203395、唯一UART采集PID203397，最长4小时；本次会话.ai/evidence/web-monitor-after-serial-midi-20261103-144756，后续不要另开串口。验收.ai/evidence/esp32-serial-midi-flash-20261103-144756/acceptance.json。本轮未改固件源码、A/FPGA/CAD，未提交推送；下方新曲线/串口入口尚未烧录记录属历史。


2026-11-03 已保留并增强UART调试通道：新增harp channel查询、harp channel <0..15>选择MIDI报文编号、harp get读取完整配置；选择经原有旧通道清音屏障和ACK生效，显式harp save才跨重启保存。带config的回复新增顶层midi_channel_id，config.channel/RPC/NVS仍为1～16；默认报文编号1、公开号2及短遮光更响曲线[20,500,127,16]保留。桌面“力度曲线/演奏参数”选择框改为报文编号0～15，同时显示已应用/草稿及常用号。同步上位机GPIO白名单至固件现用表，修复实板四组EC11配置被拒导致握手/通道编辑不可用的问题。ASAN/UBSAN完整回归、全16个编号/清音屏障/NVS往返/USB-BLE编码、15次跨语言事务及30项桌面测试均通过；ESP-IDF v6.1构建exit0。固件在firmware/b_board/artifacts/20261103-serial-midi-channel/，证据.ai/evidence/serial-midi-channel-20261103/。本轮未打开实物串口、未烧录或apply/save，现有监控owner保留；未改A/FPGA/CAD，未提交推送。


2026-11-03 已按用户要求修正遮光时长→力度默认曲线：短遮光更响、长遮光更轻，velocity=[20,500,127,16]（≤20ms→127、100ms→109、260ms→72、≥500ms→16，gain100%）。保留恢复光照后触发、200ms关音、2ms去抖、CC7总音量及报文通道编号1/config.channel=2。固件fallback/Kconfig/sdkconfig.defaults/本地sdkconfig与上位机默认一致，启动日志打印实际端点。ESP-IDF v6.1构建exit0/app522896字节；12路六组时长输入到USB/BLE Note On/Off、主机回归和桌面28项测试通过。新曲线固件已生成，本轮未烧录、未打开串口或apply/save，当前实物最后一次已记录烧录仍是14:11的MIDI编号1镜像，现有监控owner保留。有效NVS旧曲线仍优先，后续上板需回读确认velocity=[20,500,127,16]。产物firmware/b_board/artifacts/20261103-short-shade-louder/，证据.ai/evidence/shade-velocity-inverse-20261103/；未改A/FPGA/CAD，未提交推送。


2026-11-03 14:11 JST 新的MIDI编号1固件已实物烧录并验收：B板ESP32-S3 84:c7:bb:79:d2:14，45项构建输入/有效sdkconfig与新镜像一致。FT232R by-id（当前ttyUSB2）、460800/ROM no-stub，覆盖区0x0～0x90000备份及设备MD5一致；Bootloader/分区/app写入和独立校验exit0，NVS/PHY逐字节未变。新启动日志“MIDI engine channel ID=1 (channel 2)”，get回读channel=2，后续监控重新打开导致的再次启动后也保持2，无需apply/save或覆盖旧参数。实板running、USB在线、EC11 enabled=15/四组引脚正确。监控 http://127.0.0.1:8765/ 已恢复并保留浏览器标签2，HTTP PID133901、唯一采集PID133902，最长4小时；当前会话 .ai/evidence/web-monitor-after-esp32-midi-channel-flash-20261103-140827，不要另开串口。app522768字节/SHA256 6dd307c9b9dad6ec965d1a2fcfb1fa0c6fc90cf35789dc7bdda35d4ac894e5b9；验收 .ai/evidence/esp32-midi-channel-flash-20261103-140827/acceptance.json。未修改源码、A/FPGA/CAD，未提交推送；FPGA此前绿蓝红循环未据本轮USB在线快照宣称修复。


2026-11-03 13:44 JST 已按用户要求重开只读监控网页 http://127.0.0.1:8765/，实际页面“实板·已连接”、USB MIDI在线、四组EC11使能。旧采集已停止且旧HTTP进程身份核对后关闭，原始记录保留；FT232R A5069RR4重新枚举为/dev/ttyUSB2，按by-id连接。新HTTP PID103755、唯一UART采集PID103756，最长4小时或页面停止；后续读取本次live.json，避免另开串口。会话 .ai/evidence/web-monitor-reopen-20261103-134438/，浏览器标签2已保留。设备回读config.channel=1（报文编号0），新编号1/config.channel=2镜像仍尚未烧录；本次仅get/status/ping/stream，无apply/save/strike/烧录，原生USB可继续连接FPGA。

2026-11-03 已按用户最终澄清完成MIDI引擎通道默认值更新：目标是报文通道编号1（0～15），即常用通道2（1～16）。Kconfig/sdkconfig.defaults/本地sdkconfig、b_config fallback及上位机默认均设为channel=2；保留UART/NVS/GUI的1～16语义，启动日志同时显示报文编号和常用号。12弦的音符、CC7/74/71、Pitch Bend、Panic在USB/BLE均使用报文编号1；默认音符与遮光结束后发声规则未改。ESP-IDF v6.1构建成功，app522768字节；ASAN/UBSAN核心/9次PC-C事务/54包A解帧兼容与上位机27项测试通过，定向报文探针覆盖默认编号1及可配置0/15。新固件已生成，尚未烧录或写NVS，未改A/FPGA/CAD、未提交推送。产物 firmware/b_board/artifacts/20261103-midi-wire-channel1/，证据 .ai/evidence/midi-engine-channel-20261103/；若设备NVS仍保存channel=1，其报文编号仍为0，后续上板需显式核对并应用channel=2。


2026-11-03 用户更正接收电路后，重烧与网页调试已完成：32项生产输入及sdkconfig与2026-10-30已构建产物一致，复用ESP-IDF v6.1镜像；FT232R 460800 ROM no-stub烧录和三镜像独立校验通过，NVS/PHY逐字节保留。覆盖区0x0～0x90000备份与ROM MD5一致，旧16MB备份全部保留。实板running、USB MIDI在线、四组EC11绑定回读正确/enabled=15，当前raw=0x102、HIGH通道=2,9；本次未完成受控遮挡/恢复验收，不能仅凭电平认定前级完全正常。网页 http://127.0.0.1:8765/ 已实际打开并验证“实板·已连接”，HTTP PID51682、唯一UART采集PID51683，最长4小时或页面停止。当前记录 `.ai/evidence/web-monitor-20261103/`，不要另开串口；验收 `.ai/evidence/esp32-reflash-monitor-20261103/acceptance.json`。本次无固件/配置/上拉/极性修改，未改A/FPGA/CAD，未提交推送。

2026-10-31 重烧并打开监控已完成：沿用指纹一致的10月30日ESP-IDF v6.1镜像（32项生产输入+sdkconfig未变），FT232R 460800、ROM no-stub写入/独立校验均通过，NVS/PHY逐字节保留。新做0x0～0x90000覆盖区备份并验证ROM MD5；原16MB备份保留。实物running、USB MIDI在线、四组EC11回读正确/enabled=15，当前raw=0xFFF（本次已观察12路LOW），不再沿用昨天恒高作为当前结论；完整遮挡/恢复周期仍由用户实验。只读网页 http://127.0.0.1:8765/ 已在右侧打开，HTTP PID752212、采集PID752213，最长4小时或页面停止；不要另开串口。证据 `.ai/evidence/esp32-reflash-monitor-20261031/acceptance.json`，实时记录 `.ai/evidence/web-monitor-20261031/`。未改固件/上拉/极性或A/FPGA/CAD，未提交推送。 本次监控已记录0x000→0xFFF的12路电平变化，尚无恢复沿形成的完整击弦事件；不能继续认定输入始终恒高。

2026-10-30 14:35 JST 已按用户要求打开本机只读监控网页 http://127.0.0.1:8765/，浏览器标签保留。HTTP服务PID356692、真实FT232R单owner采集PID356694（最长4小时，可页面点击停止释放串口）；后续不要另开串口。真实握手get/status/ping通过，running、enabled=15、当前raw0xFFF；网页直接按raw及polarity显示受光/遮光电平，避免既有shade=0误标。包含12路电平、近期轨迹、hits/时长/力度/Note及CSV导出。只发送读取和stream，未apply/save/strike/烧录，固件指纹未变。运行日志/状态/验证在 `.ai/evidence/web-monitor-20261030/`。

2026-10-30 14:23 JST 自然光复查：用户说明现在已恢复自然光照射，FT232只读采集30.03秒/146帧，raw仍全程0xFFF、无输入变化/真实事件；running、EC11 enabled=15、USB在线。自然光未让数字输入进入受光LOW，尚不能确认实物感光触发；优先核对门限和OUT输出。采集结束、串口释放，无固件/参数/Flash写入。证据 `.ai/evidence/ambient-recheck-20261030-142346/result.json`。

2026-10-30 新接线固件已烧录并独立校验：ESP32-S3 84:c7:bb:79:d2:14，16MB整片备份与ROM MD5一致，NVS/PHY烧录前后逐字节不变；ESP-IDF v6.1构建exit0/app522624字节。光敏OUT1～12为4/5/46/6/18/7/9/15/10/16/11/17，四组EC11回读为13/14/12、40/39/41、47/21/38、2/42/1，encoder_enabled=15；running，USB MIDI枚举/初始化消息与UART ping/get/status通过。用户说明激光发射模块未搭建，当前自然光下完成遮挡/放开；176.61秒/879帧raw始终0xFFF，电平变化/真实击弦/真实Note On均0，missed/drops0。软件12路LOW→HIGH→LOW及力度/关音回归通过，但实物自然光遮光触发尚未确认；下一步检查比较器门限/OUT电压/连接，不改极性强行触发。采集已停止并释放FT232串口，板上程序继续运行。证据 `.ai/evidence/esp32-gpio-flash-20261030/acceptance.json`，镜像/备份 `firmware/b_board/artifacts/20261030-gpio-flash/`。未修改A/FPGA/CAD、未提交推送；下文未同步/EC11暂停记录属历史。

2026-10-30 GPIO交接更正（源证据revision=4）已归档：撤回上一版“光敏与编码器六项实际硬件GPIO重叠”的结论，原比较混用了光敏旧IO标签与编码器排针参考图换算。两组都按同一J1/J2图及H1/J1、H2/J2对应时，光敏1～12为4/5/46/6/18/7/9/15/10/16/11/17；编码器A/B/SW仍为13/14/12、40/39/41、47/21/38、2/42/1，用户已逐项确认全部十二条编码器GPIO，ENC_A4=GPIO2、GPIO12仅用于ENC_SW1，不再列为未知。共24个唯一GPIO、不占19/20。十项旧标签/当前固件与参考图差异仍存在，但不是实际硬件重叠。当前固件旧光敏数组、校验及编码器-1均未改；本轮仅修正文档/交接记录，不恢复用户接手的EC11绑定或设备写入。五份更正后版本资料在 `docs/ec11-gpio-handoff-20261030/`，24条一致性/指纹/45份固件未变验证及用户GPIO列举顺序与OUT通道的区别在 `.ai/evidence/ec11-gpio-handoff-20261030-revision4/validation.json`；第1版已标记撤回相关硬件推断，旧版和前镜像保留。GUI运行信息及下文采集均为当时观测。

2026-10-29 20:36 JST 上位机已打开：按用户要求在桌面显示真实中文“B 板调试台 · 激光竖琴”，PID239337，FT232R A5069RR4 /dev/ttyUSB0、115200、5 Hz；window_viewable/connected/ready均true，demo=false，r0、running、raw=0xFFF、hits全0。窗口保持运行，用户可直接看“原始 OUT”；启动未武装时“光照”标签的既知误显示仍未修复。仅通过现有应用API启动并读取配置/状态/开启遥测，未apply/save或烧录，EC11仍由用户处理。launcher、运行日志、实时window-status.json见 `.ai/evidence/debugger-window-20261029/`。此前18:17监听已于18:27:56自动结束，20:30才收到的操作完成回复不能与旧采集窗口关联；20:31短采仍全高，证据 `.ai/evidence/laser-after-uncover-20261029-203110/result.json`。下文历史采集占用/暂停等待测量状态已由当前实时GUI观察替代。

2026-10-29 18:17 JST 交互监听：用户进一步说明当前保持遮挡，高电平与此状态一致；尚需恢复照射及完整LOW→HIGH→LOW验证，不能只凭遮挡恒高判故障。为避免反复开串口复位，启动同一FT232连接的有界连续采集，PID233860，最长600秒，18:27:56 JST自动结束；端口当前由采集占用。实时状态 `.ai/evidence/laser-live-20261029/live.json`，进程身份/期限见同目录process.json。用户完成放开/再遮挡/再放开后直接读此记录，不要重复打开串口；验证完成或用户要求停止时关闭此owned进程。无配置写入、无烧录，EC11仍由用户处理。

2026-10-29 用户确认正在遮挡后重采：120.02秒/596帧，12路raw仍全部0xFFF，电平变化0、真实击弦0、hits全0；running，max_gap1373us、missed/drops0。当前问题尚未到击弦事件产生阶段，需要区分接收板1OUT是否翻转与输出到MCU的连接；请用户测第1路1OUT对接收板GND在受光/完全遮挡两种状态的直流电压，取得读数后继续定位。当前任务暂停等待此项实物测量，不更改GPIO/极性或烧录。证据 `.ai/evidence/laser-physical-20261029/result.json`；EC11仍由用户处理。

2026-10-29 光敏输入/弹奏检查：用户明确EC11由自己处理，本任务停止编码器换算与绑定。已通过FT232 UART只读采集90.03秒/446帧，12路raw始终0xFFF，无真实击弦/电平变化；是否在采集时接好并遮挡尚待确认，不能据此判光敏板损坏。当前polarity1、2ms去抖、恢复后发声、20..500ms→力度16..127、gate200ms、AUTO优先USB。核心回归与定向探针通过，发现启动未武装时shade=0被GUI误显示为“光照”的状态问题，已复现并记录，未修改生产代码或设备配置。报告 `docs/b-board-play-logic-review-20261029.md`，证据 `.ai/evidence/laser-play-review-20261029/`；下文EC11等待连接说明属历史，现交由用户处理。

2026-10-29 EC11资料补充：已按用户指引读取桌面 `EC11编码器引脚与电路连接说明.md`，四组A/B/SW到H1/H2的12条连接与既有原理图记录一致。文件只列排针号，未给出实际ESP32 GPIO，也未明确H1/H2与开发板的物理连接关系；此项需要用户说明后继续绑定。当前暂停EC11绑定任务，固件和设备配置保持现有状态（编码器-1禁用）。文件快照/指纹/检查结果在 `.ai/evidence/ec11-pin-document-20261029/`，原桌面文件未修改。

2026-10-29 实物首刷已完成：用户授权并手动进入下载模式后，对ESP32-S3 revision v0.2、MAC `84:c7:bb:79:d2:14` 完整备份16 MB Flash，ROM MD5与文件一致；随后写入并独立校验Bootloader/分区表/B应用三镜像。ESP-IDF v6.1实际构建成功，应用522624字节，16 MB DIO单factory、PSRAM关闭。重启枚举为 `Laser Harp B` / `303a:4008`，ALSA `hw:2,0,0`。用户确认FT232R A5069RR4接ESP32 UART，临时加载被现有配置禁止自动加载的ftdi_sio后，`/dev/ttyUSB0` 的115200 ping/get/status均通过；软件试奏100ms实际收到MIDI60/velocity35的Note On和自动Note Off。状态running，观测采样约1ms、missed/drops均0。12路raw全高，物理光路尚未测试；EC11 GPIO仍全-1禁用，BLE端到端及PSRAM读写未验证。备份和镜像在 `firmware/b_board/artifacts/20261029-first-flash/`（Git忽略），验收证据 `.ai/evidence/esp32-first-flash-20261029/acceptance.json`。本轮变更未提交/推送，下文首次连接及未烧录内容属历史。

2026-10-29：用户授权将当前 `codex/laser-harp-b-board` 分支提交并上传至私有仓库 `ICECREAM1230/Lets-go`。当前目录原先无 HEAD、无远程，按现有独立项目的根目录结构建立首个提交；不导入其他工作目录的未提交内容。本轮 C/协议/9次跨语言事务/54包A兼容回归与桌面27项测试均通过；上次 ESP-IDF 构建的39项输入指纹一致，本轮未重新交叉编译或写设备。发布验证记录位于 `.ai/evidence/github-upload-20261029/`；实际上传结果以远程分支与本地 HEAD 校验为准。下文“无 HEAD／未提交”属于历史状态。

2026-10-28：按用户最新要求移除20K的OLED规划；RGB LCD也未纳入。当前17传感器+5桥接+1功放关断=23个GPIO，剩余11个。释放71／J5.18与55／J5.11，后者仍有功放输入负载。已更新当前管脚/拓扑/布局资料，未改CAD/RTL、未构建烧录；下文25脚/OLED描述为历史。

2026-10-28 B板本轮：已完成生命周期/调度模块化、UART电脑调试协议和中文Tk上位机。USB1/CH343使用UART0 GPIO43/44、115200，原生USB2保留MIDI；采样owner、MIDI router、UART RX/TX与NVS worker分离。支持逐路notes/gains、倒向力度曲线、原子apply/revision、实际保存ACK、软件试奏与逐次CSV。模拟门限PR1..PR12独立说明：比较器参考滑片空载0～2.5V、正反馈迟滞约0.3V，MCU无ADC回读。见 `docs/b-board-lifecycle-and-debug.md`、`docs/b-board-debug-protocol.md`、`docs/b-board-potentiometer-threshold.md`、`host/b_board_debugger/README.md`。

本轮验证：C原15组+3组映射/力度/试奏、GPIO、配置/协议/维护/CRC迁移、54包A兼容及9次实际PC↔C事务通过ASAN/UBSAN；PC27项含PTY和Xvfb实际Tk窗口通过，父任务再跑25项非GUI通过。ESP-IDF v6.1实际构建成功，app522624字节(0x7F980)，持久产物 `firmware/b_board/artifacts/20261028-lifecycle-debug/`，日志/基线在 `evidence/lifecycle-debug-20261028/`。正常目标1ms，未实测硬实时；未连接/烧录，未知编码器GPIO仍-1。A的3文件和原CAD5页指纹未变。Git无HEAD，仍全未跟踪、未暂存/提交。下文早期537536字节/REPL等为历史状态，本轮UART任务与产物为准。

2026-10-28：用户确认两FPGA独立供电、GND共地。已同步 `docs/20k138k-plan/`：138K保留原供电入口，20K首版独立USB，底板不接Nano J5.1到138K 5V，也不并联3.3V输出；公共地H1.6→Nano J5.2。SPI五信号针位、25脚及中置坐标不变，新增6线接线/电源表和图。单板掉电IO保护尚待实现/验证；未改CAD/RTL或写设备。

2026-10-28：完成双FPGA中置拓扑候选v2，见 `docs/20k138k-plan/`。最新约束：摄像头/音源/PCM在138K，传感器/OLED在20K；H1/H2/H3/H4两组40-pin和FDC共88个焊盘位置不变，其他模块/孔位可调。Nano放中间、USB向上，2.54mm同排/20.32mm跨排；建议SPI+READY、四组独立I²C、原生控制量主通路。按中置重新优化25脚表，SPI集中Nano右排。平面坐标已核对，Z叠层和模块电气尚未验收；未改CAD/RTL、未构建烧录。

2026-10-28 IO 审查完成：按原电路意图，十二路 GPIO 顺序及 EC11 十二条 RC 路径一致；修正 B 默认遮光极性为高有效（原默认低有效错误），在上升沿开始计时、下降沿恢复光照后发声。最新回归为配置检查+15组核心+GPIO+54包兼容，ASAN/UBSAN通过；ESP-IDF v6.1重新构建通过，应用537536字节（0x833C0）。用户说明EDA导入差异不影响实物，故只作取证限制、不列实物故障。审查报告 `docs/b-board-io-review.md`；原始五张CAD指纹不变。

2026-10-28：B 板独立 ESP32-S3 固件已完成实现、主机回归和 ESP-IDF v6.1 实际构建，位于 `firmware/b_board/`。B 负责激光/EC11 和 MIDI，A 是原主工程中负责 FPGA 通信的 ESP32；没有修改 A、FPGA 或 CAD，没有烧录。用户最后明确只交付 B 逻辑和接口报告，其他模块问题另列 MD，不作为阻塞。

用户已选择完整遮光结束后发声：每路独立计时，20–500 ms 默认线性映射力度 16–127，越久越强；恢复光照后 Note On，独立 gate 默认 200 ms 自动 Note Off。支持十二路同时处理、同音重触发、移调重合、Panic、四个 EC11、USB MIDI Device、发往 A 的 BLE MIDI central、UART 参数和显式 NVS 保存。

首次逻辑交付验证：14 组核心行为 + GPIO 校验 + A 现有解帧器 54 包通过 ASAN/UBSAN；实际 ESP32-S3 构建成功，应用 `0x833B0` / 537520 字节。产物 `/home/ice/.cache/laser-harp-b/build/`；日志、A 源码未变校验、输入与产物指纹在 `evidence/b-board-20261028/`。分支 `codex/laser-harp-b-board`，无 HEAD、未暂存/提交。

交付文档：`docs/b-board-interface-report.md`、`docs/b-board-integration-issues.md`、`firmware/b_board/README.md`。编码器 MCU GPIO 默认 -1，后续绑定实际 IO；实物电平、枚举和延迟留待联调。构建 profile 是 4 MB/无 PSRAM，不能当作实物型号证明。NVS 保存需在演奏间隙；正常扫描目标 1 ms，不声称维护命令期间硬实时。

## B 板后续对接

- 用户自行处理其他模块兼容/结构问题；已单独记录 KS 无 Pitch Bend、减法模板弯音范围、FM 八声部、跨链路确认边界等，未扩大修改范围。
- 实物联调或烧录须另有用户授权；编译产物不是烧录授权。
- 编码器 MCU GPIO 是可配置输入，尚未绑定不是当前逻辑交付的阻塞。
- 本轮未产生仿真文件；测试变体的临时源码/执行文件已清理，保留测试、日志、前镜像和构建产物。
- 本轮生命周期改造未新增FPGA/SPICE仿真产物；临时测试目录由脚本清理。英文目标构建目录 `/tmp/laser-harp-b-debug.DPq5Dt/build` 保留供增量重建，持久产物已复制到workspace，不依赖/tmp保存交付镜像。

## 实现前资料工作摘要（历史）

2026-10-28：完成转接板2→Nano20K辅助处理可行性评估，见 `docs/adapter-nano20k-offload-assessment.md`。用户确认原编码器20P改作双FPGA连接口，PCM5102不迁移。20P全信号、无电源/地、未直达传感器；外设原分线33 IO，另加SPI为37，合并兼容I²C/固定配置后的条件示例29。来源未保存原生SCH，导入源/历史PCB 149/162直接一致、13差异保留；20P为20/20一致。主工程源码默认传感器/HDMI/FFT关闭，未承诺当前资源收益，未改设计或构建。

2026-10-28：已整理 `docs/Tang-Nano-20K-PCB底板设计/`：37 份底板相关原始资料、6 个模型包解出 35 个文件（含 3 个整板 STEP），并完成以 3921 为基线的 40 针逐脚说明与 CSV/JSON。34 个 FPGA 脚号/IO 名/BANK 与 UG115 交叉核对通过；修正官方图第 31 脚 IOB29A 的错误标注，实际为 IOB18A。实际板卡版本尚未指定，未创建可投板封装或写硬件。

2026-10-28：已完成 Tang Nano 20K 官方资料归档至 `docs/Tang-Nano-20K/`：133 个来源文件（约 151.44 MiB），含 8 类下载目录资料、Wiki 文档/图片与固定提交示例 ZIP。1 个 17.79 MB PicoRV 工具包受下载站验证码限制未取得，见资料 README 和 manifest。校验通过；本轮仅收集资料。

2026-10-28：按用户指示停止 EasyEDA 接入尝试，改读刚导入的 KiCad 完整原理图。已确认五张 `.kicad_sch` 保存，直接通过 KiCad MCP 读取“光敏电阻 / 排插”（`5_排插.kicad_sch`），47 个元件、24 个命名信号网络；原生 XML 网表复核 12/12 编码器到排针连接一致。后续引脚核对使用 API/网表，不再渲染图片。IOxx=GPIOxx 的用户定义仍有效；实际编码器 MCU 映射不得凭排针位置猜测。尚未编写、编译或烧录固件。
## 2026-10-28 切回完整 KiCad 与 API 核对

- 当前工程：`/home/ice/桌面/ProPrj_1激光竖琴_2026-10-28_1/ProPrj_1激光竖琴_2026-10-28.kicad_pro`；目标原理图：同目录 `5_排插.kicad_sch`。最初检查时尚未保存的图纸随后已经出现，不再作为缺文件阻塞。
- KiCad MCP 已实际返回 H1.19→ENC_A1→C18.2/R69.1，H2.8→ENC_A2→C27.2/R75.1；完整 12 路排针连接与原生层级网表一致。
- 保存结果在 `evidence/kicad-import-20261028/`：源文件 SHA256 清单、目标页快照、分析 JSON、原生 XML 网表和连接复核。原工程未修改。
- 曾为视觉复核生成 SVG/PDF/PNG；用户要求直接使用 API，已停止这项渲染。该渲染从未作为电气连接的唯一依据。
- 用户停止 EasyEDA 接入后，已停止本任务新建且无连接/无待处理请求的桥接进程 2205343；未关闭 EasyEDA 客户端、未导入插件、未修改其项目或账户数据。
- 接口边界：当前原理图 API 读取的是已保存文件；不能声称取得未保存 GUI 状态，也不能把官方 CLI `doctor.connected=true` 等同于已取得 EasyEDA 当前原理图。

## 2026-10-28 IO 编号补充

- `IOxx = GPIOxx` 是用户确认的硬件事实，不再要求通过 H1/H2 到 J1/J2 换算这些标签。
- 已撤回的编码器位置推算与激光 GPIO1、2、39、40、41、42 冲突，不得用于固件。
- 原理图中的编码器命名为 ENC_A1/B1/SW1 至 ENC_A4/B4/SW4，没有相应 GPIO 数字；目前明确的是排针网络，不是 MCU GPIO。
- 确认结果及逻辑检查在 `evidence/io-clarification-20261028/gpio-mapping.json`；本轮修改前的文档与 `.ai` 记录保存到同目录 `preimages/`。

## 2026-10-28 完整工程接线核对

- 新来源：`/home/ice/桌面/ProPrj_1激光竖琴_2026-10-28.epro`，已保存快照与 SHA256 到 `evidence/easyeda-20261028/`；原文件未修改。
- SW4 / SW5 / SW6 / SW7 均使用 EC11E15244B2。1号 A/B/SW→H1.19/20/18；2号→H2.8/9/7；3号→H2.17/18/10；4号→H2.5/6/4。
- 本次最初曾把通用排针位置用于条件 GPIO 推算；后续用户明确 IO 标签就是实际 MCU 编号，该条件推算已撤回。有效定义见上方“IO 编号补充”。
- 详细接线、来源及条件推导见 `../docs/hardware-wiring.md`；机器可读结果见 `evidence/easyeda-20261028/header-connections.json`。
- 读取的是本地新版导出，不是 EasyEDA 窗口的实时编辑状态。未使用原 KiCad 旧工程推断当前是否缺少 EC11。
- `.ai` 四个记录修改前已保存到 `evidence/easyeda-20261028/preimages/`。

## 2026-10-27 准备检查

- 当前聊天目录最初为空 Git 仓库，分支 `master`，没有提交或用户文件。
- 实际打开的 KiCad 工程位于 `/home/ice/桌面/ProPrj_激光竖琴_2026-10-27/`。工程顶层缺少 `.kicad_sch` 和 `.kicad_pcb`；`.history/` 内保留一份接收板 PCB。没有修改或恢复该工程。
- 原始设计 `/home/ice/桌面/ProPrj_激光竖琴_2026-10-27.epro` 包含“光敏电阻”与“激光外设”两套板、共五张原理图；另有 `.epro2`，两者均已记录 SHA256。不能把历史 PCB 视为两套设计的完整当前工程。
- KiCad 10.0.7，已有 Seeed KiCad MCP 0.1.0，实际调用 `get_pcb_statistics` / `list_pcb_footprints` 成功。
- 已在 `/home/ice/.local/share/kicad/10.0/codex-ipc-venv/` 安装官方 `kicad-python==0.8.0`；原有 MCP 环境没有安装新依赖。
- 启用 `/home/ice/.config/kicad/10.0/kicad_common.json` 的 `api.enable_server`，旧文件保存在 `evidence/kicad-20261027/preimages/kicad_common.json`。
- PCB 编辑器打开的是 `evidence/kicad-20261027/inspection/laser-receiver.kicad_pcb` 检查副本；实时 `ping`、版本、封装读取成功，139 个封装。复查入口：`scripts/kicad_probe.py`。
- YD 开发板资料：用户提供 `/home/ice/Project/my_project/development/ESP32/esp32_reference/ESP32-S3_reference/`，已阅读 V1.4 原理图并渲染检查。
- FPGA 主工程 README 明确具有独立 USB MIDI Host。现有 S3 控制端 `ble_midi.c` 提供标准 MIDI GATT 服务与 WRITE / WRITE_NO_RSP / NOTIFY，设备名 `FPGA_Bluetooth`；仅检查源码，未确认实物烧录版本。
- 全程没有烧录 ESP32 或 FPGA；没有更改合成器仓库，也没有触发 FPGA 构建。
