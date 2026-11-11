# FLU1 控制回归（2026-10-26）

`bash tests/host/run_flute.sh` 运行真实 SPI owner、RAW mailbox、template ACK、voice owner 和 HTTP handler 的 CPU-only 测试，不打开设备。`SANITIZE=1` 为引擎/owner 加 UndefinedBehaviorSanitizer。

- 保留模式 0减法、1 KS、2 FM；3加法编号保留，本轮未恢复其远程控制；新增固件扩展 `ENGINE_MODE_FLUTE=4`。共享 generated FM contract 及其 hash 不变。
- `GET /api/v2/capabilities` 的 `engines` 增加 id4，available 只由 FLU1 capability/version/preset 读回确定。旧 FPGA 的 FLU1 全0安全报告不可用。
- 沿用 API v2 session 和 `X-FPGA-Control: 1`。引擎选择 command 纯文本为 `<token> <sequence> <context_epoch> 5 4 0 0`；不得把4截为2位。
- 模式操作会更换 owner context。观察 `/api/v2/state` 的新 `context_epoch` 后重新取得 v2 session，再演奏。确认 `mode.applied=4`、`mode.switching=false` 和 `legacy.inputs.ready=true`；状态还公开 `flute` 的真实 capability/ready/selected/busy/active/preset/ACK/fault/counter。
- 演奏沿用 `/api/v1/midi/raw`，JSON 的 web source、token、sequence、context_epoch 和 bytes 格式不变。normal 支持 MIDI60..84，固定 seed24680 由 FPGA 实现；没有新增手工 note mailbox。RAW 字节流保留 source/channel/Running Status，范围等 MIDI 语义由 FPGA 验证。
- API v2 旧25键快照仍以48为起点，所以笛模式只接受其中60..72；完整60..84范围使用 RAW。旧网页/Android UI 没有新增第五路入口，本轮不宣称它们支持笛；旧API v1笛模式仅保留空键释放/STOP。CC音色编辑、KS decay、FM参数等在笛模式清楚返回 unsupported。
- FLU1 B2 使用 `(nonzero_sequence<<1)|bind`，B3必须完整匹配，selected和ready/busy必须满足完成状态，才可重开 RAW。ready独立selected，所以正常解绑ready=1/busy=0。解绑可离开仍保留deadline fault的笛模块；它的离线诊断不封锁其他引擎。
- 本地 ENG1 模式使用 status[10]/[11] 的高位及 mask[20:16]。本地 revision 抢占会清除尚未确认的远程 FLU1 操作；新模式不会误当作DDS。

回归包含0/1/2/4的16种切换、100us SPI固定开销下全部进出笛、140us开销下进笛及回减法/FM、错ACK/序号回绕/本地抢占、真实RAW60/84 NoteOn/Off+sustain、旧硬件拒绝、HTTP能力/状态/拒参、未选中笛的故障隔离和带fault解绑。

140us固定SPI开销的**完整双bank冷重建后切KS**保留为已知基线限制，不计作通过：原工程的 `KS_PREPARE_TIMEOUT_US=1000000` 会超时。用任务开始时保存的 baseline/main 编译原 `test_three_engine.c`，`0 1 normal 140` 同样得到 `KS_FAULT_PREPARE_TIMEOUT=4`；因此本任务没有修改KS超时或用更宽超时掩盖它。现有 `run_local_engine.sh` 的100us完整切换和140us稳定idle/template预算回归另行运行。

完整 `tests/host/run.sh` 和 `idf.py build` 若在 `dds_factory_check` 报 `DDS Factory generated header differs`，那是预存 Galaxy TOML 与 fallback header 未同步的独立问题。此任务保留这些输入；不能把独立host通过或真实S3编译器的 `-fsyntax-only` 当作完整固件链接通过。
