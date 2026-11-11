# 变更与验证

## 2026-11-03 网页监控断线自动恢复

- 2026-11-03 16:34 JST 已修复ESP32断电后网页监控不能恢复：旧采集PID203397因SerialException退出而HTTP仍存活，现已替换为版本化入口host/b_board_debugger/web_monitor.py及web_monitor.html。网页http://127.0.0.1:8765/已连接实板并保留标签2，服务及唯一UART owner PID333294（线程采集），FT232R A5069RR4按by-id每秒重试；无4小时退出限制，手动停止会释放并停止重试，可页面恢复。MCU uptime回退会重新握手并重建事件会话；35项上位机测试通过，新增5项含真实PTY断开/重插、无UART断连的MCU重启、静默恢复、时间回绕及HTTP停止/恢复。实板已验证页面停止后无串口owner、恢复后唯一owner/配置回读正确，未人为物理拔插、未做新光学/MIDI音频验收。本次证据.ai/evidence/web-monitor-reconnect-20261103-162629，运行记录同目录runtime/；旧证据及原固件/Flash备份保留。本轮仅改主机监控/README及.ai四份记录，无固件/NVS写入，未暂存提交推送；电脑重启后需重新启动本机服务。
- 日志确证旧进程在1623.51秒退出，异常为device reports readiness to read but returned no data；不是4小时上限到期。新增单owner UART线程、稳定设备路径重试、只读重新握手、观测重启的事件去重重置，保留正常uint32回绕。
- 页面明确等待连接/数据/已停止状态，离线数据提示为缓存；新增恢复按钮，首次未收到state时也显示状态。CSV复用现有EventLog，最多保留4096条，网页最近100条、显示12条；解析帧日志4MiB×4轮换。
- 35项unittest（含Xvfb真实Tk）7.263秒通过；5项新增测试使用真实pyserial和PTY，不打开实物串口。随后实板HTTP/UART与页面停止/恢复验证通过，浏览器error/warn日志为空，截图monitor-restored.png。
- 已保存原Git差异、重叠README/.ai四文件前镜像及旧live/capture/process快照。测试TemporaryDirectory自动清理；没有新增仿真产物，保留测试源码、旧记录、重要日志、验收JSON和当前运行会话。

## 2026-11-03 实物烧录最新串口/反向力度版本

- 2026-11-03 14:53 JST 最新串口调试/短遮光更响固件已实物烧录并验收：ESP32-S3 84:c7:bb:79:d2:14，app523104字节/SHA256 e2bb487f90c352001f87e16a42016c0c83268befbad4e05ef52c72fa5ef26d71，32项生产输入及有效sdkconfig与镜像一致。FT232R A5069RR4（ttyUSB2）、460800/ROM no-stub，覆盖区0x0～0x90000备份与设备MD5一致；Bootloader/分区/app写入及独立verify通过，NVS/PHY逐字节不变。回读curve=[20,500,127,16]、config.channel=2/报文编号1，harp channel及harp get查询实际成功；无需apply/save，重开监控后的再次回读仍正确。实板running、USB MIDI在线、EC11 enabled=15，未发送软件strike或做新的受控光路/音频实测。网页http://127.0.0.1:8765/已恢复并保留标签1，HTTP PID203395、唯一UART采集PID203397，最长4小时；本次会话.ai/evidence/web-monitor-after-serial-midi-20261103-144756，后续不要另开串口。验收.ai/evidence/esp32-serial-midi-flash-20261103-144756/acceptance.json。本轮未改固件源码、A/FPGA/CAD，未提交推送；下方新曲线/串口入口尚未烧录记录属历史。
- 备份firmware/b_board/artifacts/preflash-serial-midi-20261103-144756/preflash-0x0-0x90000.bin，589824字节、ROM MD5 932f03e2bdaafe252a6fec1d77e3b043匹配；flash 19.34秒，独立verify 1.36秒，NVS/PHY读回 1.57秒，均exit0。两次启动回读确认新默认已生效，不需要额外NVS更新。
- NVS/PHY、原固件源码及已保存参数保留，旧监控目录与所有旧Flash备份未覆盖；仅新增烧录/监控证据并更新.ai四份记录。无新增仿真或临时测试可执行文件，保留备份、UART原始流、RPC帧、启动/烧录/校验日志。


## 2026-11-03 串口MIDI通道选择

- 2026-11-03 已保留并增强UART调试通道：新增harp channel查询、harp channel <0..15>选择MIDI报文编号、harp get读取完整配置；选择经原有旧通道清音屏障和ACK生效，显式harp save才跨重启保存。带config的回复新增顶层midi_channel_id，config.channel/RPC/NVS仍为1～16；默认报文编号1、公开号2及短遮光更响曲线[20,500,127,16]保留。桌面“力度曲线/演奏参数”选择框改为报文编号0～15，同时显示已应用/草稿及常用号。同步上位机GPIO白名单至固件现用表，修复实板四组EC11配置被拒导致握手/通道编辑不可用的问题。ASAN/UBSAN完整回归、全16个编号/清音屏障/NVS往返/USB-BLE编码、15次跨语言事务及30项桌面测试均通过；ESP-IDF v6.1构建exit0。固件在firmware/b_board/artifacts/20261103-serial-midi-channel/，证据.ai/evidence/serial-midi-channel-20261103/。本轮未打开实物串口、未烧录或apply/save，现有监控owner保留；未改A/FPGA/CAD，未提交推送。
- b_protocol新增人工选择/查询别名及配置回复midi_channel_id，b_runtime启动日志提示命令；上位机在显示/采集草稿边界转换通道编号，保留实际ACK和草稿语义。同步白名单为1,2,8,12,13,14,21,38,39,40,41,42,47，更新对应测试夹具与接口说明。
- RED分别复现人工命令不支持、GUI显示常用号2而非报文编号1、实板ENC2引脚被上位机拒绝；日志control-red/gui-red/pinmap-red保留。GREEN：全部0～15编号可选择/查询并通过旧序号/未清音屏障、NVS编解码往返及USB/BLE发包检查；15次PC-C事务包括0/15选择、查询、越界拒绝和显式save；实际Tk选择0/15、ACK后状态与越界禁止应用测试通过。
- 完整主机回归含原15组、短遮光更响12路序列、协议/生命周期/CRC及54包A解码兼容通过ASAN/UBSAN；桌面30项/5.161秒通过。已有实板配置捕获经新上位机校验可接受，未因此重新打开串口。
- ESP-IDF v6.1构建32.44秒/exit0，app523104字节，SHA256 e2bb487f90c352001f87e16a42016c0c83268befbad4e05ef52c72fa5ef26d71。源码指纹和生成SDK配置已核对，完整日志保留SDK原有Kconfig/组件私有包含提示。
- 全部本轮涉及文件的前镜像和原Git差异已保存。无新增仿真产物；临时控制测试目录由脚本清理，保留原有.build、ASCII构建/tmp/laser-harp-b-serial-midi-xku2ns_u、固件与重要测试日志。


## 2026-11-03 遮光时长与力度反向

- 2026-11-03 已按用户要求修正遮光时长→力度默认曲线：短遮光更响、长遮光更轻，velocity=[20,500,127,16]（≤20ms→127、100ms→109、260ms→72、≥500ms→16，gain100%）。保留恢复光照后触发、200ms关音、2ms去抖、CC7总音量及报文通道编号1/config.channel=2。固件fallback/Kconfig/sdkconfig.defaults/本地sdkconfig与上位机默认一致，启动日志打印实际端点。ESP-IDF v6.1构建exit0/app522896字节；12路六组时长输入到USB/BLE Note On/Off、主机回归和桌面28项测试通过。新曲线固件已生成，本轮未烧录、未打开串口或apply/save，当前实物最后一次已记录烧录仍是14:11的MIDI编号1镜像，现有监控owner保留。有效NVS旧曲线仍优先，后续上板需回读确认velocity=[20,500,127,16]。产物firmware/b_board/artifacts/20261103-short-shade-louder/，证据.ai/evidence/shade-velocity-inverse-20261103/；未改A/FPGA/CAD，未提交推送。
- 修改b_config/Kconfig/sdkconfig两份默认、启动日志、上位机默认及相关说明；补齐持续时间“最短/最长”文字，避免与力度数值最小/最大混淆。变更前镜像及原Git差异已保存，先前MIDI编号更新和后续实物烧录记录均保留。
- 新C回归对5/20/100/260/500/1000ms逐一驱动12路LOW→HIGH→LOW，不发送软件strike，验证恢复前不发音、恢复后力度127/127/109/72/16/16、200ms后Note Off、USB/BLE报文编号1及CC7仍为100；这是主机输入序列测试，非新实物光学验收。上位机新增相同默认曲线用例，500ms/150%gain演示夹具更新为velocity24。
- RED：旧默认在C和Python新用例中失败。首次GREEN验证发现测试期望把100ms舍入误写为108；独立有理数复核得到108.5→109，修正期望和示例后全绿，初次日志保留rounding-fixture-*。未为测试调整正确的插值算法。
- 主机完整回归含ASAN/UBSAN、9次PC-C事务、54包A解帧兼容均通过；Xvfb真实Tk完整28项/4.377秒通过。ESP-IDF v6.1实际构建36.77秒，app SHA256 79811803bacc33c4ef8fc15724e980de34a41374acfb156e8dbf79f858cf666c，生成SDK配置六项与源码指纹一致。SDK原有component_validation私有包含提示保留于build.log。
- 无新增仿真产物，控制测试的临时编译目录已由脚本清理，工作区无core文件；保留主机.build原有产物、ASCII构建/tmp/laser-harp-b-shortshade-tv292xfu/project及固件镜像/重要日志/测试源码。


## 2026-11-03 14:11 JST 烧录MIDI报文编号1固件

- 2026-11-03 14:11 JST 新的MIDI编号1固件已实物烧录并验收：B板ESP32-S3 84:c7:bb:79:d2:14，45项构建输入/有效sdkconfig与新镜像一致。FT232R by-id（当前ttyUSB2）、460800/ROM no-stub，覆盖区0x0～0x90000备份及设备MD5一致；Bootloader/分区/app写入和独立校验exit0，NVS/PHY逐字节未变。新启动日志“MIDI engine channel ID=1 (channel 2)”，get回读channel=2，后续监控重新打开导致的再次启动后也保持2，无需apply/save或覆盖旧参数。实板running、USB在线、EC11 enabled=15/四组引脚正确。监控 http://127.0.0.1:8765/ 已恢复并保留浏览器标签2，HTTP PID133901、唯一采集PID133902，最长4小时；当前会话 .ai/evidence/web-monitor-after-esp32-midi-channel-flash-20261103-140827，不要另开串口。app522768字节/SHA256 6dd307c9b9dad6ec965d1a2fcfb1fa0c6fc90cf35789dc7bdda35d4ac894e5b9；验收 .ai/evidence/esp32-midi-channel-flash-20261103-140827/acceptance.json。未修改源码、A/FPGA/CAD，未提交推送；FPGA此前绿蓝红循环未据本轮USB在线快照宣称修复。
- 保留此前所有完整Flash备份及监控数据，新覆盖区备份路径 firmware/b_board/artifacts/preflash-midi-channel1-20261103-140827/preflash-0x0-0x90000.bin。Bootloader/分区/app独立verify1.40秒通过，实际写入19.67秒；NVS/PHY读回1.75秒，启动与UART握手/HTTP页面验证通过。
- .ai四份记录更新前镜像和原Git差异已保存。未新增仿真文件或临时测试可执行程序；保留备份、脚本、UART原始流/帧、烧录和校验日志及监控会话。


## 2026-11-03 13:44 JST 重开监控

- 2026-11-03 13:44 JST 已按用户要求重开只读监控网页 http://127.0.0.1:8765/，实际页面“实板·已连接”、USB MIDI在线、四组EC11使能。旧采集已停止且旧HTTP进程身份核对后关闭，原始记录保留；FT232R A5069RR4重新枚举为/dev/ttyUSB2，按by-id连接。新HTTP PID103755、唯一UART采集PID103756，最长4小时或页面停止；后续读取本次live.json，避免另开串口。会话 .ai/evidence/web-monitor-reopen-20261103-134438/，浏览器标签2已保留。设备回读config.channel=1（报文编号0），新编号1/config.channel=2镜像仍尚未烧录；本次仅get/status/ping/stream，无apply/save/strike/烧录，原生USB可继续连接FPGA。
- 仅新建隔离会话并更新本次记录，HTTP实时JSON及浏览器实板页面验证通过；原有记录/未提交内容保留，未提交推送，无新增仿真产物。

## 2026-11-03 默认MIDI报文编号1

- 2026-11-03 已按用户最终澄清完成MIDI引擎通道默认值更新：目标是报文通道编号1（0～15），即常用通道2（1～16）。Kconfig/sdkconfig.defaults/本地sdkconfig、b_config fallback及上位机默认均设为channel=2；保留UART/NVS/GUI的1～16语义，启动日志同时显示报文编号和常用号。12弦的音符、CC7/74/71、Pitch Bend、Panic在USB/BLE均使用报文编号1；默认音符与遮光结束后发声规则未改。ESP-IDF v6.1构建成功，app522768字节；ASAN/UBSAN核心/9次PC-C事务/54包A解帧兼容与上位机27项测试通过，定向报文探针覆盖默认编号1及可配置0/15。新固件已生成，尚未烧录或写NVS，未改A/FPGA/CAD、未提交推送。产物 firmware/b_board/artifacts/20261103-midi-wire-channel1/，证据 .ai/evidence/midi-engine-channel-20261103/；若设备NVS仍保存channel=1，其报文编号仍为0，后续上板需显式核对并应用channel=2。
- 更新b_config fallback、Kconfig帮助/默认、显式sdkconfig.defaults及本地sdkconfig，启动日志同时打印两种编号；同步上位机默认及字段范围标签、接口/USB/BLE示例与配置文档。修改的既有文件完整前镜像和原Git差异均已保存。
- 定向探针在旧编号0默认上因状态字节低四位错误而失败（channel-red.log），修改后默认编号1及可配置编号0/15均通过（每种40条消息同时验证USB/BLE编码）；这是主机模型输入序列验证，非实物光路验收。更新已有切通道用例为常用2→3，继续覆盖旧通道清音屏障及持久化。
- ESP-IDF v6.1实际构建34.59秒/exit0，app SHA256 6dd307c9b9dad6ec965d1a2fcfb1fa0c6fc90cf35789dc7bdda35d4ac894e5b9。采用ASCII项目副本/tmp/laser-harp-b-channel-abc0ysfx/，源指纹和生成的CONFIG_B_MIDI_CHANNEL=2已核对。组件registry网络检查跳过，使用本地已锁定esp_tinyusb2.4.0/tinyusb0.21.0~2并实际编译；SDK既有Kconfig/私有包含提示保留于build.log。
- 受限环境的两个Tk测试无法连接Xvfb，失败日志保留desktop-tests-sandbox.log；主机环境重跑同一完整测试27项/4.479秒全通过。临时探针可执行文件已自动删除，无新增仿真产物；保留构建目录、固件产物、测试源码及重要日志。


## 2026-11-03 重烧并打开实板调试页

- 2026-11-03 用户更正接收电路后，重烧与网页调试已完成：32项生产输入及sdkconfig与2026-10-30已构建产物一致，复用ESP-IDF v6.1镜像；FT232R 460800 ROM no-stub烧录和三镜像独立校验通过，NVS/PHY逐字节保留。覆盖区0x0～0x90000备份与ROM MD5一致，旧16MB备份全部保留。实板running、USB MIDI在线、四组EC11绑定回读正确/enabled=15，当前raw=0x102、HIGH通道=2,9；本次未完成受控遮挡/恢复验收，不能仅凭电平认定前级完全正常。网页 http://127.0.0.1:8765/ 已实际打开并验证“实板·已连接”，HTTP PID51682、唯一UART采集PID51683，最长4小时或页面停止。当前记录 `.ai/evidence/web-monitor-20261103/`，不要另开串口；验收 `.ai/evidence/esp32-reflash-monitor-20261103/acceptance.json`。本次无固件/配置/上拉/极性修改，未改A/FPGA/CAD，未提交推送。
- 仅新增日期隔离的烧录/备份/网页证据并更新.ai四份记录；本轮涉及的.ai记录已保存前镜像，原有Git差异另存baseline.diff。FT232R驱动临时加载，未修改黑名单。初次新增Flash ID检查在attach前读到0xffffff，断言阻止后续操作；按本机esptool 5.4.0实现顺序移至attach后，实际ID0x184068/16MB，失败日志保留。本轮无新增仿真产物或待清理文件。


## 2026-10-31 重烧既有镜像并重启监控

- 本次监控已记录0x000→0xFFF的12路电平变化，尚无恢复沿形成的完整击弦事件；不能继续认定输入始终恒高。
- 2026-10-31 重烧并打开监控已完成：沿用指纹一致的10月30日ESP-IDF v6.1镜像（32项生产输入+sdkconfig未变），FT232R 460800、ROM no-stub写入/独立校验均通过，NVS/PHY逐字节保留。新做0x0～0x90000覆盖区备份并验证ROM MD5；原16MB备份保留。实物running、USB MIDI在线、四组EC11回读正确/enabled=15，当前raw=0xFFF（本次已观察12路LOW），不再沿用昨天恒高作为当前结论；完整遮挡/恢复周期仍由用户实验。只读网页 http://127.0.0.1:8765/ 已在右侧打开，HTTP PID752212、采集PID752213，最长4小时或页面停止；不要另开串口。证据 `.ai/evidence/esp32-reflash-monitor-20261031/acceptance.json`，实时记录 `.ai/evidence/web-monitor-20261031/`。未改固件/上拉/极性或A/FPGA/CAD，未提交推送。
- 原生USB ROM读取未响应，保留失败日志并改用已验证的FT232R默认复位路径完成备份/烧录。停止昨天已无采集子进程的旧HTTP服务，新建日期目录，保留全部旧数据。HTTP首页/JSON/CSV均200、浏览器实板状态和12路LOW显示验证通过。

## 2026-10-30 14:35 JST 打开真实串口监控网页

- 2026-10-30 14:35 JST 已按用户要求打开本机只读监控网页 http://127.0.0.1:8765/，浏览器标签保留。HTTP服务PID356692、真实FT232R单owner采集PID356694（最长4小时，可页面点击停止释放串口）；后续不要另开串口。真实握手get/status/ping通过，running、enabled=15、当前raw0xFFF；网页直接按raw及polarity显示受光/遮光电平，避免既有shade=0误标。包含12路电平、近期轨迹、hits/时长/力度/Note及CSV导出。只发送读取和stream，未apply/save/strike/烧录，固件指纹未变。运行日志/状态/验证在 `.ai/evidence/web-monitor-20261030/`。
- 网页、服务与采集脚本仅保存在本次证据目录，未修改生产固件/已有桌面程序；HTTP/浏览器实测通过。无新增仿真文件，未暂存/提交/推送。

## 2026-10-30 14:23 JST 自然光当前状态复查

- 2026-10-30 14:23 JST 自然光复查：用户说明现在已恢复自然光照射，FT232只读采集30.03秒/146帧，raw仍全程0xFFF、无输入变化/真实事件；running、EC11 enabled=15、USB在线。自然光未让数字输入进入受光LOW，尚不能确认实物感光触发；优先核对门限和OUT输出。采集结束、串口释放，无固件/参数/Flash写入。证据 `.ai/evidence/ambient-recheck-20261030-142346/result.json`。
- 未改固件、未构建/烧录；未暂存/提交/推送，无仿真产物。

## 2026-10-30 新GPIO固件烧录与自然光实测

- 更新board_io.c的OUT顺序、编码器GPIO校验白名单、Kconfig/sdkconfig.defaults及本地sdkconfig的12个EC11默认GPIO；保持音符/力度/极性/调音逻辑。同步接线与固件说明；保存全部重叠文件前镜像。
- 接线契约测试先因旧光敏数组失败；修正后主机15组核心、GPIO、映射/配置/协议/NVS/生命周期、9次PC↔C事务及54包A兼容全通过ASAN/UBSAN。更新了被新光敏占用的旧测试引脚夹具。ESP-IDF v6.1构建exit0、app522624字节，SHA256 `4b3873ebf163d949c832572ec122125059d99807548eda93de5ced4422d12d07`。
- 新做16MB完整Flash备份并核对设备MD5；Bootloader/分区表/app写入与独立verify均exit0，NVS+PHY逐字节未变。USB Laser Harp B /303a:4008、UART ping/get/status通过，实际绑定12个GPIO、enabled=15。
- 用户自然光遮挡/放开实测176.61秒/879帧仍raw0xFFF、变化/真实事件0；软件遮光逻辑通过，实物触发未确认。采集已结束、串口释放，没有发送软件试奏或修改NVS。证据 `.ai/evidence/esp32-gpio-flash-20261030/acceptance.json`。
- 未改A/FPGA/CAD、未提交推送。无新增仿真产物；保留ASCII增量构建 `/tmp/laser-harp-b-gpio-e8ygjfbt/` 和持久artifacts，测试脚本临时目录已自行清理。

## 2026-10-30 revision=4最终编码器GPIO确认

- 归档第4版五份交接原文件，用户逐项确认的12个编码器GPIO与原表一致；明确ENC_A4=GPIO2、GPIO12仅用于ENC_SW1，移除当前记录中的未知/重复疑问。保持光敏OUT通道顺序与用户列举顺序的区别。
- 更新交接README、接线说明、固件README及.ai四份记录；rev3资料与修改前内容保存在 `.ai/evidence/ec11-gpio-handoff-20261030-revision4/preimages/`。第1～3版检查标记被第4版替代，当前检查在同目录 `validation.json`。
- 12+12条CSV/JSON对应、用户确认、24个唯一GPIO、排针/PCB网络名及源指纹检查通过；45份固件输入指纹保持原样。PCB源指纹相对rev3再次变化，按版本保留，不声称源文件未变。未连接EasyEDA或打开串口，未改固件/配置/设备，未构建、暂存、提交、推送；无新增仿真文件或清理项。

2026-10-30 revision=3历史接收补充（已由revision=4取代）：复核时原文及JSON更新至revision=3，新增用户光敏GPIO清单，其集合与统一换算一致；列举顺序对应OUT11/9/7/3/5/12/10/8/6/4/2/1，未据此重排琴弦或修改固件。已保留revision=2接收副本并归档最新五份原文件，当前检查见 `.ai/evidence/ec11-gpio-handoff-20261030-revision3/validation.json`。

## 2026-10-30 revision=3 更正硬件重叠推断

- 撤回第一版六项实际GPIO重叠结论，纠正比较光敏旧IO文字与编码器参考图换算的依据混用。光敏按同一图得到4/5/46/6/18/7/9/15/10/16/11/17，编码器四组不变，共24个唯一GPIO、无交集、不占19/20。
- 归档更正后版本MD、编码器CSV、新增光敏CSV、参考PNG及revision=3 JSON。两组各12条CSV/JSON对应、24个排针/网络名称及当前源文件指纹校验通过；PCB更正后版本指纹与第一版不同，分版本记录，不声称跨版本源文件未改。
- 改正交接README、接线说明、固件README和.ai四份记录；保留十项旧标签/固件与参考图差异，明确固件未同步。第一版validation标为superseded，原数据和文件先保存在 `.ai/evidence/ec11-gpio-handoff-20261030-revision2/preimages/`。
- 本轮验证45份固件输入保持原样。未访问串口、连接EasyEDA、改配置、构建或写入设备；无新增仿真/清理项，未暂存/提交/推送。

## 2026-10-30 EC11 GPIO资料交接（首版，已由更正后版本更正）

- 接收并归档完整MD、UTF-8 BOM/CRLF CSV、参考PNG和证据JSON，四份副本与源文件逐字节一致；CSV12条与JSON信号/GPIO/排针及PCB网络证据12/12相符。
- 首版比较当前 `b_laser_pins` 与新编码器表，记录六项数字交集；更正后版本确认这是混合依据，不得作为实际硬件冲突。编码器12脚唯一、USB19/20交集为空；首版保存时的源指纹及12条资料一致性检查保留为历史。
- 更新 `docs/hardware-wiring.md`、B README及.ai四份记录，明确已有GPIO来源但最终统一配置未决。修改前内容、Git差异和45份固件输入指纹保存在 `.ai/evidence/ec11-gpio-handoff-20261030/`。
- 本轮未改固件逻辑或配置，未启动串口、EasyEDA接入、构建或设备写入；无新仿真产物或清理项，未暂存/提交/推送。

2026-10-29 20:36 JST：通过现有 DebuggerApp/Controller API启动可见真实上位机并自动选择FT232R串口，等待串口开口复位稳定后握手；实测ready=true、5 Hz遥测、raw=0xFFF。新增任务证据launcher与状态快照，未修改生产GUI/固件或设备配置，未提交/推送。

## 2026-10-29 遮挡阶段澄清与连续监听

- 用户说明当前保持遮挡。补充区分“遮挡HIGH正确”与“尚未观测受光LOW”，不据恒高单独判故障；90秒跟随采集仍为0xFFF，完整照射/遮挡/恢复操作尚未确认。
- 新增 `.ai/evidence/laser-live-20261029/capture.py` 用单一FT232连接被动记录GPIO状态、事件及USB MIDI，最长600秒自动关闭，SIGTERM/SIGINT可安全结束；已语法检查、进程存活及实际状态帧验证。进程信息/期限在同目录process.json，实时状态在live.json。未向设备发送配置或烧录命令，未改生产代码/参数，未产生仿真文件。

## 2026-10-29 用户确认遮挡后的120秒重采

- 重新只读采集FT232 UART和USB MIDI：120.02秒/596帧，raw只出现0xFFF，电平变化/真实击弦0、hits全0；max_gap1373us、missed/drops0，当前配置读回正常，未修改设备设置。
- 保存原始UART二进制/文本、逐帧JSONL、USB MIDI及结果至 `.ai/evidence/laser-physical-20261029/`，笔记修改前镜像保留同目录。更新弹奏检查报告，明确本次物理操作已获用户确认，与前轮90秒数据分开。
- 当前等待用户测1OUT受光/遮挡两种电压。没有代码/配置/硬件写入、构建或仿真产物，也未删除原文件、暂存、提交或推送。

## 2026-10-29 光敏接收与弹奏逻辑只读检查

- 用户接手EC11，停止相关推导。通过FT232采集90秒UART及USB MIDI，446帧有效扫描中raw均0xFFF，真实事件0；独立读取当前配置成功，未apply/save或烧录。
- 重跑完整C主机回归，15行为组、映射/配置/CRC/协议/维护、9次PC↔C与54包A兼容通过ASAN/UBSAN。定向探针复现启动未武装高电平被显示“光照”，同时验证100ms遮光恢复后力度35及200ms关音、AUTO USB优先。
- 新增 `docs/b-board-play-logic-review-20261029.md`；证据、源代码指纹、四份笔记前镜像、探针源码及日志在 `.ai/evidence/laser-play-review-20261029/`。生产代码、设备配置和桌面源文档保持原样，本轮无FPGA/SPICE仿真产物；临时主机执行文件确认后清理，无暂存/提交/推送。

## 2026-10-29 桌面EC11接线文档核对

- 读取并快照 `/home/ice/桌面/EC11编码器引脚与电路连接说明.md`；确认12条排针连接与既有资料一致、上拉3.3V/1k/串联1k/100nF、按键低有效。文件本身未给实际MCU GPIO，因此未进行绑定。
- 证据和三份笔记前镜像在 `.ai/evidence/ec11-pin-document-20261029/`。没有修改原文件、固件或设备配置，没有构建/烧录，也未生成或删除仿真产物；当前任务等待H1/H2到开发板连接关系。

## 2026-10-29 实物识别、16 MB首刷与链路验收

- 只接收读取原生USB启动日志，识别DOWNLOAD等待模式；ROM安全信息/Flash ID查询成功，无写Flash。读出启动头1024字节及分区表4096字节，原factory为1 MB。
- 按用户后续明确烧录授权准备首刷，保存配置/分区/说明/笔记前镜像。首轮stub备份因USB重新枚举失败，用户手动下载模式后改用ROM。为避免一次累积16 MB的缓冲开销，使用256 KiB分块落盘；完整备份16777216字节，设备/文件MD5均为 `4a2e37e004fe027e42c9778e42719a1b`。
- 将sdkconfig.defaults及本机生成配置同步为16 MB，单factory分区扩展到`0xff0000`、保留NVS/PHY地址。PSRAM保持关闭、编码器12项GPIO保持-1。更新B README及硬件接线记录的实测容量证据。
- 项目build.sh在ESP-IDF v6.1/锁定TinyUSB依赖下实际构建exit0；产物、配置和指纹在 `firmware/b_board/artifacts/20261029-first-flash/`（Git忽略）。备份/构建/重连日志在 `.ai/evidence/esp32-first-flash-20261029/`。本轮无新增仿真产物，未暂存、提交或推送。
- 通过无stub的ROM写入 `0x0` Bootloader、`0x8000` 分区表、`0x10000` 应用；应用522624字节，SHA256 `50766f0a9ebf5073bae5fcc87d329f66a462b999f32394d8e19f8b18ce09e456`。写入自带校验及独立verify-flash三段digest全部通过，未整片擦除。
- 看门狗完整复位后识别 `Laser Harp B` / `303a:4008` / `84C7BB79D214`，ALSA MIDI端口正常；12秒接收观察稳定且收到初始控制消息。
- 用户确认FT232R连接ESP32 UART后，临时加载现有ftdi_sio驱动，保留黑名单文件原样。实际115200 `ping/get/status` 成功，running；`strike 1 100` ACK及synthetic事件匹配，USB实际收到 `90 3C 23` / `80 3C 00`。本次观测last_gap1000us、max_gap1375us、missed/drops0；只作当次观测，不声明硬实时保证。
- 完整验收汇总 `.ai/evidence/esp32-first-flash-20261029/acceptance.json`，原镜像备份保存在Git忽略目录、权限0600；分块partial完成后改名为完整备份。未删除既有文件或仿真结果，未改A/FPGA/CAD。

## 2026-10-29 GitHub 上传准备与复核

- 用户授权提交并上传当前分支至 `ICECREAM1230/Lets-go`；为当前仓库设置 `origin` 和已有账户的提交身份，保留原目录结构及分支名。
- C 主机回归通过：15组核心行为、GPIO、映射/力度/试奏、配置/协议/维护/CRC、9次真实PC↔C事务、54包A解帧兼容；使用 ASAN/UBSAN。桌面程序通过27项测试，包含PTY与Xvfb下实际Tk窗口。
- 对比上一轮 ESP-IDF 构建记录的39项输入，SHA256全部一致；本轮不宣称重新交叉编译。上传前候选605文件、385698193字节，最大单文件45881859字节；高置信度凭据模式检查未发现匹配。
- 本轮日志和结果在 `.ai/evidence/github-upload-20261029/`。修改前的笔记、Git配置、完整文件SHA256基线保存在 `.git/codex-upload-20261029/`；本轮临时主机测试执行文件验证后清理。未生成新仿真产物，既有测试、仿真结果及资料均保留。
- 已审查暂存差异及基线指纹。源码/编写文档的空白检查通过（识别CSV的CRLF）；既有归档、网表渲染、补丁及工具日志中的空白提示按原始字节保留，不作无关格式化。

## 2026-10-28 移除OLED规划

- 当前规划CSV删除OLED两信号和模块位置候选，更新拓扑、I²C预算（3组独立）、0Ω位置预算（34位、默认17颗）、机械说明和布局图，移除DISPLAY_STATE显示载荷建议。
- 校验23个GPIO唯一、只移除55/71，其他23项及桥接/固定坐标保持不变。未改CAD/RTL，无构建或仿真产物；前镜像与检查保存在 `.ai/evidence/20k138k-remove-oled-20261028/`。

## 2026-10-28 B板结构化、UART上位机与PR门限说明

- 新增纯C `components/b_control/`：配置/原子patch、严格RPC、JSON、CRC持久化与v2迁移、可测生命周期；pinmap原内容迁入。harp_core增加逐路notes/gains、倒向曲线、越界静音及带synthetic标记的试奏。
- 入口改为b_entry/b_runtime，新增b_router/b_console/b_storage；移除旧app_main/console。扫描不直接打印、格式化、发送USB/BLE或写NVS；加入精确清音屏障、维护回滚、真实ACK和时序/丢弃统计。
- 新增 `host/b_board_debugger/`：中文四页签GUI、UART worker、草稿/版本冲突处理、曲线预览、逐次CSV、JSON配置、离线演示；同串口MCU重启观察与重新回读。USB调试链路按用户选择独立为UART。
- 更新B README/接口报告及root索引，新增生命周期报告、调试协议与PR1..PR12阈值解释；逐路核对固定10k/PR10k/47k/100nF/470k，滑片开路0～2.5V与迟滞示例约0.31V，未声称实测电压。
- C原15行为组、3组新映射、GPIO、控制域/协议/维护/CRC、9次真实PC↔C事务、54包A解帧均通过ASAN/UBSAN；当前ptrace限制关闭LSAN。PC27项含PTY及Xvfb真实窗口通过；父任务追加非GUI25项通过、2项GUI按无DISPLAY跳过。新鲜只读审查无剩余Critical/Important。
- ESP-IDF v6.1真实构建exit0，应用522624字节，SHA256 `027b6d1854858d257565bfc33baf07ead676071caed0140227b37ee2a258dc4a`。app/bootloader/partition/ELF/config与manifest在 `firmware/b_board/artifacts/20261028-lifecycle-debug/`（Git忽略）；未写设备。
- 本轮前镜像/日志/指纹在 `evidence/lifecycle-debug-20261028/`；A3文件及原CAD5页最终SHA256与既有基线一致。未新增仿真产物；脚本临时测试输出已清理，原sim和重要历史结果保留，英文目标构建目录保留供重建。无暂存/提交。

## 2026-10-28 IO 接收审查与默认极性修正

- 新增 `docs/b-board-io-review.md`，同步接口报告、硬件接线、B README及独立兼容文档。按用户澄清，将导入瑕疵归为表达/取证限制而非实物缺陷。
- Kconfig和sdkconfig.defaults默认改为高有效；本机生成配置同步。新增test_io_profile.py配置/顺序检查、十二路高电平完整时长回归：RED→GREEN，最终15组+GPIO+54包ASAN/UBSAN通过，目标构建exit0。
- 保存MCP读结果、原始和临时唯一标注副本网表、四页分析、十二路前级/编码器矩阵、官方PDF及指纹于 `evidence/io-receive-audit-20261028/`。原始CAD五张SHA256保持一致。
- 新增 `sim/config/b_io_receive.cir`；ngspice-42行为级接口/RC测量通过，结果保留 `sim/results/b_io_receive_20261028/`。工作目录按要求使用 `/media/ice/D;/FPGA_simulation/laser_harp_b/io_receive_20261028/`，空目录已删除；未删原有或不明文件。
- B镜像更新为537536字节；无烧录、无A/FPGA修改、无暂存提交。

## 2026-10-28 独立 B 板固件、时长力度与接口报告

- 新建 `firmware/b_board/`：纯 C 音乐/输入核心、独立 gate/逐音符力度与触发编号、GPIO 校验、TinyUSB MIDI、NimBLE central、UART 命令、显式 NVS 保存、Kconfig/分区/锁定依赖、构建与主机测试脚本。
- 四个编码器定义：移调/CC7/CC74/CC71；按键微调−/+1cent、滤波复位、Panic。保留未知 GPIO 为 -1，不套 A 引脚。
- 按用户选择追加完整遮光结束后发声；新增力度曲线/gate配置、独立计时、同音重触发及移调重合回归。status 改快照，由 REPL 打印。
- 原核心 RED 9组、GPIO RED、时长 RED 4个失败均记录，最终 14 组 + GPIO + A 解帧器 54包 ASAN/UBSAN 全过。逐路触发编号变体被移调重合回归捕获；临时变体已删除。
- ESP-IDF v6.1实际目标构建通过，应用 537520 字节；输出与指纹记录在 `evidence/b-board-20261028/`。A 的3个只读接口参考文件 SHA256 与实现前一致。新鲜只读审查未发现剩余重要问题。
- 新增根 README、B 使用说明、第三方许可、`docs/b-board-interface-report.md` 和独立 `docs/b-board-integration-issues.md`；更新接线记录、执行计划与 .ai 四份笔记。
- 修改前的原笔记/接线/.gitignore 保存在 `evidence/b-board-20261028/preimages/`，时长需求前的本次代码快照在 `pre-duration/`。保留失败/成功日志、测试和有用产物；没有仿真产物，没有删除原有文件。
- 当前分支 `codex/laser-harp-b-board` 仍无 HEAD，所有内容未暂存/提交。没有修改或烧录 A/FPGA/CAD；本轮没有网络服务连接尝试。

## 2026-10-28 新 KiCad 工程与直接 API 读取

- 按用户要求停止 EasyEDA 接入工作，确认自建网关无客户端/无待处理请求后终止进程 2205343，保留日志和停止记录；未改变其客户端生命周期或导入插件。
- 定位新 KiCad 目录 `ProPrj_1激光竖琴_2026-10-28_1`，确认五张已保存的 `.kicad_sch`，通过 MCP 直接读取排插页及 H1/H2 引脚、命名网络、层级连接。
- 用原生 `kicad-cli sch export netlist --format kicadxml` 导出层级网表，12/12 编码器到排针连接与 MCP 及历史源提取一致。仅对这些连接作出结论，不宣称完整电气/实物验收。
- 在 `evidence/kicad-import-20261028/` 保留源清单、目标原理图快照、分析 JSON、原生 XML 网表及复核结果；曾生成的渲染产物保留为诊断记录，已按用户偏好停止进一步渲染。
- 更新 `docs/hardware-wiring.md` 和 `.ai` 记录。原项目未编辑，未生成仿真文件，未构建或烧录固件，未暂存或提交。

## 2026-10-28 用户确认 IOxx 为实际 GPIOxx

- 修正 `docs/hardware-wiring.md` 和 `.ai` 当前记录，按用户确认的编号语义登记 12 路激光 GPIO；撤回按排针位置换算的表和 GPIO46 推导。
- 保存用户补充后的 `evidence/io-clarification-20261028/gpio-mapping.json`，编码器实际 GPIO 未知项明确为 null；文档修改前已保存前镜像。
- 验证源文件包含全部 12 个 IO 标签和 12 个 ENC 标签；激光 GPIO 唯一且不占用 USB19/20。检测到旧编码器位置推算与激光 GPIO1、2、39、40、41、42 冲突，因此撤回，不写入代码。
- 本轮没有修改硬件设计、编写或构建固件，也没有烧录、FPGA 构建或产生仿真文件。未删除预先存在文件，未暂存或提交。

## 2026-10-28 切换完整 EasyEDA 来源

- 保存新版原始 `.epro` 的任务快照、SHA256 与结构化排针连接；修改前的 `.ai` 四份笔记已留前镜像。
- 新增 `docs/hardware-wiring.md`，列出四个 EC11 的 A/B/SW 到 H1/H2 的确切连接，并分开列出待确认的 YD 直插 GPIO 推导和 12 路接收网络。
- 验证：从原始符号 PIN 坐标和 WIRE.NET 提取四个编码器、12 路完整且无重复的编码器网络；与系统 `pcbnew` 10.0.7 读取新版历史 PCB 的 H1/H2 焊盘网络 12/12 一致。
- 更新 `.ai/progress.md`、`decisions.md`、`issues.md`，撤销旧来源对当前工程“没有 EC11”的推断；保留历史记录和原文件。
- 本轮仅资料读取与记录，没有写 EasyEDA/KiCad 设计，没有固件编译、设备烧录或 FPGA 构建。没有生成仿真文件，也未删除原有文件。未暂存或提交。

## 2026-10-27 KiCad 接入准备

- 新增只读探测脚本 `scripts/kicad_probe.py`，报告 IPC 连通性、KiCad/绑定协议版本、当前 PCB 文件名及实际封装/走线数量；有错误时返回非零，不保存或修改 PCB。
- 新增 `.gitignore`，仅忽略本任务的 PCB 检查副本目录、PDF 渲染图片与 Python 缓存。
- 新增 `.ai` 状态、决策、问题记录；在 `evidence/kicad-20261027/` 保留安装日志、源文件指纹、配置前镜像、PCB 编辑器日志和探测结果。
- 用户级设置变更只有 KiCad API 开关；安装路径为 `/home/ice/.local/share/kicad/10.0/codex-ipc-venv/`，绑定固定 `kicad-python==0.8.0`。
- 已完成：KiCad CLI 版本、MCP 文件读取、原生 PCB IPC 读取、YD 原理图视觉检查、FPGA Host/BLE 源码接口检查。最终只读探测结果见 `evidence/kicad-20261027/ipc-probe.json`。
- 未完成：激光竖琴固件编写/编译、USB MIDI 实机枚举、无线 MIDI 实测、激光电平/极性实测、EC11 输入测试。
- 本轮未产生仿真文件；没有删除任何预先存在的文件。检查副本、原理图渲染和诊断日志保留用于后续核对。未暂存或提交。

## 2026-10-28 Tang Nano 20K 官方资料下载

- 新增 `docs/Tang-Nano-20K/`，保存官方目录、Wiki 文档/图片、示例工程 ZIP；README、manifest 和 SHA256SUMS 提供分类、版本与来源。共 133 个成功来源文件，158792671 字节。
- 原理图保留 3850 / 3920 / 3921 / 3923；示例仓库提交 `e23949a0a77381b94960cbc4e97a7c5e5ba8d222`，ZIP 不含外部子模块。
- 复核 SHA-256、字节数、PDF/7z 标识与 ZIP/XLSX CRC，全部成功归档项通过。1 个 PicoRV 工具包返回错误 JSON，移除本任务产生的无效 ZIP，保留错误响应与入口。
- 修改前笔记保存在 `evidence/tang-nano-20k-20261028/preimages/`；下载日志和验证结果保存在同目录。未生成仿真文件，未改变构建输入，未触发构建/烧录，未暂存或提交。

## 2026-10-28 Nano 20K 底板资料与逐脚分析

- 从已有全量归档复制 37 份原理图、BOM/装配、尺寸/DXF、原始模型压缩包、管脚/器件规格与官方图片，原归档未修改。新目录 `docs/Tang-Nano-20K-PCB底板设计/`。
- 解压官方 6 个 7z 包得到 35 个文件，包括 v3850/v3920/v3921 整板 STEP 与 VRML/依赖；复制件尺寸目录中重复的 3D 包已清除，原始归档与模型专项目录保留。
- 编写 README 和 40 针 CSV/JSON：J6 左排、J5 右排，34 信号+6 电源/地；以 3921 MAIN/层级页为基线。与 UG115 交叉核对 34/34 脚号、IO 名和 BANK。官方图第31脚的 IOB29A 已在表内纠正为 IOB18A，原图未改。
- 核对 37 份复制文件 SHA-256、全部模型字节数/哈希、STEP/VRML 标识及 174 个 VRML 相对引用，均通过；实际装配干涉与实物未验证。证据、前镜像和脚本保留于 `evidence/nano20k-carrier-20261028/`。
- 未触发 FPGA 构建、固件编译或设备写入，未生成仿真产物，未删除任何预先存在的文件，未暂存/提交。

## 2026-10-28 转接板2与20K辅助处理评估

- 新增 `docs/adapter-nano20k-offload-assessment.md`，覆盖摄像头、FDC、双测距、双超声、OLED、供电和20P连接。PCM_A/B不迁移；原编码器接口按用户确认改作板间连接，不再当作20路编码器负载。
- 通过KiCad MCP读取导入历史PCB32个封装，再用原生pcbnew独立读焊盘；解析同名.epro原理图PIN坐标/WIRE.NET。162个接口针位149直接相符，13个差异保留；20P连接20/20一致，全部去138K排针且无地/电源。
- 外设（不含PCM和旧编码器）为33个独立信号，板间SPI另加4；记录共享低速I²C后的29脚条件方案，以及模块电平/时钟和Nano板载复用限制。未把条件预算当作最终物理分配。
- 只读核对主仓库活动GPRJ、顶层与约束：源码默认ENABLE_SENSOR_I2C/ENABLE_HDMI/ENABLE_FFT均0；不把未启用功能当成可释放的现有资源。未量化资源节省。
- 证据和前镜像：`evidence/adapter20k-offload-20261028/`。未编辑桌面原项目、主仓库或固件，未产生仿真文件，未启动综合/PnR/设备写入，未暂存提交。

## 2026-10-28 20K中置拓扑与桥接规划v2

- 新增 `docs/20k138k-plan/`：总体拓扑、I²C共享与MIDI/原生桥接对比、机械规划、25脚CSV、5线SPI表、40针本地/放置坐标、88个固定锚点、可移动模块中心候选、SVG/PNG示意。
- 用户先要求按现有布局评估，随后明确Nano中置、两组40-pin与FDC不动，其他模块/孔位可调；板顶外扩初稿已移入证据历史，不作为当前方案。摄像头明确保留138K。
- 独立解析官方3921 iBOM压缩数据取得40孔和板边；固定锚点由KiCad pcbnew API读取。Nano排针阵列中心(146.995750,109.740500)，平面左右余量到内针列中心线约11.58/11.60mm；Z尚未验收。
- 为直连右侧40-pin，SPI重分配为Nano42/41/48/54/56，138K仍A19/A18/C17/D17/B18；端点直线约12.96～21.87mm。旧20P不要求绕行；电源/地单独处理。
- 规划检查：25脚唯一、40孔节距/排距、88点原坐标及方向不变、5条网络对应、帧和IO预算、SVG XML通过；源文件输入指纹无漂移。未做实物装配、STA或仿真，未改CAD/RTL/CST/SDC。
- 更新前笔记和前轮评估有前镜像；证据在 `evidence/20k138k-topology-20261028/`。无本轮仿真产物；原有sim/和固件文件未触碰，未暂存/提交。

## 2026-10-28 规划文档同步独立供电

- 更新 `docs/20k138k-plan/README.md`、`mechanical-layout.md`、`mechanical-plan.json` 和 `fpga-link-plan.csv`；新增 `power-ground-wiring.csv`、`interboard-wiring.csv`、`power-and-interconnect.svg/png`。
- 明确5V/3.3V分域、H1.6与Nano J5.2固定共地、旧20P不能拿信号当地；图中逐项显示5条SPI/READY方向及独立USB供电。机械字段、原五信号针位和固定锚点未改。
- 增加各上电/掉电状态、跨板上拉和反灌保护条件；不把RTL高阻/串阻作为已验证的掉电保护。保护适配和电源有效使能仍未实现。
- 文档/CSV/JSON与SVG一致性检查通过，两个地引脚与既有资料核对通过，接线图已视觉检查，git diff --check通过。未跑硬件测试、综合、PnR或仿真；无本轮仿真产物。
- 修改前内容及校验记录保留于 `evidence/20k138k-independent-power-20261028/`。没有修改CAD/RTL/CST/SDC或固件，未暂存/提交，未触碰其他工作内容。
