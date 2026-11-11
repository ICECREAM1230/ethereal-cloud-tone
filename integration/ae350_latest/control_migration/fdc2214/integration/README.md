# FDC AE350 统一集成候选

本目录是当前活动工程的待合并补丁，不是另一套产品工程。生产单写者为“查找 GW5AST138K 嵌入式处理器 (2)”任务；本任务只新增模块、独立候选、验证及专属笔记。禁止从本目录整目录覆盖正在构建的源码。无设备写入授权。

## 已确定的硬软边界

- FPGA：50MHz系统时钟驱动FDC两路I2C配置/读回/轮询；保留外部40MHz转换参考；原始完整读数、AW/WD/STATUS、传输错误、重启epoch、一致快照和有界保护。新组合只启用一个FDC master，不与旧sensor_i2c_top并联驱动同一SCL/SDA。
- AE350：三点中值、回差、1€低通、校准/上下限、连续pitch14与gain17、静音和斜率；调用既有唯一VM、compiler、prepared/APB/CDC/native链。
- FPGA：实时DDS/滤波/CSP/ADSR/LFO/PCM保持原职责；新增17bit线性增益只对真实sensor-owned当前voice生效，不影响全局MIDI。混响仍开启，启动wet0并保留运行调节。

## 源码清单和生效边界

新常规模块在`../rtl/`，算法/驱动在`../firmware/`。`candidate/`只包含按真实源SHA复制并修改的共享文件：

- VM/compiler五份C和头文件：窄source3 claim/update/release/flush API。
- SUB native backend、element_pipeline、subtractive_engine、synthesizer_manager四份RTL：增益及watchdog参数通路。
- control_service.c/h：M_OWNERS阶段先清理source3，再执行原mask7 MIDI flush；真正KILL完成后才能route。
- top/synth_system接线候选由专属agent交付；原生产文件没有改动。新增5引脚必须经实际接线/ball-pin确认，不能解注释音频冲突旧CST。

各来源SHA与最小patch在`sim/results/fdc_ae350_20261030/{native_c,native_rtl,native_service,top_integration}`。主owner须以这些前像和当前源作三方合并，尤其与SUB_PATCH1独立GLOBAL320bit配置扩展合并；不要丢掉较新的modeRPC/headless/74bit ingress时序修复。合并后旧测试和报告不能直接作为新输入PASS。

## 总线和原生命令

主架构批准F8010100..F801017F，byte offset0x100..0x17F。现有00..BC及候选SUB CAP C0不占用。接在现有lab_apb_pipeline注册请求/响应之后作本地子译码，control_link_apb与FDC不得同时ACCESS；不用第二套CPU或APB框架。详细字段见`../interface-v1.md`与mailbox/integration.md。

邮箱CTRL只有SYS接收和liveness诊断，不是正常DSP参数第二写者；其ACK不等于native执行或PCM输出。驱动publish回调对实际声部更新必须等native最后ACK被现control_service消费后才能返回1；例外是已经确认没有FDC声部且请求inactive的幂等零输出，它可直接完成而不发送native命令，统计时必须与实际native应用次数区分。BUSY返回0保持不可变sending，明确取消返回-2、不提交CTRL、不计applied，其他失败-1为本地FDC故障，不清全局fatal。

VM原RAW MIDI来源仍0..2，channels=48。source3只经sensor API进入唯一manager，总SUB声部仍32，不固定扣除、不新增allocator。`command.pitch = pitch14 | (gain_q16 << 14)`，kind8=VM_SENSOR，只在C逻辑接口使用；wire仍原160bit prepared。

- pitch14绝对1024/八度：4864→MIDI57(约220Hz)、5888→MIDI69(约440Hz)、6912→MIDI81(约880Hz)。若换旧MIDI Q16单位则乘768，但候选直接使用`pitch14-cf_note_pitch(69)`作为native signed18偏移，不经过旧MIDI弯音裁限。音高0及16383也已列为边界测试。
- PC_PARAMS保持原低140bit；新增[156:140]=gain17，[157]=sensor_owned，顶端[191:158]保留0。MIDI这18bit为0；featureoff必须拒绝任何新增位，on时检查gain<=65536及非owned不得携非零gain。
- PC_BEGIN flags bit2=FDC_LIVENESS，低2bit仍NEW/HOT/SCALARS。只有fresh sensor ON/update设置；模板HOT/SCALARS刷新保留pitch/gain/owned但不设置续命位，OFF/KILL/MIDI也不续命。liveness=1且最终owned=0的COMMIT拒绝。旧generation/revision保持原STALE保护。
- 静止但新序号的有效FDC帧也产生新更新，避免合并同值后误超时；旧缓存/重复seq不能刷新算法年龄。采集年龄与CPU读取时长计入保守sample_time，不能把刚读取旧缓存的时间当新测量。

## 音频与资源边界

PARAMS的gain/owned与pitch在同一真实COMMIT安全窗口整组发布。native per-voice values增加18bit×32=576个存储位，这是RTL字段变化，不是官方FF/LUT资源数字。native-stage增加fresh标记及音频域watchdog，只在实际sensor publication续命；模板不续命。

S10 renderer输出为signed18，复用stereo_expression_gain(SAMPLE_W=18,GAIN_FRAC_BITS=16)。结构时序优化后，先将同一个S10元素的左右样本、规范化增益与valid捕获一拍，再进入原两拍乘法/舍入模块；featureon普通MIDI也通过相同三拍，featureoff仍直通。state_store仍按原S10写回，只有mix流增加三拍，吞吐保持每拍一对左右样本。增益按同一个S10 index查询稳定的native参数；真实安全发布slot>=320，而全部有效mix样本必须在slot300前排空。新增断言检查sample_tick、slot512交换以及320..699 native发布窗口均无mix有效。MIDI NEW在其新generation实际COMMIT时清sensor-owned，因此旧watchdog不能抑制新MIDI身份。

`fdc_ae350_voice_guard.sv`是独立身份/缩放验证端点，不是最终candidate中再次实例化的第二个控制器；不要在已有native增益路径外重复缩放。

ENABLE关闭应生成裁剪FDC master、mailbox CDC和增益硬件；默认端口集31pins，启用需专用引脚宏和实际CST。C的ENABLE_AE350_FDC必须与RTL参数一致。新增浮点软件采用本工程原RV32IM/ilp32和真实RV32软浮点libgcc，局部freestanding math/string shim不改SDK。target链接与主机模型不能证明硬核WCET。

## 合并后的验证和构建

1. 保存实际脏/未跟踪基线、确认当前stage已结束且共享源窗口已交接。
2. 按源码SHA三方合并native C、RTL、service及top，加入新RTL实际GPRJ；生成匹配C开关并重建实际固件和4个ITCM byte lanes。保留默认runtime演奏关闭，校准应用会从静音清历史。现`manage_control_build.inputs()`只收原firmware及control_migration/firmware/include，不能漏掉新`control_migration/fdc2214/firmware`和broker、构建/开关配置；合并时将真实编译源及局部shim纳入输入快照/GUI同步凭据。记录实际RV32 libgcc身份，不能只hash新的control.bin而漏其生产源。
3. 跑featureoff/on、真实I2C→APB、驱动+邮箱、真实C/APB/CDC/三DSP/native PCM、整合broker和算法链、目标链接与结构/引脚检查。
4. 新CDC必须按真实综合网表核对查询非空/宽度/组合锥，payload用实际max-delay预算，只有req/ack首meta D可作狭窄例外。不能全域falsepath。新增存储/DSP/时序须新官方实现，不沿用旧STA。
5. 由唯一vendor owner自动综合，独立日志/10分钟单次检查，输入不变再PnR并按原工作流结束；硬件下载仍需新授权。当前FDC候选未官方综合/PnR、未板测或最终天线校准。
