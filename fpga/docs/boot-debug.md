# AE350 启动与串口诊断（2026-10-28）

AA13复位恢复更新：原主工程把AA13约束为低有效`sys_rst_n`，独立实验曾将其改作第三个引擎键。当前候选恢复AA13→GSR，保留AB13/Y12引擎切换与N12重新配置。AE350 POR/HW仍由包含外部复位、启动计数完成和core_lock的powerup_ready驱动，各域同步释放链保留。此接法尚须本轮仿真/综合/PnR/实板分别验收，不把GSRI接通等同于Recovery/Removal闭合。

当前实板的旧100MHz菜单位流停在 `FPGA VIDEO READY / WAIT AE350`。
该画面证明HDMI输出正常、显示发布尚未完成；不能区分未取指、栈访问失败、ID/ABI错误或计时/发布卡住。

## 官方板卡设置核对

本实验器件PG484对应Tang Mega 138K非Pro。Sipeed官网把拨码检查列在外部Flash固化章节：
https://wiki.sipeed.com/hardware/zh/tang/tang-mega-138k/mega-138k.html

与主库保存的官方31005底板原理图第7、8页对照：

| SW2组 | 电路用途 |
|---|---|
| 1/2/3 | FPGA MODE0/MODE1/MODE2配置模式 |
| 4 | 用户输入DIP_Switch_0，接FPGA AB12 |
| 5 | 板载BL616 ModeSel |
| 6 | 板载BL616 EN |

没有发现独立的AE350启动拨码。BL616模式表中EN=1、MODE=1是JTAG调试模式；EN=1、MODE=0是游戏模式。SW3是BL616固件更新按键。本轮不需要改变开关。
表中的Master CPU/Slave CPU是FPGA配置总线模式，不是AE350启动选择。

高云MUG1031-1.3.1 §8.2.2说明ITCM初始化由itcm0..3文件提供。
本实验采用Emb_TCM，程序随位流初始化；未使用外部CPU Flash或DDR3启动。
30354 SOM的CPU_RESET按钮接F4普通FPGA IO，当前顶层没有使用该输入；AE350 POR/HW reset由powerup_ready驱动。

## 时序修复候选

旧STA有实际APB/ITCM数据违例：APB最坏slack -6.100ns，ITCM -4.431ns。
报告的两个“hold violated”计数实际上来自Removal检查，不能混称普通数据hold。
候选经官方PLL生成器将DTCM/AHB/APB从100MHz调整到50MHz；AHB/APB保持1:1同相。
CORE800MHz、RTC10MHz、视频PLL不变；BUS_HZ、防抖和4ms ACK常数同步修改。
新PnR仍须核对setup、hold、BSRAM脉宽、Removal，不能仅凭降频声明闭合。

## 串口与被动观察

复用主库原样`midi_diag_tx.sv`和`MIDI_DEBUG_TX U15`约束，诊断版设为115200、8N1。
31005原理图中U15接FPGA_UART.TX→BL616_IO27_RX；V14为FPGA_UART.RX，本轮只需读取输出。
诊断在50MHz板载参考时钟域工作，不依赖AE350执行UART驱动。
APB旁观模块不驱动PREADY/PRDATA/PSLVERR；CPU PRESET低时保留观察记录。

每秒一行，共105字节：

```text
AE35 S=xxxxxxxx B=xxxxxxxx N=xxxxxxxx A=xxxxxxxx W=xxxxxxxx R=xxxxxxxx C=xxxxxxxx P=xxxxxxxx V=xxxxxxxx\r\n
```

| 字段 | 含义 |
|---|---|
| S | bit0 core_lock、bit1 video_lock、bit2 powerup_ready、bit3 CPU PRESETN；bit4见过APB完成、bit5见过写、bit6见过PSLVERR、bit7见过publish写、bit8 APB域快照已响应、bit9完整trap已收齐 |
| B | 最近启动标记，普通scratch测试数据不覆盖它 |
| N | APB完成事务数，含错误事务 |
| A | 最后一次原始32位PADDR，不假定硬核高位含义 |
| W/R | 最近写数据/最近读返回值，各自保持 |
| C/P/V | trap的mcause/mepc/mtval |

跨时钟传输使用保持型mailbox与req/ack握手，UART整行锁定快照。
即使PSLVERR为1也观察总线请求，标记出现不等于该store成功，须同时检查S的error位和异常记录。

| B值 | 阶段 |
|---|---|
| AE350001 | reset入口，CSR和栈初始化之前首次APB写 |
| AE350002 | SP/mtvec初始化完成 |
| AE350003 | 即将进入main |
| AE350010 | main入口 |
| AE350011 | ID/ABI读取通过 |
| AE3500E1 | ID或ABI不匹配，随后停机 |
| AE350020/AE350021 | 首次publish门铃写之前/之后 |

trap先写scratch header DEAD3500，再按顺序写三个完整32位异常寄存器，每笔fence，不依赖栈。
这些写仍依赖APB，若总线本身不能完成，不能保证得到trap记录。

## 本机读取

当前无ttyUSB节点，但已成功打开现有FT2CH通道B，通过安装自带D2XX读取，无需改驱动绑定或安装新驱动。

```bash
python3 scripts/read_debug_uart.py --seconds 10 --output logs/uart_<新名字>
```

只读取，不发送UART命令，不操作DTR/RTS，不复位调试器；保留raw bytes、文本及字段解析JSON。
旧位流读取3秒为0字节，接口调用成功；旧位流没有这个诊断发送器，不能把无数据推断为CPU未运行。
新的诊断位流须在生成并SRAM加载后再采集。

## 800MHz核心是否需要降低（2026-10-28补充）

从官网下载的V1.3 SDK压缩包MD5再次核对为5bff79193c69424cdbe55eb197b3135d，与官网一致。已将官方Emb_TCM和Tang Mega 138K Pro参考工程关键配置解压到docs/reference/clock_official_v13，manifest记录原归档路径和SHA。完整ZIP扫描含52个IPC、27份PLL配置、15个SDC，其中15套核心PLL实际CLKOUT1全部800MHz，没有发现400/200MHz核心运行例程；其他PLL的400/200及未启用输出默认值不算核心配置。

官方Emb_TCM为CORE800、DTCM/AHB/APB100；Tang Mega Pro参考工程为CORE800、DTCM/AHB/APB50。MUG1031表4-1给的是最高800MHz，未要求必须800；仍必须保留PLL_R[0]/CLKOUT1直连。

400MHz隔离候选已由本机官方PLL生成器产生：VCO800、ODIV1=2，输出50/400/50/50/10MHz，各相位0。wrapper与当前800MHz版相同，不是用fabric触发器分频。文件在vendor/ip_generation/core_pll_400_probe；probe400.json记录实际HDL分频核验。它未加入选中的gprj，未综合/PnR/上板，不冒称官方例程已在400MHz运行。

当前2556B菜单固件读取FPGA APB毫秒计数，不使用CPUFREQ/mcycle/PLMT/RTC，降低CORE不会直接改变100ms门槛、4ms虚拟ACK、防抖或UART/HDMI时基；执行与轮询延迟会改变。若后续采用官方完整BSP，必须核对CPUFREQ=800*MHz及delay.c等依赖，不能只改PLL。

最新800核心/50总线版PnR已成功，普通数据setup最差+1.927ns、hold+0.247ns、最小脉宽+1.292ns；剩余Recovery5（最坏-0.887ns）与Removal1（-0.058ns），涉及视频/serializer复位。核心仍800时总线数据路径已通过，不能认定800核心就是原WAIT根因；也不能把fabric STA当成硬核内部时序验证。全时序尚未闭合。

14:09:59 JST尝试按既有SRAM调试授权加载上述800/50诊断位流，Programmer在设备操作前拒绝：Verified SIPEED debugger is disconnected; connect its USB-DEBUG port。退出1，未发生下载。日志logs/program_sram_20261028T050959Z；本次未采集新版UART。400MHz候选仍是隔离产物。后续需连接调试USB并确认供电，再对照运行阶段。

## WS2812 AE350启动指示（2026-10-28补充）

复用主工程原样midi_status_rgb.sv，MIDI_STATUS_LED=J16，LVCMOS33、DRIVE8、BANK_VCCIO3.3。驱动仍输出GRB，一颗像素，低亮度；50MHz下bit62周期，0/1高20/40周期，latch至少300us。

| 颜色 | 判定 |
|---|---|
| 蓝色明暗变化 | 板载参考时钟/灯逻辑工作，等待AE350执行标记 |
| 黄色明暗变化 | 已观察到AE350启动代码标记，尚未满足持续运行条件 |
| 绿色常亮 | boot阶段AE350021、CORE/POR/PRESET/视频就绪，至少两次不同非零成功显示ACK推进，无APB/trap/ID错误，且仍在保活期限内 |
| 红色明暗变化 | ID/ABI不匹配、PSLVERR、完整trap，或启动3秒/已收到ACK后停更1秒超时，或启动后CPU就绪丢失 |

“成功显示ACK”来自CPU完成且无PSLVERR的APB读0x54；固定非零值不会重复计数，普通APB轮询不计保活。停更锁存后须两次新推进恢复绿。lamp只证明菜单程序和显示应答保活，不是所有合成器控制/音源功能的总验收。

新增观察字段通过原mailbox一并跨域，payload由288扩为320bit，原UART九字段保持不变；没有反馈主总线。灯的复位来自board powerup_count，独立于core_lock，因此AE350 PLL未锁时仍可亮蓝/红。

sim/results/boot_led/green02完成4253项波形检查、88帧、28项状态检查，默认超时在TB中缩短，真实50MHz WS2812时序不缩放。monitor回归在sim/results/debug_monitor/boot_led_monitor_green；RED行为日志保留。主库不改，固件与800/50时钟不改。

## 当前实板下载与UART结果（2026-10-28）

14:31:45 JST WS2812位流SRAM加载成功：100%、Finished、exit0，UserCode E5C5匹配、Status26230。核心800、DTCM/AHB/APB50，Flash未动，未读回比对。

U15诊断经D2XX通道B实际采集4200字节/40条完整行（10秒读取含之前USB积存）。B=AE350021、S=000001BF；APB事务计数461048→18797963，稳定采样中的最后写值为菜单发布序号9→399，无PSLVERR/完整trap标志。已证明CPU执行到菜单并持续运行；不代表已目视看到绿灯/菜单或验证三个按键。

当前彩灯版STA普通data TNS0，5条Recovery最差-1.208ns、1条Removal -0.054ns仍未通过。日志和运行核验保留在logs/program_sram_20261028T053124Z、logs/uart_after_program_20261028T053124Z。当前UART reader无需修改，已单独验证真实CRLF、截断和坏hex识别。
