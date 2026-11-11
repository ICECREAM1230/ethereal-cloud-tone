# FMC / RVR SPI 读回窗口修复（2026-11-03）

本次地址修复已通过离线回归；主工程安装、统一综合和队列 PnR 的实际状态见本任务 `continuation.json` 与 `coordination/` 回执。尚未烧录配套固件或位流，未新增实板验收。

## 修复范围

原 FMC 五个只读状态字占用 `0x0720..0x0724`，高优先级的 transport 模板译码遮住 RVR 的同址读回，导致 ESP32 读不到 `RVR1`。本次保留既有混响 ABI，将 FMC 状态移到经当前生产地址表审计的空闲区域：

| 所有者 | 读地址 | 本次变化 |
| --- | --- | --- |
| RVR1 | `0x0720..0x0727` | 不变，含标识、ABI、状态、已应用参数/序号和 generation |
| AOR1 | `0x07B0..0x07B7` | 不变 |
| FMC1 | `0x07C0..0x07C4` | 新只读窗口，依次为 magic、accepted、completed、result、CRC |
| FMC 保留区 | `0x07C5..0x07CF` | 不增加读选择或写许可 |

FPGA `control_input_transport.sv` 五个精确 case 和两份 `fm_config_v1.h` 同步更新。FMC 配置写 `0xB410..0xB41C` / COMMIT `0xB41F`、APB 状态 `0x00D0..0x00E0`、CRC、13 字配置格式、所有混响和 PCM 选路写入均不变。不增加旧 `0x0720` fallback。

本次保留主工程既有 FM32×4op / 20MHz / II1、分步复位和 SSRAM 优化、CDC/显示时序改动、五音色/46 参数、默认普通电钢琴 catalog3、FDC 私有 variant、其它引擎与原网页。

## 配套产物

ESP32 版本为 `ae350-fmc-mapc0-20261103`。真实 ESP-IDF v6.1 / ESP32-S3 构建 APP 为 1,415,648 B，SHA256 `ccb10418d41b016cbeab2930d19fe8036969263fe8276aae9308ee544c28037e`；factory 余量 120,352 B。393 项输入已冻结，网页 gzip 解压后与任务前主树逐字节相同。与新 FPGA 位流配套部署；构建成功不等于已烧录。

AE350 RV32 已用更改后的共享头重新编译：55,216 B、SHA256 `ceab95e51a3186ffea0654aa4b570af92e4c1e13d4dc975ac528002a1d92f211`，与任务前逐字节相同，四条 ITCM lane 不变。官方生成的 32 SP / 64 KiB ITCM 重建比对为 0 字节差异；不因未被 AE350 使用的 SPI 地址宏而重写相同的官方 IP。

## 验证与持续检查

- 原源码物理 SPI RED：`0x0720` 精确实读 `FMC1`，触发应为 `RVR1` 的断言。
- 候选真实 SPI GREEN：416 checks / 141 Mode0 frames，使用原 transport、实际 `reverb_control` 和 `pcm_output_routes`；覆盖混响 shadow/commit/ACK、FMC 五字及突发边界、AOR 更新、RAW、复位和只读写拒绝。
- 实际重叠 mutant：把首字译码改回旧址后同一测试精确失败。
- 既有 prepared SPI：43,107 checks / 85 frames 通过，仅更新其两处 FMC 读地址。
- ESP32：13 组 host/HTTP 回归（含新增的真实 `engine_link.c` 同一 SPI 表 FM/RVR 并存、独立完成/CRC）；旧 FPGA 冲突和旧 S3 地址两条负控均在 owner discovery 失败。
- `sim/config/spi_status_windows.json` 与 `check_spi_status_map.py` 校验已审计的当前生产 `0x0700..0x07FF` 选择器、掩码、实际实例 BASE、双头一致及保留区。现存非生产 FLU1 低 B 页兼容覆盖单独记录，不扩展修复。本检查不是通用 SV 解析或全配置地址唯一性证明。

主工程安装后及 SYN→PnR 入口需显式执行：

```bash
python3 -B sim/config/check_spi_status_map.py --project . --out <本轮结果>/spi-status-map.json
python3 -B scripts/manage_control_build.py verify
```

SYN 验收还必须核真实 exit0、`GowinSynthesis finish`、无 ERROR、current=run=本任务冻结输入、官方 ITCM 及保留的时序约束。PnR 只经全机共享队列启动。旧含冲突的 `syn_20261102T151257Z` 已保全，不用于新地址版 PnR。

## 证据与边界

本任务根目录：`sim/results/fmc_status_relocation_20261103T003159/`。

保留 `before/`、`baseline-inputs.json`、地址审计及 checker 正/负控、`spi-tests/run01/`、`prepared-spi/new-window/`、`rv32/`、`esp32/` 完整输入/产物和复审回执。TB 保留于 `sim/tb/`，runner/config 保留于 `sim/config/`。仅清理本次已确认无用的 scratch 编译文件；不删除旧任务或不明文件。

离线 SPI/host、固件构建、地址表检查和 ROM 身份验证通过不能代替新综合/PnR/STA/实板。PCM A/B 停声使用独立 `0x07B0..0x07B7` 链路，本次未把其根因归到 FMC/RVR 冲突。

## 主工程构建回执（2026-11-03T01:23:17.680615+09:00）

本节由唯一主工程 owner 在安装后追加，属于运行状态文档；原 13 文件安装快照与回归绑定 SHA 保留，不改写原验收记录或冻结清单。

- 唯一新综合 `syn_20261102T160112Z` 真实 exit0、`GowinSynthesis finish` 且无 ERROR；官方 verify、350 项 current/run/stage/frozen 同一性、配套 S3 393 输入/APP 身份及 55,216 B / 32 SP / 64 KiB ITCM 0 diff 均通过。
- 主 SPI map、11 控制选择器、51 音频选择器及 6 payload 预算、三路音频映射和 SUBcap CDC 检查全部通过。新 VG 语义审计确认 9 个独立 Q、含反馈 leaf fanout 最大 16；FFT 同步 SDPB 共 3 个物理 BSRAM，155 bit 流水及两条共享 motion 链保持。整机 LUT 76,579 / FF 57,190 / SSRAM 167 / BSRAM 279；FM channel SSRAM 18 且无 `_ER_RS` 辅助 RAM。
- 有效共享队列预约后启动 `pnr_20261102T161634Z`（JST 2026-11-03 01:16:34），build PID `1461990`、supervisor `1461989`、独立 viewer `1461997`；票据 `733c6719b53c42e283106d425bc6bc25` 已立即 attach，启动回执为 RUNNING。日志：`logs/control_build/pnr_20261102T161634Z/gowin.log`。
- 原生综合检查 `ae350-fmc-mapc0-20261103` 已删除，核 DB 无行、TOML 不存在。本轮仅确认启动，不读取新 PnR exit/log/STA；后续监测或分析需用户新请求，尚未验收新 routed timing。

验收与启动证据：本任务 `coordination/syn-verification/acceptance.json`、`coordination/pnr-startup-receipt.json`、`coordination/synthesis-check-deleted.json`。本轮无设备访问/编程或 stage/commit/push。

## PnR 重启回执（2026-11-03T01:29:54.475528+09:00）

旧 `pnr_20261102T161634Z` 中断、无真实退出码，日志止于读取网表；旧 STA/FS 不能视作该轮结果。用户授权重新开始后，350 项输入与已验收 `syn_20261102T160112Z`、任务 frozen 全同，官方 verify 和独立准入证据复核通过，因此只重启 PnR。

新运行 `pnr_20261102T162909Z` 于 JST 01:29:10 启动，build `1484605` / supervisor `1484604` / 独立 viewer `1484629`；有效预约票据 `df5b49458ae74ccfb82d2231f4af5be8` 已立即 attach。日志 `logs/control_build/pnr_20261102T162909Z/gowin.log`。构建位于独立瞬态 systemd 用户服务 `ae350-fmc-pnr-retry-20261102T162909Z.service`，已验证其 cgroup 不属于 Chromium 应用组，保留内存计量以便诊断。未修改冻结构建输入或系统 OOM 策略。

本次仅启动确认，不读取新 PnR exit/log/STA；后续监测和时序验收需用户新请求。启动证据 `coordination/pnr-retry_20261102T162841Z/startup-receipt.json`。
