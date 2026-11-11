# AE350第二轮数据时序优化

记录时间：2026-10-30T00:18:45+09:00。工程保持D盘独立副本，基线`provenance/timing_round2_baseline_20261029T145804Z`保留402文件、81,462,311字节及302项生产输入。对比基线为`pnr_20261029T052735Z`，PnR资源Logic62288、FF43944、BSRAM276、DSP73、PLL5。

目标是对实际违规数据路径增加寄存边界、缩短控制扇出。普通布线PR1014中的sys_clk_d和ESP_SPI_SCLK_d按用户接受；时钟、SDC预算/例外、31端口引脚、32/16/8声部、采样率、统一FPGA PCM请求调度和29664B AE350固件保留。CPU继续管理MIDI/声部并发出prepared ABI2指令，FPGA继续实时PCM计算。

| 路径 | 基线setup | 修改及语义 |
|---|---:|---|
| APB地址→RX payload CE | -1.157ns | 保留32x74 payload和独立snapshot，改head/tail/live环形队列。payload只随真实RAW接收写tail；capture/POP/flush/清扫不搬payload，full不借同拍POP提前ready。 |
| SUB running/slot→runtime read | -0.936ns | 原768slot、256有效lane按固定次序预解码下一voice的一热地址，保留原bank/S1采样边沿。需覆盖owner单独复位而drum继续运行的首拍资格化。 |
| SUB slot→values CE | -0.638ns | 安全copy/start/scalar窗口先注册；标量one-hot发布提前一拍，完整context仍40个原bank写，最后实际写与标量发布完成后才ACK。 |
| SUB tag→PCM held_response CE | -1.738ns | 64bit比较拆成8个byte equality寄存，与112bit完整packet/valid/branch owner同拍，下一拍归约。增加1个SUB clock延迟，输入间隔仍1拍，统一frame时间线不变。 |
| PLL状态→MIDI accept_pulse D | -10.299ns全局 | USB core业务busy/packetCDC reset和owner均使用control_clk三级本地释放状态；USB物理pad即时释放及USB域原三拍释放不变。 |
| USB PLL状态→link_fault CE | -0.213ns hold | 用已有ae_control_link_reset_n本域状态观察失锁，保留owner会话sticky及FM fault优先级；短失锁可被资格化级保持到下一个control边沿。 |

验证使用真实RTL/Host/PHY/queue/packetCDC、原参数bank和三DSP；PLL单元测试的时钟/LOCK模型及生产块隔离均单独注明，不等同真实PLL/硬CPU/物理USB或STA证明。每项单元和最终冻结C/三DSP回归的源SHA、失败/通过日志、清理manifest保存在`sim/results/timing_round2_20261029`。最终资源/时序结论只能取新的官方PnR同阶段结果。

用户板测报告USB MIDI灯红、可演奏、无法切换引擎。静态发现control_main没有读取F8000054，而诊断LED需要显示ACK推进；该问题先保留记录。引擎切换实际原因尚未确认。按用户随后要求，本轮不改C/main/LED/按键/切换算法，也不把时序失败推断成这两个现象的已证实原因。新位流未经本轮硬件写入授权不自动烧录。
