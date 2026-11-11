# B 板对接兼容性与结构问题

日期：2026-10-28。供用户处理 A 板、FPGA 和系统集成时参考。**这些项目不阻塞本次 B 板逻辑交付；本次没有修改其他模块。** 范围是本次已读源码和接口，不是其他模块的全面审查。

已确认事实与待测事项分开描述；没有把尚未实测的现象写成已发生的故障。对接协议见 [B 板接口报告](b-board-interface-report.md)。

## 已确认的功能/容量约束

### C01：KS 路径目前不使用 Pitch Bend

- **证据/可信度：高，当前源码。** `/home/ice/Project/my_project/development/tang_mega_138k_synth/audio_i2s/eda_proj/src/synthesizers/karplus_strong/control/midi_ks_performance.sv:50` 实例化 `midi_voice_owner` 时 `HAS_BEND=0`。
- 影响：B 的 ENC1/ENC2 按键分别发送 −1/+1 cent 的 Pitch Bend，当前 KS 不会据此微调音高。旋转 ENC1 发送改变后的 MIDI Note，半音移调不依赖 Pitch Bend。
- 可由接收端处理：若要求 KS 连续微调，需要实现并验证 KS 相应的音高更新通路。B 继续输出标准 Pitch Bend，不改成私有协议。

### C02：减法音源的弯音范围可以被 A 的模板改变

- **证据/可信度：高，当前源码。** `/home/ice/Project/my_project/development/tang_mega_138k_synth/audio_i2s/eda_proj/src/synthesizers/subtractive/control/midi_dds_performance.sv` 定义默认 `bendrange=2`，可由模板字 `0x8041` 改写，Pitch Bend 乘当前范围计算。FM 的 `midi_voice_owner.sv:54` 使用 Q16 半音、满幅约 ±2 半音。
- 影响：B 按固定 ±2 半音换算 cents；若减法模板使用其他范围，则“按一下 1 cent”会产生不同实际偏移。FM 当前符合该换算，KS 则见 C01。
- 可由系统约定：将支持微调的接收器范围固定为 ±2 半音，或后续给 B/接收器增加明确的范围协商。当前 B 不主动发送 RPN 改范围。

### C03：FM 当前只有八个声部，B 能输出十二路同时触发

- **证据/可信度：高，当前源码。** `/home/ice/Project/my_project/development/tang_mega_138k_synth/audio_i2s/eda_proj/src/synthesizers/fm/control/midi_fm_performance.sv:42` 的声部索引为 3 位，下一行实例化 `.VOICES(8)`。
- 影响：十二路同一时刻完成遮光且 gate 重叠时，B 会如实发送十二个音符；FM 可能按已有分配策略抢音，不能承诺十二音同时保留。这是接收端复音容量约束。
- 可由系统决定：接受八声部/抢音策略，限制同时发音需求，或扩展接收端复音架构；扩展须同时评估底层音源容量与索引，不宜只改一个常数。

## 已确认的协议边界

### C04：USB 与 BLE 必须分别解封装

B 原生 USB 使用四字节 USB MIDI event；BLE 特征使用时间戳头加 MIDI status/data（通常五字节）。A 应继续使用 BLE MIDI 解帧器，不能直接把整个 BLE payload 当作原始 MIDI，也不能把 USB 的 CIN 字节塞入 BLE。现有 A 解帧器已对 B 编码的 54 个测试包验证通过；未改 A 源码。

### C05：发送成功不构成跨板执行确认

- **证据/可信度：高，B/NimBLE 发送接口和实际接收结构。** BLE 使用 Write Without Response；B 的成功只表示本地发送 API 接受。USB FIFO 接受也不等于 FPGA 已合成该音符。
- 当前 A 使用 BLE raw source 2（`main/ble_midi.c:82`），USB 解码使用 source 0；减法与公共 KS/FM 声部 owner 按 source/channel/note 匹配 Note Off，清理也按来源隔离。
- **先前猜测已排除**：在当前源码中，不能把“BLE 迟到的 Note Off 会关掉 USB 来源的新音符”列为已确认缺陷。已有 source 隔离证据反驳了这一推断。
- 仍存在的系统边界：热插拔切路或 `both` 可产生短暂重触发/叠音，且不同传输不具备共享完成屏障。若系统需要严格无重触发切换，需要接收端协议/路由层提供相应机制；现有 B 可用固定 `usb` 或 `ble` 避免自动切路。

### C06：断连清理需要接收端兑现

B 检测断连后会忘记旧连接影子，新连接先发 CC120/123 再同步当前状态；断开的线路本身无法继续送 Note Off。A 当前源码具有断连 raw source flush，但仍需确认实物烧录的是相应版本；USB Host 也应在设备移除时释放该来源的音符。此项是接收端契约，不是已经实测的故障。

## 待验证或可配置事项

| 项目 | 当前已知与边界 | 后续对接工作 |
| --- | --- | --- |
| CC7/74/71 的音色语义 | B 固定输出标准通道音量、截止、共振；各音源的曲线/可用参数未全面核对 | 检查所选音源映射和听感，不把全部 CC 支持当成已验收 |
| A 的 BLE 服务 | 当前源码名称 `FPGA_Bluetooth`，标准 service/characteristic，名称在 Scan Response | 核对实际 A 固件；若改名称，可配置 B 的匹配名称 |
| 加密/绑定 | B 没有主动配对命令和持久绑定操作；现有测试覆盖报文，不覆盖安全握手 | A 若要求加密/绑定，另做互操作和断连/重连验证 |
| 原生 USB 数据口 | YD-S3 USB2 是 GPIO19/20；USB1 经 CH343P；电路的供电型 Type-C 不等于 USB MIDI 数据口 | 用实际原生数据链路连接 Host，核对枚举；本次未改电路 |
| EC11 的 MCU 引脚 | 已知编码器到 H1/H2 的连接；尚无可靠 MCU GPIO 对应，B 默认 `-1` | 后续按实际 IO 定义绑定，不套用排针物理号 |
| B 板存储规格 | 构建 profile 为保守 4 MB、DIO、无 PSRAM | 烧录前与实物型号核对；不影响本次主机逻辑回归 |
| 脉宽与演奏延迟 | 软件 1 ms 扫描、两侧 2 ms 确认；用户选择释放后发声 | 实测模块电平/抖动、无线延迟及曲线手感；不能当作硬件计时捕获 |

证据来自用户当前本地源码及已保存原理图，不代表已核实 A/FPGA 实物烧录版本。本次保留其他模块原样；所有已知兼容边界均可由用户后续修正或确定取舍。

## C07：KiCad 导入表达差异（用户说明不影响实际电路）

本次 IO 审查发现导入的十五个 `Short-Symbol-Auto` 共用 `?` 位号，且传感器局部 IO 标签与排插全局同名标签在 KiCad 原生网表中未合网。按用户补充，这类内容差异不影响实际电路，因此本项是文件取证限制，不能据此判断实物开路，也不要求先修 CAD。原图设计意图的十二路 GPIO 顺序一致；B 的默认极性已按前级拓扑改成遮光高有效。详见 [IO 审查报告](b-board-io-review.md)。
