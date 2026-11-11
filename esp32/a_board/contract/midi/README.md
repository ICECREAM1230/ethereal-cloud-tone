# FPGA 原始 MIDI 传输契约 v1

2026-10-24更新：生产输入为USB0 / Web1 / BLE2 / 原生传感器3。0x070A INPUT_CAPABILITIES bit0仍为INDEPENDENT_USB，新增bit1=NATIVE_SENSOR3；生产FPGA当前返回3。bit1=1时，RAW_BYTE与单来源FLUSH只接受来源0..2（RAW_BYTE仍排除0），source3请求明确拒绝；ENABLE及FLUSH_ALL不影响传感器。传感器通过SNS1/MOD1逻辑接口配置与读回，内部选择FDC或双激光。

S3识别bit1后不绑定、排队或清理source3，避免引擎切换时发送旧DIN清理命令。旧DIN/面板UART不再参与生产组件编译，历史源码与旧镜像协议参考仍保留。下文source3 DIN和四来源RAW约定仅适用于未声明NATIVE_SENSOR3的历史镜像；USB/Web/BLE协议保持原用法。


2026-10-16。本轮用户授权：USB Host 与统一 MIDI 解析接入正式 FPGA；ESP32-S3 转发 Web/BLE 原始 MIDI，并继续托管网页与必要参数控制。原有 DDS/KS/FM 参数、预置、引擎选择 SPI ABI 保留。本文定义新增传输接口，不能把传输确认解释为已产生声音。

## 文件与协作边界

- FPGA owner：`audio_i2s/eda_proj/`、本目录；不修改 S3/Web 源。
- S3 owner：`development/ESP32/ESP32_development/esp32s3/`，包含最终嵌入 HTML。
- Web owner：`development/用户界面开发/web/`，输出单文件 HTML；不覆盖 S3 嵌入文件。
- 本轮不 stage/commit/push、不写 `.ai/`、不下载或复位设备。

## SPI 兼容扩展（冻结 wire v1）

沿用现有 SPI Mode0/MSB-first、8-bit command + 16-bit word address。写：`00 addr_hi addr_lo data[31:24] data[23:16] data[15:8] data[7:0]`；读：`80 addr_hi addr_lo 00 00` 后读 32-bit big-endian。仍由原控制平面驱动 MISO/ID `A5`。新增 `0x0700..0x07FF`，不改变 `0x05xx` KS、`0x06xx` FM 或其它现存语义；该范围不再映射普通 store RAM。

| word address | 属性 | 内容 |
|---|---|---|
| `0700` | R | `4D494431`，ASCII MID1 |
| `0701` | W | COMMAND，单个完整32-bit word提交；禁止burst |
| `0702` | R | STATUS：bit0 mailbox busy，bit1 performance enabled；其余位0 |
| `0703` | R | ACCEPT_COUNT，已完成合法命令数，32-bit自然回绕 |
| `0704` | R | REJECT_COUNT，busy写入及非法/禁用raw命令数，32-bit自然回绕 |
| `0705` | R | EVENT_COUNT，统一解析器向演奏消费者交付的事件数；包含消费者接受的非演奏事件；在进入消费者前取消的旧事件不计数，不代表音符已发声 |
| `0706` | R | USB_STATUS：bit0 configured，bit1 error；其余0 |
| `0707` | R | DDS_TEMPLATE_STATUS，最后完成锁存的DDS COMMIT generation |
| `0708` | R | KS_TEMPLATE_STATUS，最后完成的KS模板提交标识（见ks-fm-template.md） |
| 其它 `07xx` | R/W | 读0；写入不产生演奏命令 |

COMMAND 编码：`[31:24] opcode, [23:16] source, [15:8] reserved=0, [7:0] payload`。

| opcode | source | payload | 语义 |
|---|---|---|---|
| `01` RAW_BYTE | 1 Web / 2 BLE / 3 DIN | 原始MIDI字节0..255 | 进入该来源的独立 byte ready/valid 队列；不合并不同来源字节，不改写Running Status、实时消息或SysEx |
| `02` FLUSH | 0 USB / 1 Web / 2 BLE / 3 DIN / `FF`全部 | 0 | 清该来源会话、取消积压并释放该来源声部；确认后才允许新会话字节 |
| `03` ENABLE | `FF` | 0或1 | 默认0；每次改变启用状态清全部来源和声部。1允许演奏，0使raw命令被明确拒绝。重复同值合法但不引入额外会话 |

USB数据只能来自 FPGA Host，SPI RAW_BYTE 的 source0 被拒绝。未知opcode/source、非零reserved或非法payload被拒绝，不能当有效MIDI。复位清计数、禁用演奏并清会话。

单槽完整 payload + req/ack toggle 跨 SCLK→synth_clk；payload 保持到确认。只有接收器消费该字节或完成flush/enable后，合法命令才计入 ACCEPT_COUNT。busy 写入不能覆盖前一payload，必须累计REJECT_COUNT。CS拉高只结束SPI帧，不能清mailbox或截断命令。

## S3 发送与恢复

1. 探测magic；不匹配时报告功能不可用，不能悄悄退回S3声部分配。
2. 以现有控制通路完成引擎/音色模板初始化，确认旧控制事务完成，再 ENABLE=1。初始化/恢复期间保持禁键。
3. 单一SPI owner串行执行：读STATUS等busy=0，记录accept/reject，再写一个COMMAND，读回直到busy=0且accept恰增1、reject未变。32-bit差按模计算。跨调用互斥，参数事务不得插入同一个CS帧。
4. 不确定提交、确认超时或reject变化时停止该来源，新会话先显式FLUSH，不能重发不确定的NoteOn。错误不能假报演奏成功。
5. BLE解析仅去除BLE-MIDI封装和timestamp；保持原MIDI字节顺序，不做音乐消息解析/声部分配。Web完整消息可编码成原始字节发送。
6. Web/BLE断连、队列溢出/会话失效只flush自身；用户全局Panic和引擎切换可flush全部，防止跨模式持键或重播旧消息。

有限队列不承诺无条件无损，SPI为逐字节确认的首版稳定通路，后续批量化必须另行版本化。HTTP接收/排队、FPGA解析、音源采用与PCM输出是不同观测层。

## Web HTTP 配套边界

保留现有会话/epoch、snapshot/release、租约/Panic及参数控制接口，避免覆盖此前恢复修复。新增通用原始MIDI入口使用 `POST /api/v1/midi/raw`，JSON `{\"source\":\"web\",\"bytes\":[144,60,100]}`，1..256字节，整数0..255，不接受其它来源伪装；放入Web来源发送队列。成功返回202仅表示排队，返回 `{\"accepted\":true,\"queued_bytes\":N}`；满队列503、非法数据400、演奏未就绪409。需复用已有绑定/授权检查；断网与旧会话不能重放。现有演奏snapshot可继续作为有租约的高层输入，由S3按差量编码NoteOn/Off原始字节，不分配硬件voice；这层状态是网络释放保障，不是第二套MIDI解析。

接口实现需两端据当前HTTP框架细节验证；不得把本条绕过现有绑定检查。S3必须在原有演奏路径统一转换并停用旧动态voice寄存器写入，防止同一按键同时走两条演奏路径。

## 验收

位级SPI原控制回归、新mailbox无额外SCLK提交/CS中断/busy拒绝、CDC/复位；USB与Web/BLE多源Running Status和SysEx独立、来源flush；FPGA演奏NoteOn/Off/velocity0/重复音/抢占/sustain/bend和模式切换；正式工程官方综合/PnR/STA；S3真实构建、网页单测/浏览器和嵌入文件hash。新正式镜像与固件的板测、完整键盘到PCM 5ms均须独立验证，不能继承此前独立工程结论。

## 音色模板契约

DDS详见 [dds-template.md](dds-template.md)，KS/FM详见 [ks-fm-template.md](ks-fm-template.md)。高地址模板编译是参数控制，MIDI演奏期间S3不做voice分配。原有静态element/producers/rates初始化仍保留。需等模板提交ACK与既有bank/模式确认后才ENABLE。
