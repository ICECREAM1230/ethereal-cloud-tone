# FM32 默认电钢琴与 ESP32 配套（2026-11-02）

> 最新状态（2026-11-02T21:51:38.358730+09:00）：用户要求先停止以进行代码优化。本轮PnR115326已主动终止，真实exit143，原进程树已退出，本队列ticket已RELEASED。日志与已有输出保留；本chat后续自动构建暂停，需用户明确恢复。以下PnR已启动记录仅保留为历史，不表示完整实现已通过。

> 最新状态（2026-11-02T20:54:38.143023+09:00）：本轮SYN113735验收通过，真实exit0于20:44:12JST完成。350项冻结、配套55216B官方ITCM及独立身份审查通过。PnR **pnr_20261102T115326Z** 于20:53:26JST启动：build **1200092**、supervisor1200089、独立viewer **1200099**；队列ticket `10d5a6b25bcf4ee3a2a8859e6896f266` 在有效RESERVED后启动并立即attach RUNNING。日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/pnr_20261102T115326Z/gowin.log`。唯一SYN检查已原生删除并核，next=null。本轮停止于启动交还，未读取新PnR结果/退出/日志/STA。下文“当前正式构建”保留启动阶段历史，以上述最新状态及continuation为准。


已按用户要求保留主仓库现有多音色切换，并将普通 FM 的冷启动默认音色改为 catalog1 / preset3 `electric_piano_tine`。当前最新主树包含上轮FM32合并与本轮默认音色修改；本轮在新stage验证、独立复审后仅安装41项明确文件，1226项相关保留文件SHA不变。没有覆盖FDC/SUB/模式路由/32核/20MHz/显示/CH569等原改动。

## 实际默认与切换行为

- `firmware/ae350/app/control_data/fm_boot_default.h`由新`sim/config/export_fm_boot_default.mjs`从当前FM目录生成，`--check`可验证。算法4、反馈0、keys15，完整24个operator bytes等于正常电钢琴，保留自然D2R与衰减。
- `default_fm()`、`cf_init()`与32声部/4bank启动寄存器均使用实际新默认；首次FM NoteOn不必先发音色请求。启动仍169ACK、全KeyOff/mute，不自动NoteOn，也不改变原引擎选择。
- 共享两份`fm_config_v1.h`定义`FMC_DEFAULT_PRESET_ID=3`且逐byte相同，FMC13/CRC/wire ABI没有改变。ESP32只在CPU accepted和completed都为0时建立46项默认电钢琴owner缓存；重启ESP遇到已有或在途CPU配置保持未知，不猜默认、不发自动apply。
- UI只从owner已确认的46参数匹配目录，首次可信时DOM预选电钢琴；该匹配不是硬件寄存器读回或完整patch身份。用户手选保持，五音色0..4显式应用及参数编辑仍走原FMC next_note，held/sustain/tail保留各自快照。网页刷新/重连不会自动覆盖CPU音色。
- FDC source3继续原私有持续电钢琴变体（D2R0/两carrier SL1），与普通自然衰减默认区分；原FDC头/owner/app、VM/SUB/32voice RTL/模式/普通时序及CST均保持。

## 验证和交付

实际C FDC0/1的13组ASan/UBSan至少101302checks通过；默认EP专项7593/7630checks，覆盖完整24bytes、boot32/keyoff169、未经FMC首ON、5音色/custom、旧held/sustain/tail/reset及FDC高31。使用真实旧C默认的负控在任何FMC前精准失败。新完整C/APB/CDC/FM32/JT51联合17156054checks/4096帧/最坏131clocks通过，deadline/bubble错误0。旧bank/II4负控199项RTL身份完全一致，复核沿用，不冒称本次新C又重跑负控。

Web549/549、8组实际owner（包括5音色46值/CRC/无提前完成、两种ESP重启未知cache边界）、HTTP/inline6、theremin15和真实Chrome12全部通过。Chrome验证冷启动EP默认预选不发请求、五音色逐项一次显式应用/同一session、手选不被poll覆盖、参数编辑及未知cache重连；后端是明确loopback/BFM，不是实板音频证明。失败的缺fixture、共享header漂移和测试fixture期望诊断都保留，未以改产品屏蔽测试。

ESP-IDF6.1最终构建`idf-default02`，版本 **ae350-fm32-epiano-20261102**，APP **1415648B**，factory余 **120352B**；SHA256 **ce739c225a7231cf4d776c218c39093c7644092cbf68ebe0dcb374d3b0a57811**。404项最终源指纹466a087f3e632ef0583f788064f0b9a4b48b7e424f86188b193af0ba36ca8a55；HTML214194B/实际gzip61709B，source=dist=真实build gzip解码逐byte相同。产物 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/sim/results/fm32_default_epiano_20261102T201850/esp32/idf-default02`；79d0...第一次构建仅是中间版本，不能当最终镜像。

真实RV32/FDC1 **55216B**，SHA256 **ceab95e51a3186ffea0654aa4b570af92e4c1e13d4dc975ac528002a1d92f211**，官方AE350重新生成后32SP/64KiB逐byte0diff；非ITCM VO tokens、接口/配置保持，protected V与VO原样安装。DTCM BSS25008B、至栈顶40272B、原4KiB预留保持。

独立审查`audit/final.json` PASS，无未解决P1/P2；41项清单SHA7a041139d5c188ac977e354e0b0fa586c28165434554b06a2a33c43b3a04b6b7。安装前1261源、覆盖前像、旧SYN111233完成后的838impl已归档；本轮重要测试/日志/源码/负控/archive均在 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/sim/results/fm32_default_epiano_20261102T201850`。测试临时ELF/objects、joint scratch/alias、S3成功cache和临时依赖链接由各代理仅清理自有项，详`firmware-tests/result.json`与`esp32/delivery.json`；无既有/不确定内容删除。

## 当前正式构建

旧主树SYN111233真实exit0，但使用旧默认，已保留未送PnR；其旧检查已删除。唯一新SYN **syn_20261102T113735Z** 于20:37:35.372JST启动，已经确认 **GowinSynthesis start**。build **1194611** / supervisor1194610 / 独立viewer **1194618**；350=current=stage=run=frozen一致。日志 `/media/ice/D;/PROJECT_improve/ae350_fpga_menu_lab_20261028/logs/control_build/syn_20261102T113735Z/gowin.log`；冻结 `frozen-inputs.json`。

原生唯一 **ae350-fm32-default-epiano-syn** ACTIVE，已核当前chat目标和完整prompt。唯一未来检查 **2026-11-02T20:47:35.472000+09:00**，约启动+10分钟；确认真实exit0/finish/无ERROR、350及新55216B官方ITCM后经全机队列PnR。只有本ticket RESERVED且300秒lease有效才启动，立即attach真实PID/run。PnR+viewer启动后报告并结束，不自动读取新结果或STA。

本轮没有stage/commit/push或设备访问/编程，Git仍unborn main/整体未跟踪；四份.ai笔记更新。新ESP和新主树位流须作为这一套使用，当前实板仍先前验收的独立FM32组合，尚未烧录本轮默认EP配套。旧reset/外部IO/STA覆盖及动态EG量化记录继续保留，不把功能测试、host C或旧实现结果当新SYN/PnR/实板验收。
