# FM32 单流水线核心（2026-11-02）

`jt51_native32_top` 使用一个 JT51 算子流水线处理 32 声部 × 4 算子。`cycles[6:0]={op[1:0],voice[4:0]}`；请求工作期间每时钟推进一个槽，稳态每请求 128 槽，帧间完整停车。首请求因原 JT51 zero 初始化包含 129 槽。prepared executor 保持 reset 512 clocks，wrapper 保留 8 帧 warmup。

接口与 native96 相同，增加 `bank_i[1:0]`，`ae350_mute_i/ae350_idle_o` 扩至 32 bit。MMR 在 ADDRESS 相位锁存 bank；通道及算子地址取 `{bank,address[2:0]}`，KEYON 取 `{bank,data[2:0]}`；其余全局寄存器忽略 bank。busy 持续 128 个有效算子使能，不按墙钟倒计时。调用方需在有效 operator step 发 ADDRESS/DATA 单拍，并等 busy 解除。

状态环按真实反馈闭环扩大：PG 29→125，EG 27→123 / 32→128 / 31→127，参数/KEYON 环 32→128；通道历史及累加环 8→32。算子结果输出 padding 4→52，使入口到反馈结果的延迟由 16 槽变为 64 槽（两个算子组）。PG reset 的 4 级、EG 算术 3/4 级和 phase modulation 8 级保持不变。立体声累加从 19 扩为 21 bit guard，最终仍饱和至 16 bit，不额外缩小单声部幅度。

20 MHz 墙钟下沿用 native96 的 63/176 物理时基；每个 PCM 请求才推进一次 voice phase/EG 帧，沿用 14931/25630 的采样时间比例。相同 PCM 请求率为 `91015625/948 Hz`。测试严格使用相应 208/209 clocks 有理周期。LFO/noise/timer 物理时基不随 operator 停车停止。

## 动态 EG 的量化边界

原 JT51 的共享 EG counter 在 `zero` 更新，而 stage IV 在同一时钟沿读取更新前值。扩大环后，跨 `zero` 的逻辑算子位置随环长移动。这能令特定声部的 envelope step 相对旧 8 声部环提前或滞后一帧；不是 envelope 或 pitch 加速。真实 `jt51_eg96` 与 `jt51_eg32` 的独立 8000 帧比较证明：普通槽逐状态一致，旧 wrap 位置 cycles31 与新 cycles127 一致；按 `eg_slot_II=cycles-1`，对应原 voice6 和新 voice30 的第四算子。普通槽值始终等于 wrap 槽的当前或前一帧值，attack/decay1/decay2/release 全部覆盖。

因此不承诺所有动态包络下旧8声部PCM逐bit一致。保留初次动态比较的 1 LSB 差异记录，并用可重复的 wrap 诊断与逐算子 PG/状态比对区分包络量化和反馈错位。静态 envelope、LFO/noise 关闭时，8 个算法 × 8 个 feedback 设置与未改 native96 的 PCM 和算子输出逐bit比较。

验证入口：`python3 sim/config/run_fm32_core.py` 与 `python3 sim/config/run_fm32_core.py --eg`。TB 位于 `sim/tb/fm32_core*.sv`，保留结果位于 `sim/results/fm32_core_20261102/`。这些是实际 RTL 的功能仿真；官方综合、PnR/STA 和实板验证由上层任务单独完成。vendor 原文件保持不变，native96 计算源保留，共享 timebase 已按20MHz更新；fork 源身份见 `provenance.json`。
