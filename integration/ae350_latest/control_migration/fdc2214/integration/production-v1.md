# FDC2214 在 AE350 混合工程的生产接入 v1

本页描述 `ae350_control_lab.gprj` 的实际接入，优先于同目录旧候选中默认关闭、物理脚未定及宏控制端口的说明。FPGA/AE350 与 ESP 候选分别验证，禁止把编译完成当板测或 STA 闭合。

## 物理接线与编译开关

用户于 2026-10-30 确认 H4：1/SCL=P19、2/SDA=T21、3/ADDR=P16、4/INT_N=R18、5/SD=V17、6/GND。模块 I²C 已外部 3.3V 上拉；40MHz 参考来自模块自带振荡器。W21 不用作 FPGA 时钟输出。当前这五脚在 Bank4，与当前音频/USB/ESP 引脚不冲突，原 FM 诊断灯未启用。

顶层 `ENABLE_SENSOR_I2C=1` 且 `ENABLE_AE350_CONTROL=1` 使 `ENABLE_AE350_FDC=1`；CPU 用 `scripts/build_control_firmware.py --enable-fdc 1` 构建。显式关闭时顶层设 `ENABLE_SENSOR_I2C=0` 并将 CPU 构建改为 `--enable-fdc 0`，必须配对重新生成固件/ITCM和位流。生产两种配置都保留已约束的36个pad bits；关闭会释放SCL/SDA、保持SD高并裁剪采集/邮箱CDC/增益硬件，不再使用旧候选31pad约定。

## 硬软边界与启动

FPGA 单个50MHz/100kHz I²C master 配置并轮询FDC两路数据。INT引脚保留，当前通过STATUS轮询完成采样，不依赖INT事件。原始数据、质量和源时间经冻结快照到AE350。FPGA不运行旧theremin mapper。

AE350按现有单owner协作循环处理标定数据：音高通道将原码在far/near之间直接线性映射到pitch_min/pitch_max，使用整数运算与端点钳位，不做自适应滤波或音高滑动限速。pitch14定义为整组音色的基准音高；各振荡器保留模板中的相对失谐、八度与明确启用的调制。音量通道保留原有中值/回差/1€低通和gain17；通过唯一VM/compiler/prepared/APB/native链驱动现有32个SUB声部之一。没有第二套allocator，不固定扣除声部。KS16/FM8、原采样率和实时PCM职责不变。模板刷新不能给sensor watchdog续命；切换或显式停用经真实KILL/身份已替换证明排空，不能以邮箱CTRL_ACK代替native应用。

`cs_poll`、原实体键处理优先，之后每轮FDC application只做一次有界poll，driver最多2个FDC MMIO。时间来自F800毫秒计数，扩展到64位微秒但仍为1ms分辨率，不使用CPU cycles冒充微秒。独立状态发布位于cs_poll，每变化或100ms最多追加1次APB写，与driver预算分开。

传感器演奏默认关闭。历史实验默认校准不代表当前天线已标定；用户需显式提交配置再启用。离开SUB、配置采用、重启或身份替换后保持关闭，不自动重发音。

## CPU 配置记录

通过原SPI模板FIFO交CPU，未送第二写者。数据为32bit：

| 地址 | 内容 |
|---|---|
| B500 | 0=请求停止，1=显式启用；启用要求稳定SUB且真实硬件probe可用 |
| B501 | 1=停止/排空后restart，完成保持关闭 |
| B510/B511 | pitch far/near原码 |
| B512/B513 | volume far/near原码 |
| B514/B515 | pitch/volume noise sigma的IEEE754 float32位型 |
| B51E | 1=完整6字段验证后，停止/排空再原子采用，完成保持关闭 |

NaN/Inf/负sigma、非法span/原码、缺字段或busy请求只记本地错误，不改变有效配置或使MIDI全局fatal。普通MIDI来源flush保持原mask范围；显式网页全局STOP应额外优先排B500=0并撤销未发送enable/apply，不能把MIDI stop_completed当FDC native停止receipt。

## 只读状态合同（ESP首版）

CPU软件status寄存器位于F8010170（FDC子窗口+0x70），通过独立32bit held CDC发布到SPI071F。不得占用原ACK0..7，其中ACK6是FM_SENT、ACK7是mode。这个新增接口需真实组合验证及新综合网表检查后才算已实现。

高16位=FDC1；低8位：0 available(init+真实ID/ABI/CAP probe)、1稳定SUB eligible、2requested_enable、3driver_enabled、4应用停止/配置/重启busy直到排空、5driver或采集fault、6local_error、7user_config_active(用户配置已采用，不是实板校准合格证明)。bits15:8为状态revision，每状态变化或100ms成功发布后递增，仅供新鲜度/活性观察，不是请求完成序号。

ESP最小入口首版只提供启停、重启、完整六字段手填配置；无raw读数或自动采样标定。HTTP返回queued/transport-sent，与观察状态分开，不声称本请求已采用或native已ACK。原网页/ESP固件未更换之前不能使用新入口；本轮全部离线，不刷设备。

## 验证边界

实际RV32构建/四ITCM字节核对、生产明文顶层接口展开、真实I²C→APB，以及真实host C/算法/驱动/service/VM→APB/CDC→原三DSP/PCM分层验证分别记录。Protected AE350只做接口替代展开，不能证明CPU启动；数字I²C模型不能证明模拟天线/上拉边沿；host模拟不证明硬核WCET。新的SDC集合和STA必须绑定本轮新实际综合网表/布局布线。物理校准、FDC声控与网页交互上板结果待用户恢复供电并另行授权后确认。

## 2026-10-31 cam2dvi IIC 移植

用户确认保留 H4 的 P19/SCL、T21/SDA，ADDR/SD/INT 及其它引脚保持。两条 IIC IO 改为参考工程的 LVCMOS33/PULL_MODE=UP/DRIVE=8；仍需既有外部3.3V上拉。实际 byte/bit controller 与参考目录逐字一致，pad 使用相同 `~padoen ? pad_o : 1'bz` 接法，只有拉低/释放，不主动驱高。

参考 top.v/SDC 的输入为50MHz：100000拍（2ms）后将FDC SD拉低，3000000拍（60ms）后释放IIC master复位；reset/restart均重新执行，计数饱和不回绕。保留参考 clk_div_cnt=1000（实际SCL周期由参考位状态机/输入滤波决定，不沿用旧核的精确频率估算）；这不改变音频/CPU/PLL时钟。FDC 0x2A地址、制造商/设备ID、12个配置及读回、双通道帧/软件接口保持。摄像头8bit数据sequencer改为FDC 8bit寄存器指针/16bit大端数据，所有写字节均检查ACK，读高字节ACK、低字节NACK，保留有界故障/恢复。

本改动的数字协议/延迟仿真与后续官方构建结果记录在 `sim/results/fdc_cam_iic_20261031`；源码替换不能证明实板初始化故障已消失，且不自动烧录。网页available仅代表FPGA MMIO控制器能力，不能单独证明外部FDC芯片ID/采样成功。
