# Gowin GW5AST / AE350 联合开发

本参考用于这两个技能的 Gowin 适配；核对日期：2026-10-28。它对下述通用 ASIC/Arm 示例作平台限定。先遵循当前用户任务与项目 AGENTS.md，按本次改动选择阶段和验收项。

## 工程与工具分工

- `soc-integration`：硬软分工、寄存器/地址空间、总线角色、CDC、复位、IRQ/DMA 和集成验证。
- `embedded-firmware`：复用实际 AE350 SDK/BSP，编写 MMIO 驱动、事件调度、超时和中断处理。
- 现有 `gowin-project-navigation`、`gowin-ip-generation`、`gowin-arora-v-rtl`、`fpga`：核验 `.gprj` 输入/器件、生成官方 IP、综合/PnR、资源和时序。读取这些已安装技能后使用其流程。`.gprj` 是官方工程清单，不能用通用 `.f`、Yosys/nextpnr 流程替换。
- IP 资格检查改为器件/修订支持、版本、端口、配置、许可和模型覆盖。LEF/GDS、ASIC 工艺角、UPF、scan/tie-cell 等仅在实际适用时检查。以官方报告的 CLS/LUT/FF/BSRAM/DSP 与各时钟域 STA 为准，不强制 ASIC 面积或上游默认利用率。

## AE350 与 FPGA 的接口方向

名称描述外接逻辑的角色，不能仅凭 Slave/Master 字样判断 CPU 的角色。

| AE350 扩展端口 | 数据宽度 | 发起端 | 响应端 | 用途 |
|---|---:|---|---|---|
| Extended APB Slave / `APB_*` | 32 位 | AE350 | FPGA 外设 | 控制/状态寄存器、命令门铃 |
| Extended AHB Slave / `EXTS_*` | 32 位 | AE350 | FPGA 外设 | 参数 RAM、较多批量读写 |
| Extended AHB Master / `EXTM_*` | 64 位 | FPGA 主设备 | AE350 | FPGA 访问 AE350 的 ILM/DLM |

- 本机 Gowin V1.9.12.04 中 `RiscV_AE350_SOC` 为 1.2；实际工程生成的 wrapper/config 才是接线依据。安装目录示例：`${GOWIN_IDE}/ipcore/RiscV_AE350_SOC/`。
- 本机 1.2 wrapper 的 APB 带 `PSTRB[3:0]`、`PPROT[2:0]`、`PREADY`、`PSLVERR`，不能把只实现 APB3 基本信号的模板当成完整适配。明确字节写/非对齐访问/保护属性的支持范围，并检查等待与错误响应。
- AHB 按实际 `HTRANS/HSIZE/HBURST/HPROT/HREADY/HRESP` 端口建契约；地址阶段与数据阶段要正确关联，等待期间保持应保持的状态。`EXTS_HREADYIN` 是进入 AE350 wrapper 的响应信号，不能仅凭命名接反方向。
- APB 错误走 `PSLVERR`，AHB 错误走 `HRESP`；`DECERR` 和 AXI checker 只适用于实际 AXI 接口。测试实际协议的读写、复位、等待、背靠背传输、字节选择与副作用只发生一次。
- 核心、AHB、APB、音频是不同的时钟域问题；先查实际 PLL/SDC 和时钟关系，选择合适 CDC。不能把核心频率当作总线频率，也不能对多位参数只逐位双触发同步。
- `EXTM_*` 可访问本地存储不等于任意地址/DDR 自动共享或硬件缓存一致。共享存储需要按实际 SDK/内存属性约定所有权、可见性、缓存维护和屏障；IRQ/DMA 只在实际启用并核验连线后使用。

## AE350 固件限定

- 从该工程对应 SDK/BSP 的启动文件、链接脚本、ISA/ABI、工具链参数和中断实现出发。AE350 为 RISC-V，不能照搬 Arm 的 SVC/PendSV、DMB/DSB、SWD 或 `arm-none-eabi-*`。
- 优先复用已验证的启动/PLIC/RTOS 移植。仅增加总线外设驱动，不要求重写 BSP、引入 RTOS或覆盖所有外设。
- MMIO 的 `volatile` 不代表缓存一致或跨设备原子性；使用平台的屏障/临界区约定。W1C、读清零、FIFO 等寄存器不能套普通读改写。
- 本机具有某个 GCC/OpenOCD/QEMU 可执行文件不证明支持 AE350；确认 Gowin/Andes SDK 版本、架构、调试配置与模型。缺少工具或受保护 CPU 模型时，报告未执行项；局部总线 BFM 通过不代表 CPU 启动/整机验证通过。

## 任务范围与记录

只实施当前请求涉及的阶段；通用全芯片/DMA/低功耗/24 小时验收项按实际范围标记适用性。咨询不要求新建流水线状态文件；实施时优先复用项目 `.ai/` 记录。技能安装或生成构建产物不授权烧录、调试器内存写入或破坏性 RAM 测试。

## 来源与版本边界

- [Gowin MUG1031E 硬件手册](https://cdn.gowinsemi.com.cn/MUG1031E.pdf)，当前查得 MUG1031-1.3.1E，2026-01-23；第 5、6 章及 8.2.13–8.2.15 节。该版示例器件/工具版本不完全等于本机工程。
- 本地 1.2 `data/riscv_ae350_top.v` 132–185 行及 `riscv.ipspec`，验证上述方向/宽度和 APB 附加端口。未来换版本需重新核验。
- 手册中的 SDK 示例：`example/APB_Slave_Bridge`、`example/AHB_Slave_Bridge`、`example/AHB_Master`。名称来自手册，不代表 SDK 已安装或示例已通过当前板验证。
