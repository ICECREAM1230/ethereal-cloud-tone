后续状态：普通FM默认电钢琴和ESP版本配套已另行完成，最新构建及交付见 `FM32_DEFAULT_EPIANO_20261102.md`；本文件保留下方FM32首次合并的历史验证身份。

# FM32 合入 AE350 主工程（2026-11-02）

用户已确认独立FM32候选验收完成，并要求合入主仓库、保留既有改动。本轮已把已验收的32voice×4op、20MHz、共享pipeline II1合入 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028`。用户验收针对独立候选；此主树新组合尚未烧录或实听；用户保存关闭GUI后已进入真实Gowin综合，新的综合/PnR/STA仍待验收。

## 合并与保留

通过真实8voice祖先、当前主树、已验FM32三方比较，独立暂存验证后仅安装106项明确文件；5,686项保留文件全量SHA无变。原主树unborn/main、全部未跟踪，没有以空Git diff当未改动证明。安装前保存5,362源文件、额外manifest/fixture、361个测试源，及上轮已结束PnR的828个impl文件；所有被覆盖文件另有精确前像。保全根 `/media/ice/D;/PROJECT_improve/fm32_main_merge_20261102T191542`，安装清单与独立审查在 `sim/results/fm32_main_merge_20261102/`。

主树现有FMC13word参数/预置、手机HTTP/BLE接口和独立theremin是合并权威，不恢复已被取代的B402/0727 preset-only接口。仍保留五种预置（电钢琴ID3、钟声ID4）和next_note快照，当前持键/sustain/释放尾音不重编音色。FPGA/AE350/ESP32保持原单一路径职责；旧非AE350 local allocator/native96仍8voice，其vendor profile保持，当前FMC32独立snapshot/descriptor/HTTP/Web一致为32×4；不能机械把旧legacy核改成32。

保留FDC source3私有持续电钢琴、CC/普通模板隔离、真实心跳及静音保护；修正FDC owner仅支持voice<8的门槛，支持新高声部。executor sensor_owned/base_mute/generation/feedback扩32，20MHz看门狗10,000,000周期保持0.5s。BEGIN sensor/heartbeat/silent与GLOBAL bank位按opcode分别解释。保留最新SUB per-partial cutoff key tracking/RELEASE顺序、显示10kHz/FFT/颜色粒子、CH569、实际引脚/CST和其他普通时序约束。GPRJ原262条逐attrib保留，新增12条；SDC只把FM生成时钟8/25改2/5，其他内容原样。

## 验证与产物

- 实际C→APB/CDC→FM32/JT51与原GW5A模型联合 **17,156,054 checks / 4096连续帧 / 最坏131 FM clocks**，deadline及bubble错误0；32voice/4bank/33rd steal/旧Off-Idle/高31 sustain/真实idle/5FMC next_note/Panic/reset通过。bank绑0与II4两个实际负控精准失败，生产源码不改。
- C的FDC0/1 ASan/UBSan、FMC、SUB partial、RELEASE、原owner、32声部/169ACK初始化及高31FDC共14组通过；新增高31owner7,580checks/164jobs/1,044packets。主树与扩容版181行普通MIDI事件/packet逐byte一致。原owner硬8和VMcap8负控均精准失败。
- 实际RTL writer1,453、executor194、APB67,909检查通过；core静态8算法×8feedback oracle及动态EG/wrap诊断保持；FDC高voice/bank/真heartbeat/超时/抢占/旧handle/零gain恢复42项和owned8负控通过。24bit counter/32owned/5voice全状态SAT及假heartbeat负控通过。
- ESP32/Web539/539、真实Chrome7场景、8组AE350 host（Web/BLE各33On+33Off逐byte/source）、15组theremin、实际HTTP和共享手机API/BLE回归通过。既有六个FDC1 fixture与当前FDC2 UI不同导致的基线失败已保留，仅适配测试标签，生产FDC不改。
- ESP-IDF6.1真实S3构建，`ae350-main-fm32-20261102` APP **1,415,408B**，factory剩120,592B，SHA256 `4aa178fe60db190c1f99886bd1c6dfd9557bd2f5b169c0cec9c80829468f8bab`；HTML213,524B，实际gzip61,487B，dist=源=build解压逐byte一致；各目标stack门槛通过。产物 `sim/results/fm32_main_merge_20261102/esp32-review/idf-build01/`。
- RV32/FDC1新固件 **55,240B**，SHA256 `4d2deafead0c34853d302ffc3ea11ce3f78b6cefaad72ea77f56a13e8d1a809d`；官方AE350重新生成后32SP/64KiB逐byte0diff，非ITCM VO tokens及配置与主树保持，V/VO原样安装。没有复制旧候选54,236B镜像覆盖主树新C。DTCM BSS25,008B，BSS至stack top40,272B，原4KiB预留保持。
- 顶层结构lint通过，FDC0/1 FM真实层级lint无severe。首次顶层检查缺主树已启用FFT的结构声明，已根据原厂公开wrapper端口增加仅供lint的空声明（与既有CPU/PLL声明同一边界），生产protected FFT没有改动；不宣称FFT功能仿真。以同一声明跑原主树对照，既有LATCH8/UNOPT1等告警逐identity完全相同。
- 独立规格/质量复审无未解决P1/P2。唯一P2为20MHz官方PLL IPC漏进安装白名单，已补入第106项并复核；正常验证receipt输入0缺失/0漂移。

重要功能结果分别保存在主树 `sim/results/fm32_verification_20261102/`、`fm32_control_20261102/`、`fm32_fdc_guard_20261102/`、`fm32_main_merge_core_20261102/`、`fm32_implementation_20261102/`；综合性审查、C/ESP交付、生成ROM与保留矩阵在 `sim/results/fm32_main_merge_20261102/`。只清理本轮确认无用的260个仿真生成文件共342,964,209B，测试、配置、日志、输入/负控归档保留；代理自有清理另见各receipt。未删除既有或不确定内容。

## 正式构建

首次wrapper尝试 `syn_20261102T110240Z` 在RTL前真实exit2，原因是已打开主工程GUI1115046，未产生新综合网表。日志/退出和阻塞前像已保留，没有强制关闭窗口。用户随后明确“已保存并关闭”，核GUI进程已退出且349输入/106安装/5686保留全部SHA未变。

现唯一真实SYN **syn_20261102T111233Z** 于20:12:33.557JST启动，已看到 **GowinSynthesis start**。build **1149369** / supervisor1149368 / 独立viewer **1149376**；当前=本run=冻结349项一致。后台不依赖viewer存活，关闭viewer不会停止综合。本启动轮未读取新exit、网表结果或STA。

日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/syn_20261102T111233Z/gowin.log`；原生唯一 **ae350-fm32-main-merge-syn** 已ACTIVE，已核当前chat/完整prompt/新run。唯一未来检查为 **2026-11-02T20:22:33.855000+09:00**（启动约+600.1秒），旧失败run检查已撤销。成功核真实exit0/finish/无ERROR、349及新55240B官方ITCM/20MHz/SDC选择器后经全机共享队列启动PnR+独立viewer，记录真实PID/log/attach即结束，不自动读取新PnR结果/STA。WAITING如实排队，不绕过或终止其它job。

旧SUB/MIDI/FDC暂停不恢复；本轮没有stage/commit/push、设备访问或编程。物理板仍是用户此前验收的独立FM32配套，主树新组合及外部Android APK没有实板/听音验收。既有reset Recovery/Removal、外部IO/STA覆盖与动态EG环绕≤1帧/两处1LSB差异边界保持。
