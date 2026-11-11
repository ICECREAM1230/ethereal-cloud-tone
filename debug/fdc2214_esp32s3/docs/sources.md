# 文档、API和寄存器依据

核对日期：2026-10-29。

1. [TI FDC2x1x 数据手册 SNOSCZ5B，2024-10 Rev B](https://www.ti.com/lit/ds/symlink/fdc2214.pdf)。复核本地保存的同版PDF：四通道RR_SEQUENCE见Table7-3和MUX_CONFIG字段表；RCOUNT>8、SETTLECOUNT>3、单端FIN_SEL=2、fIN<fREF/4见Table7-1。该表对多通道模式内部时钟允许fREF≤55MHz、外部时钟要求fREF≤40MHz；本工程内部默认÷2并统一保守限制fREF≤40MHz属于工程配置策略，不是TI对内部源的硬性40MHz限制。配置掩码按寄存器功能位，不依赖保留位读回。应用示例对RR_SEQUENCE=b10的三通道描述与字段表不一致，本工程按字段表b10=CH0/1/2/3设置0xC20D。
2. [TI FDC2214 EVM SNOU139](https://www.ti.com/lit/ug/snou139/snou139.pdf)。用于理解LC起振、驱动电流、接地与探头负载；不把其天线参数当作用户模块实测值。
3. Context7于本日检索 `/espressif/esp-idf`：结果有v5.x及master，另有release-v6.0索引，**没有明确v6.1版本覆盖**。只用其官方I²C和USB console接口模式作为检索线索，未将其结果冒充v6.1文档。
4. 实际匹配的本机ESP-IDF `/home/ice/esp/esp-idf-v6.1`，`idf.py --version` 输出 `ESP-IDF v6.1`。逐项核对 `components/esp_driver_i2c/include/driver/i2c_master.h` 的bus/device配置、7位地址、`i2c_master_transmit_receive`、`i2c_master_transmit` 及毫秒超时；没有-1无限等待。使用同步API，I²C只由采集任务操作。
5. 同版 `components/console/esp_console.h` / `components/esp_stdio/stdio_port.c`：使用 `esp_console_new_repl_stdio` 和 `esp_console_start_repl`，默认USB Serial/JTAG，UART0替代配置。`components/esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h` 的 `usb_serial_jtag_write_bytes` 明确支持tick超时，实际 `src/usb_serial_jtag.c` 先等待发送互斥锁，再等待环形缓冲区，两处各使用传入的20ms tick超时；不是整个调用严格20ms截止。应用用 `ftrylockfile` 避免等待stdio锁，部分写/锁忙即丢记录计数。ESP-IDF内置控制台提示符本身由SDK管理。

## 四通道配置表

| 地址 | 内容 | 默认值/读回掩码 |
|---|---|---|
| 0x1A | 睡眠配置 | 外部0x3601或内部0x3401 / 0xEAC0 |
| 0x08..0x0B | CH0..3 RCOUNT | 0x1000 / 0xFFFF |
| 0x10..0x13 | CH0..3 SETTLECOUNT | 0x0100 / 0xFFFF |
| 0x14..0x17 | CH0..3 时钟 | FIN_SEL=2；外部分频示例1得0x2001，内部分频2得0x2002 / 0x33FF |
| 0x1E..0x21 | CH0..3 IDRIVE | 各自15<<11=0x7800 / 0xF800；须按实际LC调试 |
| 0x19 | ERROR_CONFIG | 0x3821 / 0x3821 |
| 0x1B | MUX_CONFIG | 0xC20D / 0xE007；自动扫描CH0..3，10MHz deglitch |
| 0x1A | 开始转换 | 外部0x1601或内部0x1401 / 0xEAC0 |

总计20次寄存器写，每项读回核对。先读Manufacturer/Device ID，后配置，再按STATUS bit6就绪读取DATA 0x00..0x07。STATUS诊断bit11..9及DATA MSB bit13..12与数值分开保存。

全电流启动，HIGH_CURRENT_DRV保持0（多通道不可使用CH0高电流模式）。FDC2214不用FDC211x OFFSET/OUTPUT_GAIN。内部参考35..55MHz，÷2后17.5..27.5MHz，原始码比例和外部40MHz模式不同，不混用两种时钟下测得的端点。四通道扫描周期取决于测量/稳定时间和参考频率，100kHz下9个16位寄存器读理论约4.05ms，另有转换、调度、同步、错误恢复时间，不能把理论上限当作实测采样率。
