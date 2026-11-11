# BRAM资源裁剪与综合前开关（2026-11-03）

主工程ae350_control_lab.gprj；顶层rtl/control_lab_top.sv。当前生产参数：

| 参数 | 默认 | 行为 |
|---|---:|---|
| ENABLE_CH569_PCM_CAPTURE | 0 | 裁掉PC PCM回传FIFO/HSPI；本地DAC、USB-MIDI及FFT共用采样脉冲保留。HTCLK/HTREQ/HTVLD为0，HD高阻 |
| ENABLE_MIDI_STATUS_LED | 0 | 裁掉USB-MIDI状态灯，枚举和输入保留 |
| ENABLE_CH569_UART_DIAG | 0 | 原可选诊断UART保持关闭 |
| ENABLE_HDMI / ENABLE_FFT | 1 / 1 | 示波器与FFT显示保留 |

已有scope、reverb、三音源及传感器功能保留。synth_system本身的capture默认1用于兼容其它顶层；本主工程显式传0。改参数后应重新综合，不能用旧网表继续PnR。

SDC由现有sync_control_uart_sdc.py在综合冻结前纯文本选择：capture只管理精确5条HSPI时钟/FIFO CDC预算；UART管理legacy3条，PCM诊断4条要求UART&&capture。其它SDC字节保持，未扩大例外/关闭一般STA。CST未改。SPI地址守卫仅在独立复审后刷新两个顶层源指纹和启用文件集合，地址/掩码/写权限/保留区与checker算法保持，真实FMC/RVR旧址重叠和未知BASE负控继续FAIL。

## 存储优化与验证

- Scope ring：64bit内部接口保持，将每声道[23:13]共22位紧凑存储。官方新IP8192x22、READ0/SYNC。隔离SYN实测16→10 BSRAM、LUT66→54、FF2不变。原sample_y的24bit全取值×4gain形式证明、错误删14bit负控、官方RAM68,192检查及独立scope44,175,404core/44,430,942pixel、30,412,800可见pixel、16增益/时基组合和3reset逐拍对照PASS。
- Reverb：保留32bit反馈和32768拍scrub，只屏蔽生成存储29184以上的无效SCRUB写。12tap正常上限29079。官方RAM模块SYN58→53逻辑BSRAM、LUT442→371；其中一个32bitprimitive预测拆半后物理54。24,400输出/42,173,122检查、12tap>=5回卷、14,000非零尾音、忙中clear/reset及高16位丢失负控PASS。
- Capture：真实官方FIFO/EXM/reset/CDC与独立旧片段cap/UART四模式，701,283引脚/344,658pulse/1,348snapshot/2完整block PASS，shared-delay破坏负控FAIL；六类FIFOstream场景PASS。不是全SoC/实板仿真。

旧IP源码保留，主gprj只停用旧两RAM条目并启用新三source。当前整机预期264逻辑BSRAM；按本器件已验证宽度拆分规则预计305物理（旧279逻辑/319物理），物理省14是预测，需新实际实现确认。完整FFT display仍为target/meta物理3+glyph2=5，不把逻辑3称整个renderer物理3。资源下降不等于修复334unrouted。

## 证据与构建

任务根sim/results/bram_resource_prune_20261102T173352Z/：baseline保全401源/notes及55实际impl文件逐SHA归档；三候选stage与完整正负控、独立复审、安装白名单、351冻结输入、validation和coordination均保留。仅清确认无用的本任务scratch，旧失败日志与重要结果不删。主SYN及+10min唯一原生检查的实时身份见continuation.json；全门槛通过后经共享队列PnR，PnR及独立viewer启动即交还，不自动分析新STA。无设备访问/编程、stage/commit/push。
