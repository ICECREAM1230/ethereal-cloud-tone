# FFT资源优化后的数据路径时序修复（2026-11-02）

本任务直接基于主工程 ae350_fpga_menu_lab_20261028 的 PnR132447，350项构建输入在开始时逐项相同；未恢复或批量合入旧8项时序候选/USB-only候选。当前双SDPB spectrum_display.sv SHA94e64a906ed510346a41611f91e6487d726ca9d26ed690ac206f0d8ed1891825保持。原FFT资源优化已将display BSRAM18→3、整机294→279；本次新资源/STA尚待官方实现，不把旧结果当新收益。

## 实际失败路径

| 原当前PnR132447 | WNS(ns) | TNS(ns) / 唯一负端点 | 处理 |
|---|---:|---:|---|
| video ordinary setup | -2.251 | -1200.916 / 1749 | particles分组落差检测18级、motion动态读/算/写长反馈；新target/meta BRAM不在top100样本 |
| SUB ordinary setup | -1.172 | -38.106 / 59 | 主49端点由audio_control dst_data_o[5] live modulation控制，源fanout95、首段route6.089ns；保留原采样边沿，本地复制驱动意图 |
| APB ordinary setup | +0.415 | 0 / 0 | 当前已转正，不合旧APB预写方案 |
| AHB / control setup | +6.108 / +3.075 | 0 | 不扩大修改 |
| ordinary hold | +0.172 | 0 / 0 | 新PnR仍须检查 |

Recovery -13.857ns和removal -0.646ns另列未验收；未新增reset例外或关闭一般STA。旧header setup1854=普通1808+recovery46，header hold19是removal，不能误称普通hold失败。真实报告/网表已完整归档于任务baseline。

## 本次两个生产改动

1. spectrum_particles.sv：输入样本先寄存，完整group事件再寄存，分组fall/armed/pending检测从parent rounding与最大值选择后拆开。valid/bin/height/colour/end对齐；aging、group决策与spawn统一延后2拍，保留原优先级。运动采用读寄存→共享计算/写回两phase，32槽64拍，保留两个共享17bit位置加法器，没有复制32套加法器。内部消隐调度允许变化；真实自然raster下可见每拍RGB/6拍视频/ACK、每帧最终位置/速度/life/颜色、公平分配应与前像相同。
2. control_state_cdc.sv：只给真实dst_data_o寄存器驱动增加literal syn_maxfan=16，依据匹配Gowin SUG550 §5.11；源为普通数据LUT输入，不是clock/FF CE/reset。去掉注释后源token完全等同；没有改变always、时钟、CDC协议或模式生效边沿。此属性的实际复制与负载效果需要新综合网表审计，不能源码认定有效。未改element_renderer/pipeline，也未假设audio_state在sample边界稳定。

## 验证和保全

- particles局部边界：117158cycles/354054checks/80frames，group/aging/spawn原结果精确+2拍、group_max+1拍，frame_start+group-final、无效end和reset；错误end对齐负控cycle91被捕获。
- 独立video测试采用冻结旧particles单独module，与候选不共享实现；保持最终RGB/video/ACK逐拍比较、元表/初始化/1280clock等检查，内部state只允许消隐未完成窗口不同，活动及消隐收尾必须相同。自然full/split48k/disabled结果见 sim/results/timing_fft_paths_video*/。
- 首音联合：sim/results/upstream_audio_boundary_20261102/timing_fft_paths_20261102_joint/result.json，58 checks PASS、input_drift=[]，真实host C/native/CSP/DSP/PCM，不覆盖硬CPU/模拟DAC/实际CDC或STA。
- 控制属性、原版语义等同/实际模块lint及新映射审计见 sim/results/timing_fft_paths_audio*/；映射阶段后更新实际复制结果。

全部前像在 /media/ice/D;/PROJECT_improve/timing_fft_paths_20261102T143823Z/baseline：373文件inputs-and-notes.tar.gz、350项fpga-inputs.json、885文件846995420B完整impl-pnr132447.tar.gz，均逐成员SHA校验。没有创建新worktree或重置Git，主树仍unborn main/既有17项顶层untracked；不stage/commit/push。TB/config/重要PASS/FAIL/负控/图像保留，临时仿真产物仅清理本任务确认可再生项。

## 待验

候选功能回归完成后自动官方综合，+10分钟一次核查：真实exit0/accepted receipt/350当前=run=frozen、原FFT双SDPB/3BSRAM收益保持、audio driver实际复制与selectors覆盖，再按现有队列启动PnR。PnR及独立viewer运行后交还，不自动分析新PnR/STA。后续报告需另获请求。新时序闭合与全部setup/hold、reset checks、板外I/O预算/实际USB/音频/屏幕均不能由本次功能测试代替；本任务没有设备写入。

## 最终功能验收

2026-11-02T23:51:12.541440+09:00 当前新时序修复2/350生产输入已完成并通过独立功能验证，其余348保持，renderer94e64a双SDPB优化不改。particles输入/组选事件两级与64拍共享motion、原位置加法器仅2个；audio真实dst_data_o加syn_maxfan16，原语义token相同且width32/48真实模块lint通过。自然full/split48k/disabled130201791次最终RGB/video/ACK逐拍PASS，双unit1148926拍、group边界354054检查及gravity/end负控均通过，首音58PASS。准备自动官方综合及+10min一次检查；新驱动复制/资源/STA未验。

独立视频最终交付 sim/results/timing_fft_paths_video_candidate_all01/handoff.json；full/split各48真实帧，12张实际RGB PNG新旧逐byte相同；相同参考module完全独立。新构建映射仍须检查syn_maxfan真正生效，未生效不能把本策略当已修复。

## 新综合启动与单次检查

2026-11-02T23:54:43.520567+09:00 当前本轮新官方SYN145112已真实vendor start，build1394747/supervisor1394746/独立viewer1394754，350=current=run=frozen；本轮仅particles/shared流水与control_state_cdc数据driver maxfan两处输入改动，其余348保持，renderer94e64a不改。唯一原生ae350-fft-data-timing-20261102已ACTIVE并核本chat，计划2026-11-03T00:01:13.269000+09:00启动后约10min一次检查；须验实际driver复制<=16/FFT3BRAM映射与官方身份，再队列PnR，viewer启动即交还，不自动查新STA。当前综合未验收，新时序未签。无设备/Git发布。

日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/syn_20261102T145112Z/gowin.log`；任务根continuation.json与frozen-inputs.json为本run身份依据。

## 2026-11-03 统一合并构建

2026-11-03T00:20:25.319089+09:00 本chat已按FM聊天真实用户授权协调合并并保持唯一主写者/构建owner：白名单安装jt51_reg_ch32.v94a706与2新增TB/config，保留CDC d15/particles873/renderer94e64a；安装前后350仅FM1变/其它349不变，原任务合计3变/347保持。旧SYN145112 exit0/官方身份/55216B ITCM/全部selectors通过，actual driver7路最大fanout16的真实LUT+reset归纳补充独立复核PASS，原严格审计FAIL保留；旧整机73597LUT/57098FF/191SSR/279BRAM与917文件impl归档校验。新统一SYN151257已vendor start，build1404747/supervisor1404746/独立viewer1404754，350=current=run=combined freeze；安装后首音58PASS无drift、FM joint246输入中245字节相同+CDC功能token同。原生同一ae350-fft-data-timing-20261102已更新ACTIVE/151257 prompt，并从原生只读状态核真实未来2026-11-03T00:22:58+09:00一次检查；成功验新映射/实际FM SSR减量再按队列PnR，启动即交还。当前新SYN/STA未验收，无设备/Git发布、其它暂停任务不恢复。

新日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/syn_20261102T151257Z/gowin.log`；安装/独占owner/实际未来单次检查的共享回执在 `sim/results/fm32_ssram_20261102T143236Z/coordination/`。

## FMC/RVR地址修复协同（2026-11-03）

2026-11-03T00:33:15.972452+09:00 本chat接到FM聊天新用户授权的FMC/RVR地址冲突修复协同，并已只读核原用户“按照这个修复方案实行。然后合并回主工程综合和布局布线”。旧统一SYN151257真实exit0，但旧地址输入不得继续PnR；原检查ae350-fft-data-timing-20261102已原生删除并核DB无行/TOML不存在。主350输入未改，latest PnR仍旧132447已结束，当前无observed writer/本owner活跃票据；没有终止任何job。root继续唯一主树安装/SYN→PnR owner，FM仅隔离准备地址/两FMC合同/S3版本和同SPI FM+RVR/ESP回归，待明确白名单与全结果再接入；保持RVR0720..0727/AOR07B0..07B7/FM32/20MHz/SSRAM及本轮时序改动。SYN151257完整结果正在保全，证据fmc_rvr_handoff/preservation.json；无设备/stage/commit/push，不恢复旧暂停任务。

2026-11-03T00:34:27.379873+09:00 FMC/RVR协同准备完成：旧单次检查已原生删除、无待执行旧调度/新PnR/本owner活跃票据，主350=current=combined/run完全保持。SYN151257完整impl+run结果保全PASS，911普通文件/1008tar成员/848445638原始B，归档270830972B/SHA575206408a106ea481ef035175b59227daf25bf284090ae1deca3598ab84a3bb，每成员回读SHA与前后文件/350源清单无漂移。唯一root主树安装/构建owner状态和可合入窗口已写FM task/coordination/fmc-rvr-handoff.json；等待FM隔离地址候选/白名单/完整回归，未碰其stage。无生产改动、新EDA、设备或Git发布，不恢复旧暂停任务。

## FMC C0 主树安装与综合（2026-11-03）

2026-11-03T01:05:36.680299+09:00 FMC/RVR修复已按最终c00e3e0d安装13白名单：仅FMC五只读word迁07C0..4，双FMC头同8117c8，RVR0720..27/AOR07B0..B7/写/APB保持。实际376文件前像归档逐SHA，主350=current=stage=frozen=新SYNrun，FPGA本轮3变/347保持；安装后主map checker PASS，S3实际393输入指纹c1b802aa…af34与已构建APP1415648B/ccb10418…37e逐项匹配（ae350-fmc-mapc0-20261103），网页gzip exact。RV32新头重编55216B/四lane/官方VO/32SP64KiB逐byte0diff，仅刷新metadata不regenIP。既有SPI416/141/prepared43107/85/ESP13组+2负控及最终独立复审按同SHA绑定；未重复预构建。新唯一SYN160112已真实vendor start，build1451955/supervisor1451954/独立viewer1451964；原生ae350-fmc-mapc0-20261103已ACTIVE并核本chat/新run/真实未来2026-11-03T01:11:13+09:00单次检查，成功全部验收再队列PnR并attach，启动即交还不查新STA。旧151257/旧check不续PnR；FM32/SSR/CDC/FFT/defaultEP3/5preset46params保持，无设备/stage/commit/push、不恢复旧暂停任务。

新日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/syn_20261102T160112Z/gowin.log`；协议详情在 `docs/FMC_RVR_SPI_MAP_20261103.md`，共享安装/源绑定/真实未来检查回执在 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/sim/results/fmc_status_relocation_20261103T003159/coordination`。
