# AE350 prepared ABI2（2026-10-29）

## 目标与固定边界
AE350唯一解释MIDI/管理32 SUB、16 KS、8 FM声部，使用真实ESP模板/本地boot模板编译核心原生参数。FPGA不解释MIDI或执行note/bend/gain/template数值编译，保留实时PCM/DSP、参数存储和有界执行器。主仓库只读，D独立工程；既有球脚/USB Host/采样率/音源数不变，视频关闭。未经本轮验证不得声明新源时序或面积已合格。

## 命令线（160bit不加宽）
W0[7:0]engine、[15:8]voice、[23:16]opcode、[31:24]flags；W1/W2/W3为96bit engine-specific载荷；W4为32bit occupancy generation。seq仍走原独立32bit完成CDC。与ABI1不同：wire上不再携source/channel/note/raw bend。逻辑C voice_command保持原语义，control compiler将其展开成prepared包。

通用op：RELEASE=1、KILL=4、BEGIN=0x10、DATA=0x11、PARAMS=0x12、COMMIT=0x13、GLOBAL=0x15。
结果：0 accepted（或指令实际持久完成）、1 stale无副作用、2 invalid无副作用；背压不回假ACK。中央只做类型/engine路由，不存第二份generation；native保留真实generation和一个32bit单调config revision防止晚到fragment/COMMIT。所有fragment W1=revision；BEGIN要求revision非0且大于该engine上一已开始revision，不允许回绕复用。owner coordinated reset清两端状态。data/commit的voice、generation、revision须与当前stage一致；OFF/KILL能取消stage。全局配置不借用某声部的generation。

### SUB
BEGIN flags[1:0]：0=NEW（gen>当前；进入WAIT静音），1=HOT context更新（gen相等，不重新触发gate），2=SCALARS_ONLY（gen相等，包括release表达）。bit3=PARTIAL_CUTOFF，仅NEW/HOT可置1（完整flags8/9）；bit2仍为既有FDC liveness，不能被后台刷新伪造。W1=revision，W2/W3=0。全局只有一笔stage，不建32份双bank。
DATA flags[2:0]=element0..7、bit3=chunk0/1，其余0；W2/W3合64bit，chunk0=context[63:0]，bit3清零的BEGIN仍接收chunk1=context[89:64]（高38bit必须0）；bit3置1则chunk1=context[103:64]（高24bit必须0），新增[103:90]为partial独立static cutoff14。16片均带revision。低90bit格式与旧context_data[89:0]完全相同：音高/左右增益/滤波类型/dual/共振/duty/wave/enable等已由CPU编译；不携MIDI note/offset。
PARAMS flags=chunk0/1/2，其余0；W2/W3合64bit，拼接最终参数向量低位在前：bend signed18 [17:0]，cutoff_bend signed20 [37:18]，cutoff14 [51:38]，amp_depth18 [69:52]，mod_depth18 [87:70]，keytrack8 [95:88]，wheel8 [103:96]，touch_cutoff_delta signed18 [121:104]，touch_resonance_delta signed18 [139:122]，其余高52bit0。此处为事件级参数换算；动态CSP/LFO、振荡器、滤波状态、逐样本乘法和最终音频饱和仍FPGA。
COMMIT W1=revision，其他0；NEW/HOT要求16DATA+3PARAMS完整，SCALARS_ONLY只要3PARAMS。复用原element_param_bank（AE_NATIVE_WRITE=1），没有第二套context RAM；只有单笔8x104暂存。安全slot窗口320..699内，以原word0..4格式写入8元素共40个word，开始必须slot<660，slot670才到的提交要等下帧。实际40次写完才发布标量并ACK，不能让renderer看到部分更新。NEW保留原两control pass WAIT，HOT不重触发，RELEASE保留真实tail。非AE默认参数保留旧SCLK写/slot512 bank切换。未写context不能以X参与PCM。
GLOBAL flags0，W1低16=wheel_vibrato_depth(s16)，W1高16/W2/W3=0；影响实时LFO深度，CPU在模板切换时更新，其他静态touch已变成逐voice cooked delta。

### 2026-11-02：静态 partial key tracking 修复

完整上下文的 PARTIAL_CUTOFF=8 扩展使用既有第二个DATA包的保留位，不增加包数、40次RAM写或改变CSP ISA。C以 signed note_pitch + partial tune/unison 算 `clamp14(5632 + trunc((pitch - 5120) * K / 64))`，振荡器音高另行clamp14；不将pitch LFO隐式送进cutoff。每个partial cutoff写进原生parameter RAM。NEW/HOT同时发布该voice的格式位；SCALARS不改变它。新格式运行时使用RAM内各partial cutoff，后级HDL继续加入实时modulation、bend与touch。旧格式运行时仍使用PARAMS.cutoff，包括成功的SCALARS修改，保留旧ABI行为。

新固件发送扩展格式，必须与本次RTL和官方重新生成ITCM成套构建；旧硬件不会接收新flag。PC_LINK_ABI/APB地址/SPI模板格式、generation/revision、FDC sensor字段均保持。旧90bit发送方在新RTL仍可使用共享cutoff，但只有新发送方提供partial独立静态跟踪。RELEASE保留其占用的旧context，模板刷新规则保持。

### KS
BEGIN flags为原KS核心command_op（0..7），W1=revision，其他0；EXCITE新generation，其他命令匹配generation，原init/BIND常量握手可保留。
DATA flags=chunk0/1，W1=revision，W2/W3合64bit。127bit向量：length15 [14:0]、tuning signed16 [30:15]、loss16 [46:31]、amplitude16 [62:47]、gain_left16 [78:63]、gain_right16 [94:79]、seed32 [126:95]，bit127=0。
COMMIT W1=revision，其他0；直接向原KS核心发已编译整数command，保持sample-boundary applied_sequence/真实idle，不把入队视作执行完成。无音符/velocity/pan查表或/127缩放。

### FM
BEGIN flags0=NEW generation、1=同generation参数更新；W1=revision、W2/W3=0。NEW暂mute指定voice，不清其他声部；HOT不改变原音色快照（快照在C）。
DATA flags=count1..4，W1=revision，W2/W3按低16在前装4个{data8,address8}原生JT51寄存器写（地址低8、data高8）。仅保持writer/busy/done和目标voice generation保护，不再做KC/KF、TL、pan或operator地址编译。每packet实际写完再ACK。COMMIT W1=revision，其他0，NEW完成原key-on/所需warmup边界后解除mute；RELEASE发该声部原生KeyOff，KILL立即mute并KeyOff，保留真实四operator envelope idle。
GLOBAL用于初始化/全局寄存器：flags[1:0]为0=reset/begin、1=write batch、2=finish/warmup；write时flags[4:2]为count1..6（W1/W2/W3按低16在前装六个原生pair），非write时count=0，其余flags位0。GLOBAL的voice/generation均0，不占用某声部generation。GLOBAL初始化在CPU服务BOOT phase完成，ready语义区分物理sink与初始化完成，禁止CPU等ready/FPGA等CPU的死锁。不保留AE模式旧高层MIDI/patch编译器作为动态fallback。

## ESP模板送CPU（保留既有SPI framing）
SPI writes经过原CPU模式白名单（SUB 8000..8041/80ff、8100..81ff；KS b000..b0ff/b200..b33f/b3fe/b3ff；FM b400）进入有界64x48异步FIFO，记录{address16,data32}。不在FPGA复制SUB/KS模板或执行模板编译。FIFO写SCLK、读PCLK，完整reset与gray指针同步；源overflow必须粘住可见，不能静默丢。S3每burst按保守free credits限流，不改变原引脚/Mode0/10MHz。
APB 0x90 TEMPLATE_META(bit31 valid，low16 addr)，0x94 DATA，0x98 POP(write1)，0x9c STATUS(low16读域延迟free观察值，仅诊断、不能授权S3写量；bit16 overflow)。head在POP前稳定，元数据/数据读不能消耗记录。CPU只在实际POP后前进parser。
APB 0xa0..0xbc八个CPU模板状态word（PCLK寄存器），整体通过256bit held snapshot跨SCLK给原SPI状态读路；先写result/CRC等字段，再发布generation/completed。index0 SUB_COMMIT→0707，1 KS_COMMIT→0708，2 KST成功generation→055c，3 KST_COMPLETED→055d，4 KST_RESULT→055e，5 KST镜像CRC→055b（不是模型CRC 055a），6 FM_SENT仅本地计数不被S3当作applied，7保留。SPI 0x071c=写域权威保守template free[15:0]/overflow[16]（仅此credit授权S3写量），0x0719 ABI改2，CPU1 magic保持。原RAW MID1与flush隔离契约不改。
CPU从真实模板流维护staging/active，收到COMMIT后进行有界prepared刷新，按原语义发布真实ACK。每次轮询最多取4条模板记录，RAW调度保持独立。SUB range改变不重算已锁存bend；HELD/WAIT更新context/base/K，RELEASE保留旧context/base/K，但touch/global深度更新仍影响tail。S3 FM当前B400仅level/pan，不伪造完整FM preset上传。KST新模型原本关闭，提交明确返回UNSUPPORTED=2/completed token，不提升成功generation。

## CPU与验证
现有midi_parser/voice_manager/control_runtime维持逻辑语义和有界队列。新control compiler将逻辑命令展开为原生packet，只有最终COMMIT/真实执行ACK后consume逻辑队首；物理seq与逻辑seq分别管理，任何未完成payload稳定，模板刷新/flush/source丢失不会重放旧NOTE。
纯C差分覆盖signed /96、/768、base /64(trunc0)、bend/touch >>>6(floor)及边界，固定整数模板产物对照；新C→APB→native→实际DSP/PCM回归覆盖3来源、32/16/8槽、模板热更新、尾音、抢占/取消、背压、reset/clock loss、late feedback。约束不全域屏蔽，新增按APB/control/SUB域专项report_timing。只有完整集成验证后父级启动一次官方综合+独立viewer，10分钟唯一检查，通过且输入不变启动PnR后结束，不烧录、不stage/commit。

最终联合验证final01通过12组129586检查，SUB/KS/FM实际核心均产生非零PCM；1350个物理完成ACK与147个逻辑ACK有序对应。唯一native STALE是故意注入的旧代OFF，正常路径此前无错误；旧代idle经真实CDC/APB返回后被C丢弃。SPI位级链另通过38616检查，覆盖真实Mode0 framer、模板FIFO、Gowin RAM、APB和状态held CDC。硬核AE350指令执行、实板USB/S3/音频组合和官方资源/STA由后续阶段验证。

现有减法CSP/ADSR/LFO/滤波与所有音频采样计算仍在FPGA。此ABI复用原参数word存储/音频执行接口，没有新增通用CSP IMEM/DMEM上传协议，也不开放原先已被AE白名单阻止的通用SPI指令写。需要扩展时应另行定义所有权和完成语义。
