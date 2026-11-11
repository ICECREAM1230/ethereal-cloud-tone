# AE350 与 FPGA 菜单交互验证

2026-10-28。独立验证工程；主仓库只作为实际工作区基线和 HDMI/引脚参考。

当前混合控制工程的实体按键（AA13复位恢复候选）：

| FPGA球脚 | 当前角色 |
|---|---|
| AA13 / `sys_rst_n` | 低有效整机逻辑复位，直接驱动GSR；不参与菜单/引擎切换 |
| AB13 / `keys_n[0]` | 下一合成引擎，仍对应原APB key1计数 |
| Y12 / `keys_n[1]` | 上一合成引擎，仍对应原APB key2计数 |

原APB key0计数位置保留且输入恒为未按下，固件ABI不变。N12仍为独立RECONFIG_N按键，不改其重新配置用途。AA13松开后等待65535个50MHz参考时钟边沿（约1.3107ms）及PLL lock，然后沿原各时钟域同步释放；GSR只由外部AA13驱动，不反馈启动计数器/PLL状态。按住AA13保持复位，重复按下重新开始计数。

下文保留2026-10-28菜单/视频实验的历史接口说明；当前headless MIDI控制工程的按键用途以上表为准。

AE350 的 C 程序维护三个菜单参数：GAIN 0..100、CUTOFF 20..20000、MODE 0..2。
三个底板按键分别选择下一项、增加、减少。FPGA 只同步、去抖和计数按键，不解释菜单。
CPU 生成 40x20 ASCII 文本，写入 FPGA 后台字符 RAM，在帧边界发布；FPGA 完成字符取模、720p 时序和 TMDS。
FPGA 虚拟设备在收到命令 4ms 后产生 ACK，检查参数范围、更新 applied 值，并提供虚拟采样递增值。
不接入真实音源、MIDI 或传感器。

## 总线 ABI

使用官方 SDK 的 Extended APB Slave 地址窗口 `0xF8000000`。以下是字节偏移；均为32位访问。

| 偏移 | 方向 | 说明 |
|---|---|---|
| 0x00 | R | ID=0x4D454E55 |
| 0x04 | R | ABI=0x00010000 |
| 0x08 | R | FPGA 毫秒计数 |
| 0x0C | R | bit0=显示发布忙；bit1=虚拟设备忙 |
| 0x10/14/18 | R | NEXT/PLUS/MINUS 去抖后按下次数，32位自然回绕 |
| 0x1C | RW | scratch，支持字节写使能 |
| 0x20 | R | rotl32(scratch,7) XOR 0xA535138C |
| 0x24 | W | 虚拟命令 sequence，非0、全字写；忙时拒绝 |
| 0x28 | R | 虚拟设备最后完成的 ACK sequence |
| 0x2C | R | 虚拟设备结果：0成功、1参数非法 |
| 0x30/34/38 | RW | shadow GAIN/CUTOFF/MODE |
| 0x3C/40/44 | R | applied GAIN/CUTOFF/MODE |
| 0x48 | R | 虚拟采样值，每毫秒递增 |
| 0x50 | W | 显示发布 sequence，非0、全字写；忙时拒绝 |
| 0x54 | R | 最近显示完成的 sequence |
| 0x1000..0x1C7C | W | 后台字符 RAM，800个字，每字低8位是一个ASCII字符 |

APB setup 周期读取并锁存寄存器/字符数据，access 周期完成；写入只在 PSEL && PENABLE && PREADY 成立时发生。
非法地址、非法sequence、忙时命令/字符写入用 PSLVERR 拒绝。不会静默覆盖未完成事务。
虚拟命令在提交时锁存所有 shadow；执行期间修改 shadow 不改变当前命令。
非法参数完成后仍返回相同 ACK sequence，result=1，旧 applied 参数保持。

## 显示与 CDC

双字符bank的CPU所有权与像素所有权轮换。发布后CPU不得再写后台bank，直到像素端在垂直消隐接纳并返回ACK。
请求/应答使用四阶段电平握手（REQ↑→ACK↑→REQ↓→ACK↓），各经两级同步；像素域独立复位不能把复位值当作ACK。请求携带的bank及sequence保持到应答，返回低电平前不释放后台bank。payload按稳定载荷约束，不将整组跨域总线全局false-path。
上电没有CPU发布时，HDMI显示固定的 WAIT AE350；不存在替代CPU运行菜单的硬件状态机。
CPU先验证scratch变换、虚拟设备ACK及applied回读，再显示LINK PASS。CPU心跳由C程序递增。

## 程序与验收

采用官方 Emb_TCM 路线：程序在 `0x80000000` 的64KB ITCM运行，数据/栈放在 `0x00000000` 的64KB DTCM。
无DDR3；程序bin拆分为itcm0..3并嵌入位流，首次实验只需一份包含程序的`.fs`。
交叉编译使用标准RV32IM_Zicsr，禁用缓存和中断，不依赖Andes专用库。后续扩大工程可改用RDS BSP。
尚未上板前，不宣称硬核CPU、HDMI电气或实时时序已验证。仿真总线驱动不替代真实AE350。
用户负责本轮上板验证；创建/构建工程不授权写设备。
