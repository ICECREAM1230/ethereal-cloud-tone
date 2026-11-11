# AE350时序流水修复（2026-10-29）

保持F800菜单/F801控制寄存器ABI2、既有引脚、32 SUB/16 KS/8 FM及所有PCM时钟/采样率。C固件及官方AE350/PLL生成物未变。基线由provenance/timing-pipeline-latest.json定位，包含实际未跟踪源码、测试、原网表和位流。

## 实际流水边界

| 边界 | 行为 | 完成条件 |
|---|---|---|
| APB前端 | CPU请求锁存→内部SETUP→内部ACCESS→注册RESP；零等待slave产生2个外部wait | 下游只执行一次，注册响应后才CPU PREADY；PSLVERR/PRDATA一致 |
| SUB命令 | 索引generation/state寄存读→合法性/代际结果寄存→执行 | 普通指令在执行边沿ACK，COMMIT等原40word安全写/标量发布后ACK |
| 诊断观测 | 外部完成时捕获captured请求+注册响应全tuple，下一PCLK供原monitor消费 | 等待/SETUP无事件，error与data对齐，不重计或虚假display ACK |

APB前端只在CPU PRESET的本域同步复位清除，响应FF不受control域复位组合读回影响。control owner仍立即协调撤销会话，但control_link_apb与control_available共同使用PCLK三FF释放。失效control请求注册报错，不下送/恢复重放。菜单与其原有同步SETUP读行为保留。

原lab_debug_monitor和lab_apb源文件、旧menu工程保持不变。新observer仅因powerup reset清历史，CPU-only PRESET不能抹掉已经完成但尚在观测流水中的事件。

SUB上游完成CDC保持整个payload及VALID到最终ACK，无新160bit整包副本；快照32bit目标代际/2bit状态与2bit判定，在新的窄寄存器处分拍，宽values写使能不再依赖实时比较长链。安全写窗口320..699、起点<660、原WAIT/HOT/tail/generation/反馈语义保留。协调reset取消未完成判定；连续VALID仅在前一ACK后可更换payload。

## SDC与接受范围

只增加AHB→AHB、APB→AHB、control→AHB三个setup专项报告。时钟声明、最大延迟预算、所有既有例外和普通数据/复位/MPW检查保持。用户接受PR1014中sys_clk_d、ESP_SPI_SCLK_d普通布线警告，不能推广成对实际负slack的接受或false path。

## 证据与边界

实际双slave前端1143checks；observer+原monitor49checks；SUB179checks和原参数bank默认模式；真实C经新前端、原control APB/完成CDC与三原DSP的16组970602checks均PASS。三路PCM均非零，原native stale/late idle/抢占/热模板/flush覆盖保留。结构247启用文件、31引脚、原29664B固件身份PASS。全部302项生产输入在最终C联合回归前后及父级收口复查一致。详细记录见sim/results/timing_pipeline_20261029/delivery.json。

上述为功能/结构证据；不包含硬核CPU指令或SDF仿真、物理USB/SPI与新板测。当前正式资源与STA必须以重综合/新PnR判断，旧PLL→fault hold、Recovery/Removal继续列为未接受检查，不先行扩大例外。
