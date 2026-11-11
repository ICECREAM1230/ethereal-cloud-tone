# 空灵云音 · 手机 App

Android 原生 App（Java / Android Views / Canvas），面向 Android 8.0（API 26）及以上。演奏、音色、连接、曲谱四页，无 WebView、外网 CDN、云账号或遥测。当前版本 `0.10.0`。

2026-11-03：新增原站查谱、HTTPS 图片/PDF 直链下载、本地导入与离线谱夹，支持分页、缩放、平移、另存和删除。演奏页增加三首内置原创节奏练习、40–180 BPM、一小节预备拍及暂停/继续；紫色“弹/下”提示与触控、发送状态分开，提示不会自动发送音符。这里的“脱机”按用户确认指无需互联网、仍连接 ESP32 发声；App 不增加本地音源或实体 LED 协议。普通谱图只用于查看，不自动识谱或同步亮键。安装、限制与验证见 [曲谱与跟练交付](docs/score-guide-20261103/交付.md)。

2026-11-03：补齐此前只存在于浏览器原型中的 KS 木质拨弦界面。选中 KS 演奏路由即显示六根横弦 × C／Am／Dm／F／G／Em 六列和弦，36 音名在同一个木质表面中；点弦、划奏、列名和上下扫弦接入 App 音符发送路径。原木纹直接打包入 APK，未引入 WebView。默认收起 KS 底部键盘以展示完整弦面，可手动展开；已有键盘／Pad／体感仍在同一个 App 中。音符、力度、音量、声像和端口互斥沿用现有接口，T60 继续遵守设备能力。设计与验证见 [KS 原生交付](docs/ks-native-20261103/交付.md)。

2026-11-03：蓝牙默认改为标准 BLE-MIDI 单向发送。GATT 连接并发现可写 MIDI 特征后即可演奏，不再请求 capabilities、session、token、context、owner 或设备状态，也没有空闲心跳。键盘、打击垫和体感映射输出 Note On／Off 与 CC；减法／KS／FM 分别使用 MIDI 通道 1／2／3，由 ESP32 解码。界面区分本地默认值、排队值和已发送值，不把写入成功当成设备采用或实际发声。Wi-Fi HTTP 行为保持原样，与蓝牙互斥。标准 UUID、支持范围、实机限制和验证见 [蓝牙 MIDI 直发交付](docs/ble-midi-direct-20261102/交付.md)。

2026-11-02：接入 AE350 主工程配套 ESP32 的 HTTP v2／私有 BLE 控制服务。演奏按当前客户端 MIDI 通道选择减法／KS／FM，界面独立显示演奏路由和全局编辑器；采用后保留 token，避免重建会话退回 KS。蓝牙列表补充已配对和系统 GATT 已连接候选，仍须手动连接并验证控制服务。Wi-Fi／BLE 保持互斥。只修改本 App，没有写入 AE350／ESP32／FM 源码或实体设备；当前 native FM 参数合同不匹配时继续禁用该编辑接口。范围、验证和安装见 [AE350 接入交付](docs/ae350-service-20261102/交付.md)。

2026-10-28：体感增加独立Sensor→MIDI数据层及“MIDI”设置。左右倾斜可选音符或CC7/10/74，前后倾斜可选CC7/10/74或关闭；挥动生成Note On/Off和力度，显示实际三字节消息。0.6.0 当时的 MidiOutput 适配为 ESP32 私有控制帧；0.8.0 蓝牙入口现已使用标准 BLE-MIDI，USB-MIDI 仍未接入。接口和历史传输边界见 [传感器MIDI交付](docs/sensor-midi-20261028/交付.md)。

0.5.0阶段：演奏方式菜单增加“体感”，手机运动经校准/手动开始后，倾斜选C3–C5大调音，挥动触发短音。0.6.0保留此默认映射并增加可选CC与标准MIDI数据层。优先姿态/线性加速度，缺失时回退加速度估计；默认不启动，异常/断代/后台等停奏，恢复需手动开始。传感器基础与验证见 [体感交付](docs/motion-20261028/交付.md)。

0.4.1阶段：键盘控制台与打击垫合并到一个主APK，共用音色、力度与同一Wi-Fi/BLE会话；切换先松开旧触点，不重连或重置音色。0.5.0在此基础上增加体感并改为方式菜单，原独立Pad APK作为历史产物保留。[合并记录与验证](docs/unified-20261028/实施与验证.md)。

0.4.0阶段：平板控制台按双振荡器／滤波器、包络／LFO 分区，演奏与音色页底部固定键盘。旋钮支持相对拖动、精调与整数输入；演奏条区分本地触控和 owner 采用。按用户要求精简常驻说明，保留必要名称、数值、状态和按钮，详细说明放在“帮助 / 诊断”。当时采用独立Pad备选APK，现已由0.4.1主包内切换取代；协议与音源范围不变。历史交付见 [控制台交付](docs/console-20261028/交付.md)。

2026-10-25 已完成 App 端互斥 Wi-Fi / BLE 控制端口：连接页二选一，切换先释放旧按键并关闭旧端口，再等待手动连接。Wi-Fi 保留现有 HTTP；BLE 提供可配置 UUID、Android GATT 客户端和独立分片请求/响应接口，等待未来设备实现配套私有服务。ESP32 硬件本轮不处理，不把接口测试当成实板连接成功。接入入口见 [控制端口交接](docs/control-ports-20261025/接入说明.md) 与 [BLE 候选协议](docs/control-ports-20261025/BLE控制接口-v1.md)。

本副本手机工程位于仓库内 `apps/android/`；以下迁入记录为历史来源：原 canonical 工程迁入基线为 `main` 的 `446ef90ba330dc99b183f8679cd67d4d9b7db481`。2026-10-11 从最新实际 `mobile-app` 迁入 97 项文件并核对 SHA，保留现有交互和构建方式。源码来源、原内容恢复归档、原路径失败日志和本轮实际构建见 `docs/stage2-20261011/`。

当前已接入正式 S3 HTTP v2.0、schema `1.0.1` / ABI `1.1` 的 FM 运行客户端。原生界面按验证目录显示预设、参数和实际声部／算子；只有真实设备能力与 READY／SELECTED／已应用模式匹配时开放 FM。缺 FM 的 v2 设备保留 DDS／KS；只有能力路由返回404才回退旧 v1。0.3.0 已在专用模拟器验证连接页、模拟触控、KS、后台恢复与 Wi-Fi v1 mock；FM 的原生运行、实体 BLE、实板／音频仍未验收。FM 协议和历史证据见 [FM 运行期交付](docs/fm-runtime-20261011/交付.md)。

## 安装与连接

本源码副本不附带历史 APK 或开发签名。运行下方构建命令后，安装包输出为 `delivery/fpga-synth-0.10.0.apk`；包名为 `cn.fpgasynth.control`，versionCode12。首次构建生成本地开发签名；没有相同签名时不能覆盖升级其他构建。一个APK包含键盘、木质KS、打击垫、体感/MIDI映射、谱夹与节奏跟练。使用手机文件管理器打开 APK，并按 Android 安装界面的提示允许该文件来源安装。开发签名仅用于本地测试；不会自动安装到任何实体设备。历史版本与截图的下文说明属于迁入记录，不代表本源码副本附带这些产物。

查谱：在“曲谱”页输入歌名，原站搜索由浏览器打开；按原站规则保存后导入图片/PDF，或粘贴实际文件的HTTPS直链下载。App不代登录、付费或自动聚合站点曲谱。每份最多20MB，谱夹最多50份/100MB；保存后无需互联网即可阅读。外部浏览器或系统文件选择器会沿用既有停奏策略，返回后蓝牙需要手动重新连接。

跟练：先连接ESP32，再到“演奏→节奏跟练”选择练习和速度，点击开始。预备拍后按紫色“弹”提示手动弹奏，“下”表示下一个音；KS优先提示当前和弦列中的可用交点。运行时收成简洁提示条，可直接暂停/继续/停止，点击提示条展开简谱、设置与成绩。提前/稍迟的毫秒数是本地触键时差，不是设备音频延迟。切页、切换音源/端口、断线、后台或失焦会停止提示，不自动恢复。模拟体验只能预览，无声音。

蓝牙：打开“连接 → 蓝牙控制”，扫描并选择 ESP32（当前设备名 `FPGA_Bluetooth`），点击“连接蓝牙 MIDI”。允许系统要求的蓝牙权限；不需要输入私有服务 UUID，也不需要先在系统设置中配对。系统已配对／已连接只表示候选设备，仍需 App 建立自己的 GATT 连接。电脑测试结束后已释放该连接；若设备只接受一个中央连接，先断开其他控制端再重试。连接成功即可演奏，不会自动发送默认音色参数。

Wi-Fi 操作沿用原流程：

1. 手机连接 ESP32 的 Wi-Fi，SSID 和密码以当前固件配置为准。系统提示没有互联网时保留连接。
2. 打开 App 的“连接”页并选择“Wi-Fi 控制”，设备地址默认 `http://192.168.4.1`，可以修改为实际 HTTP/HTTPS origin。
3. 点击“连接设备”。App 先读取设备已有 patch，再用空按键状态建立 owner 确认；不会把默认滑块值发送到设备。
4. 在“演奏”页弹奏或切换减法／KS／FM。AE350 通道路由设备在切换后保留会话，“演奏…MIDI…”和“编辑…”分别表示当前演奏通道与共享编辑器；下方参数跟随共享编辑器。旧 native FM 设备的预设选择仍先清键再重建会话；请重新按键。

Wi-Fi 与蓝牙二选一，切换先松开旧音符并关闭旧端口，随后等待手动连接。没有设备时选择“进入模拟体验”，两个物理端口均关闭，不连接设备、不播放声音。旧私有 BLE RPC 源码和测试保留作历史兼容参考，0.8.0 蓝牙界面不使用该握手流程。

## 已实现范围

- 25 键 C3–C5（MIDI 48–72），多触点、滑奏、同一键多指保持；力度 1–127。
- 音量、声像；减法合成器／Karplus–Strong／FM 演奏路由。蓝牙直发支持三条路由的音符、音量和声像；KS T60 0.2–8 秒与 native FM 专用编辑沿用 Wi-Fi 能力门控，蓝牙中不开放尚无 MIDI 映射的接口。
- 全部 38 个 HTTP 白名单 CC：双振荡器波形／半音／微调／脉宽，声部结构、混合、齐奏失谐与空间；滤波类型、斜率、截止粗细调、共振、跟踪；音量与调制 ADSR、调制深度；两组 LFO、LFO 2 目标及力度响应。
- Wi-Fi 状态跟随设备与其他客户端，正在拖动的控件暂不被轮询覆盖。蓝牙显示本地默认和成功发送的参数，不读取设备音色。
- Wi-Fi 保留采用确认和模式读回；蓝牙保留物理连接／发送错误与本地队列诊断，不显示虚构 token 或 owner。

FM 目录来自正式生成闭包与 S3 的分页接口，profile目录ID与wire backend ID分开。13个当前JT51参数使用chip_code／next_note，不将原始码伪装物理单位；完成缓存和ESP32采用是不同证据。0.3.0 增加了私有 GATT 控制客户端和分片协议，设备端尚待对接；USB Host、实时PCM／FFT与HTTP延音仍未接入。`design/` 下的历史浏览器原型原样保留；0.9.0 已将木质 KS 弦面实现为原生交互并接入发送。

## 演奏安全与状态含义

蓝牙直发在输入时按顺序发送完整 MIDI 消息，不读设备状态、不轮询、不发租约心跳。队列最多 32 个过渡，超过 300 ms 的旧非零输入作废；切换通道先释放旧通道。后台、失焦、断线和恢复清除旧意图，重新按键才会发送新的 Note On。蓝牙“停止演奏”发送已知音符 Note Off 及三个通道的 CC123／CC120；断线时无法保证停音消息送达，设备仍需自行处理连接丢失。本地发送指示不证明 ESP32／FPGA 采用或实际声音。

以下 token、owner、状态回读和租约说明适用于保留的 Wi-Fi／私有 RPC 路径：

每 token 全部命令共用一个序号。v1使用严格五整数、v2使用严格七整数 ASCII body；HTTP context_epoch 与原生 FPGA engine_epoch 分开。命令单线程发送，HTTP 202 仅表示收到；下一按键过渡等待 `tokens[client]`、`processed[client]` 与 `keys[client]` 的 owner 确认，因此快速按下松开不会被合并吞掉。参数确认使用 `parameter_processed`，全局停止使用 `stop_processed`；模式还要求 requested/applied 一致、有效读回、非切换／恢复状态。

FM 参数等待不会阻断250 ms按键心跳；模式／预设／Panic、外部上下文、native session或布局变化会清旧意图并重建空会话，PLL失锁后不自动SELECT。

250 ms 按键心跳，最多 32 个过渡；非零排队帧超过 300 ms 作废。后台、失焦、网络故障、停止、引擎切换与恢复会清本地指针和旧队列，尽力发零位图；网络不可用时依赖固件 1.2 秒演奏租约，包络可能有尾音。重新连接必须重新按键，不重放旧 NoteOn。已接受但丢失回执的请求不会以新序号盲目重发。

普通松键只释放本 App 来源。“停止所有声音”使用 op4，会停止 App、网页、MIDI 等全部来源并关闭诊断测试音。ESP32 采用、SPI 模式读回、实际发声是不同证据；本 App 不宣称 FPGA 声音已确认。

`0.1.1` 修正恢复兼容：固件 `engine.fault` 是保留的上次故障记录。恢复、切换、模式或有效读回尚未就绪时仍禁止演奏；安全恢复完成后，历史 fault 不再阻止新按键或模式确认。恢复会清除原按键，必须重新按下；原始故障记录仍可在诊断中查看。

## 复现构建与测试

所有操作从本仓库 `apps/android/` 目录运行。Linux x86_64 / Python 3.12 路径已验证。既有工具链可通过 `FPGA_APP_TOOLCHAIN` 显式只读引用；本轮使用以下路径，没有把 SDK、JDK、模拟器或构建缓存迁入主仓库：

```bash
export FPGA_APP_TOOLCHAIN='/home/ice/文档/ChatGPT/用户界面前端开发/mobile-app/.toolchain'
```

独立恢复时若没有该已安装路径，取消该变量后运行 `python3 tools/setup.py`，只在本工程 `.toolchain/` 下载锁定版本。`setup.py` 不写引用的外部工具链；运行构建和测试无需重新安装依赖。未修改系统设置或其他工程依赖。

```bash
python3 tools/fm_contract.py
python3 tools/test_fm_guard.py
python3 tools/test.py
python3 tools/test_practice.py
python3 tools/test_score_files.py
python3 tools/test_control_ports.py
python3 tools/test_ble_midi.py
python3 tools/test_direct_midi.py
python3 tools/test_midi_integration.py
python3 tools/test_http.py
python3 tools/test_abi.py
python3 tools/test_fm_api.py
python3 tools/test_fm_runtime.py
python3 tools/test_http_v2_handler.py --vectors build/fm-current/runtime/fm-http-vectors-historical.json
python3 tools/build.py
python3 tools/build_smoke.py
```

`test.py` 已包含 AE350 冻结真实 C 服务复编译及 18 场景／421 条客户端断言；单独运行入口为 `tools/test_ae350_service_capture.py` 和 `tools/test_ae350.py`。默认只依赖 App 内的冻结输入，不读取其他代理正在修改的 AE350 工作树。

`setup.py` 下载固定版本并核对 SHA256；首次需要网络。`test_abi.py` 是历史 v1 ABI 入口，需要系统 C 编译器及 `docs/integration-20261005/参考来源.json`、`app-api-reference.tar.gz` 两份冻结输入。本源码副本已恢复这两份必要输入；2026-11-12 仅按已授权的项目名/MIT 许可归一重新封装并记录 identity 迁移，解析函数逐字节保留。实际验证 53 项参考 SHA、257 条 Java 编码报文与冻结 C 精确解码，以及 16 条非法报文拒绝均通过。当前 HTTP v2 和 AE350 C/Java 交叉验证使用 `contracts/` 内实际保留的输入。`test_fm_runtime.py` 本次生成的 HTTP 向量写入 `build/fm-current/runtime/`，不再写历史交付文档目录。

当前冻结输入、生成器和五种原始导出位于 `contracts/fm-control-v1.0.1-20261026/`；`contracts/fm-control-v1.0.1/` 作为历史闭包保留，`core/src/generated/java/` 是当前原样 Java 编译副本。APK、JVM、HTTP、ABI、mock 和原生测试包构建入口统一先运行只读校验；缺文件、多余文件或内容漂移立即失败，不自动修复。`test.py` 同时运行既有 26 组回归、冻结 fixture 驱动的 461 条 Java 断言、43 条控制台断言及当前 C 闭包测试。新旧 FM 合同分别绑定，不放宽 SHA 门槛。同步说明见 [FM 合同记录](docs/console-20261028/fm-合同同步与验证.md)。这些静态检查本身不证明硬件能力；运行期客户端由 API／生命周期／localhost HTTP／真实 C handler 交叉测试验证，仍不能证明实际发声。

版本：Temurin JDK `17.0.20.1+1`、Android SDK Platform `36 revision 2`、Build Tools `36.0.0`；编译／目标 API 36，最低 API 26。无外部运行时库或 Gradle 下载依赖。构建通过官方 `aapt2`、`javac`、`d8`、`zipalign` 与 `apksigner`。工具 URL、SHA256 和上游校验见 `tools/toolchain-lock.json`。

APK、SHA256、构建信息与开发 keystore 在 `delivery/`。keystore alias `development`，测试密码 `android`，用于可重建与后续本地升级；这是明确的开发签名，不能作为正式发行密钥。GitHub 上传使用独立索引，仅纳入 App，上传记录见对应交付目录；共享工作树的分支与索引保持原样。

## HTTP 模拟设备

```bash
python3 tools/mock.py 8765
```

仅监听 `127.0.0.1`。Android 模拟器连接地址为 `http://10.0.2.2:8765`；模拟器默认 Wi-Fi 通过 NAT 访问主机。模拟器不播放声音，也不验证 SPI。模拟器之外的手机可先用内置模拟体验。

本地测试设施 `/__mock__/fault` 支持 `owner_delay_ms`、`freeze_owner`、`drop_next_note_response` 和 `offset_ms`。它不是 ESP32 固件 API，App 不调用这些路径。真实 HTTP 场景测试自动启动临时端口并关闭服务。

Android 运行复现（额外下载约 1 GB 模拟器与镜像，需要可用 KVM）：

```bash
python3 tools/preview.py setup
python3 tools/preview.py run
```

保持模拟器运行；在另外两个终端分别运行 `python3 tools/mock.py 8765` 和 `python3 tools/preview.py test --mock`。测试只接受 `emulator-<port>` 目标，默认专用端口 5564／ADB 服务端口 5048。`python3 tools/preview.py stop` 关闭本模拟器。源码恢复快照包含版本锁定与脚本，离线构建快照不额外包含约数 GB 的可重建模拟器磁盘。

本机在新共享 Wi-Fi 模式测到约 500 ms 的模拟器 loopback 转发延迟，导致 350 ms 网络期限触发安全释放。按 [官方模拟器网络说明](https://developer.android.com/studio/run/emulator-networking-interconnect) 使用 `-feature -WiFiPacketStream` 恢复原网络模式后，原生 HTTP 全链路测试通过；`preview.py run` 已固定该本地预览选项。App 不依赖此模拟器选项，不为规避测试延迟放宽演奏租约或重放请求。

## 网络与权限

Wi-Fi 声明 `INTERNET` 和 `ACCESS_NETWORK_STATE`；每个请求使用已有 Wi-Fi `Network.openConnection`，不要求网络已被验证可访问互联网，因此不会因为移动数据是默认路由而把 ESP32 请求发向移动网络。没有自动扫描 SSID、修改 Wi-Fi 或安装 VPN。原生 HTTP 不受浏览器 CORS 限制，仍发送固件要求的 `X-FPGA-Control: 1`。

BLE 能力为可选，不支持 BLE 仍可使用 Wi-Fi。Android 12+ 在用户扫描/连接时申请 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT`（扫描声明 `neverForLocation`）；Android 8–11 扫描需要 `ACCESS_FINE_LOCATION`，旧权限仅到 API30。扫描有 10 秒上限，进入后台、取消或切换模式会停止扫描；授权返回只在前台恢复原操作。正常退出先尝试松键后关闭，阻塞时 1.5 秒取消旧端口。权限与协议细节见 [BLE 规范](docs/control-ports-20261025/BLE控制接口-v1.md)。

设备地址可配置，因此 Network Security Config 为 App 开放明文 HTTP；HTTP 层关闭跳转、限定响应大小，地址仅允许 origin，禁止凭据／路径／query／fragment。该控制路径面向用户选择的本地设备。参考 [Android 网络安全配置](https://developer.android.com/privacy-and-security/security-config) 和 [Network.openConnection](https://developer.android.com/reference/android/net/Network)。

按 [Android 局域网权限文档](https://developer.android.com/privacy-and-security/local-network-permission)，目标 API 36 的局域网访问由 `INTERNET` 兼容授权，不声明／请求 `ACCESS_LOCAL_NETWORK`。目标升级到 API 37 时须加入并在运行时请求该权限，处理拒绝／撤回；本次可下载仓库没有稳定 API 37 平台，未声称完成该目标的安装验证。生命周期参见 [Android Activity 生命周期](https://developer.android.com/guide/components/activities/activity-lifecycle)。

## 交付与验证边界

`0.4.0`：控制台、精调输入、Pad、多指与生命周期经专用 Android 模拟器运行验证；横竖屏、短横屏及 1.5 倍系统字体均保留可操作区域。主版和 Pad APK 均通过签名、对齐检查。音符／owner 指示来自实际控制状态；设备尚无音频遥测接口，界面显示“音频 —”，波形和包络为参数示意。最新证据见 [控制台交付](docs/console-20261028/交付.md)，以下为历史版本记录。

`0.3.0`：新增控制端口370余条运行时断言通过（计数随分片/并发取消点变化，最终完整回归380条）；旧 JVM/HTTP/ABI/FM 回归与 43 项 SHA 绑定的 C handler 验证通过。开发签名 APK 已安装到专用模拟器，5 组接口/授权回调场景、9 组原生触控/生命周期/KS/Wi-Fi mock 场景通过。实体 BLE、ESP32 硬件、设备间控制仲裁和实际声音尚未验证。当前准确输入、RED/GREEN、截图、限制和恢复入口见 [本轮验证与交付](docs/control-ports-20261025/验证与交付.md)。

当前冻结 Java 接入见 `docs/fm-codegen-1.0.1-20261011/接入交付.md`、`接收基线.json`、`构建输入.json`、`验证结果.json` 与 `只读审查.md`；此前迁入交接保留在 `docs/stage2-20261011/`。本目录的恢复检查点见 `snapshots/索引.md`。原始角色文档、失败历史及 0.1.0 原生截图作为历史证据保留；源工程的 `snapshots/05-sticky-fault-fixed-offline/` 及后续 06/07 快照仍在只读来源，未复制数百 MB 工具链归档。未修改 FPGA、S3 或网页源码。

`0.2.0`：368条API边界断言（其中profile99/backend1与异构VOICE_OPERATOR为隔离重生成输入）、60条会话生命周期、5条发现、11条真实localhost HTTP断言通过。真实C handler复现8份原字节fixture，81条固定parser边界和26条实际App编码请求通过；原guard9、JVM26、生成461、v1 HTTP与C ABI257+16保持通过。APK签名／对齐及原生测试包仅构建通过，未安装或运行。独立审查3项问题均已修复并保留RED／GREEN证据；Android弹窗和触控仍未运行验证。旧0.1.0截图仅属于历史版本。

构建、JVM、localhost HTTP 和 Android 模拟器都不能代替 ESP32 SoftAP／实机路由／FPGA 音频验收。实板验证与设备操作需要后续对应授权。
