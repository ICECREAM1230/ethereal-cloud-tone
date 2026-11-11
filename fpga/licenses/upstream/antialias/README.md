# 减法合成器的锯齿/PWM 抗混叠 DDS 振荡器（主工程接入版）

2026-10-18 更新：当前倒数已改为共享的实际步长归一化插值，ROM 为 `256×54 bit`，逻辑容量 1.6875 KiB。完整 DDS 仍为 11 级；C1/C2/C3 计算共享倒数，单边沿的距离乘法移至 C4。当前实现、SVF 完整编码压缩及验证入口见[系数表压缩说明](../../../../docs/compact_coefficient_tables.md)。下文 60 KiB 音高倒数、具体寄存分工和资源表属于 2026-10-14/15 的历史实现，应按各自日期使用。

术语修正（2026-10-18）：PolyBLEP与正弦斜率改进作用于DDS振荡器的波形生成。整个原音源仍是带SVF滤波、包络/调制和增益的减法合成器；保留现有 `DDS` 模块名、延迟常量和测试标识。以下数值、资源和验收结论仍按各自版本适用。

2026-10-15。正式 renderer 接入 `dds_antialias_oscillator`，活动 `.gprj` 为 94 项输入，四个具体波形模块分别实现锯齿、PWM、三角与正弦。保留另一个对话在 `f256d02` 中的预计算斜率正弦及两份 ROM 的原始字节；原 `dds_improve_oscillator` 与解析模块留作基线。原相位解码、11 级 DDS、33 拍回写/slot288、BASE恢复291、换页512、768拍采样周期、SPI ABI、滤波与 KS/FM 均保持当前约定。

本次主工程接入是独立候选之后的新任务，用户已授权四份 `.ai/` 记录；未暂存、提交、推送或写设备。当前实测与资源/时序的适用性见文末“主工程接入验证”；2026-10-14 的独立候选结果保留在下文。

## 实现来源与适配

采用 [DaisySP `Oscillator::Process()` / `Polyblep()`](https://github.com/daisyaudio/DaisySP/blob/2c72eaf9eac5fc0dca1919d65d606da907832618/Source/Synthesis/oscillator.cpp) 的双边二次多项式，固定参考提交 `2c72eaf9eac5fc0dca1919d65d606da907832618`。本地完整参考位于项目 `Open_resource/DDS波表合成/sources/DaisySP/`；其 Git 对象完整性和工作区一致性已在本任务核对。保留 MIT 声明于 [LICENSE.DaisySP](LICENSE.DaisySP)。原三角与 PWM 去直流公式继承项目 `osc_core.sv`，保留原作者和 MIT 标注。

这是相位跳变的 PolyBLEP 修正，不依赖正弦泰勒修正。锯齿与 PWM 是解析波形，正弦才使用当前 ROM；“分开波表实现”落实为四个独立波形模块，不为解析波形凭空增加采样表。

保持原递增锯齿方向、18 位 Q2.16 输出及名义幅度。PWM 保留有符号比较、`duty` 去直流以及 25 位原始幅度，未采用 DaisySP 的 `0.707` 增益。两条 PWM 边沿的修正可重叠，极端占空比不被硬钳到某个范围。正弦复用当前已接入主工程的斜率正弦，未改算法或 ROM；三角保留当前 24 位中间运算和截断。

## 模块分工

| 模块 | 职责 | 接收相位后的延迟 |
|---|---|---:|
| `dds_antialias_oscillator` | 原接口、共享相位生成/元数据对齐、四波形选择 | 完整 11 级 |
| `dds_antialias_saw` | 一条周期跳变的修正、原锯齿幅度和末端量化 | 9 级 |
| `dds_antialias_pwm` | 上升/下降两条边沿、去直流原始脉冲和末端量化 | 9 级 |
| `dds_antialias_triangle` | 原三角公式与输出对齐 | 9 级 |
| `dds_antialias_sine` | 封装复用原 `dds_improve_slope_sine` | 9 级 |
| `dds_antialias_polyblep` | 可复用的单边沿定点修正算子 | 8 级 |
| `dds_antialias_reciprocal_rom` | 所有波形共享的音高倒数同步读口 | 与原相位生成同步 |
| `dds_antialias_pkg` | 固定延迟约定 | — |

四个波形模块具有各自实现；Saw 内实例化一条边沿算子，PWM 内实例化两条，复用的是算子代码和倒数 ROM。没有跨元素积分器、波形历史 RAM、握手或回压。每拍可接收一个新的元素，输入相位仍由调用者按元素保存。波形有效信号按本包 `wave` 分派，顶层同时检查输出波形、相位和有效信号一致。

`candidate_sources.f` 是独立模块测试的依赖清单，路径相对于 `eda_proj/`。正式活动 `.gprj` 已列入本目录八份生产 HDL，并保持原正弦与相位依赖。

## 定点规则与流水线

令完整相位周期 `M = 2^24`、幅度因子 `A = 2^23`。原有符号相位对应递增锯齿的归一化相位为 `p = phase XOR 0x800000`。PWM 下降沿相位为 `(phase - duty) mod M`，占空比为 `(signed(duty)+A)/M`。

`step` 使用原 `dds_phase_generator` 的第二个实例并行解码：其旧相位固定为零，因此输出是当前实际增量；它不推进调用者的元素相位，也不拥有独立累加器。保留原音高 LUT、倍频移位和 `0x7fffff` 上限。这一安排消除了首版 `advanced_phase - prior_phase` 在 PWM 边沿准备中的 24 位减法，不增加拍数。倒数按所有 16384 个音高码离线生成 `R = nearest(2^40/step)`，不会用只有低 10 位的音高表代替实际步长。

在 `p < step` 时，`B = -(1-p/step)^2`；在 `p > M-step` 时，`B = +(1-(M-p)/step)^2`；其余位置为零。边界比较与 DaisySP 保持相同严格不等式。

```text
saw_raw = signed(phase) - A * B(p)
pwm_raw = old_dc_corrected_pulse + A * B(p) - A * B((phase-duty) mod M)
sample  = floor(corrected_raw / 256)
```

| 寄存级 | 工作 |
|---|---|
| G1 | 原音高 LUT 读取；同步读取本包音高倒数；保存 duty/wave/输入相位 |
| G2 | 原相位一次推进；并行增量解码；倒数和元数据对齐 |
| C1 | 登记边沿相位、实际步长与倒数，隔离 PWM 相位差运算 |
| C2 | 判断单边沿窗口，保存距离、倒数与边沿符号 |
| C3 | 完整 24×30 位距离/倒数乘积，寄存 DSP 结果 |
| C4 | 25 位 Q1.24 余量 `u=1-distance/step`，先钳到 0..1 防止无符号下溢 |
| C5 | 完整 25×25 位平方 |
| C6 | 平方输出寄存 |
| C7 | 恢复边沿符号，保留 48 位小数 |
| C8 | 边沿修正与原波形值对齐 |
| C9 | 52 位保护相加/相减及最终向负无穷量化至 18 位 Q2.16 |

共 11 个寄存级。从采纳边沿计为第 1 级，结果在第 11 级边沿出现，即相隔 10 个时钟周期；吞吐率为每拍一个输入。所有四种波形、先进相位和有效信号采用同一约定。复位清空在途有效包；无效周期的输出数据不构成接口承诺。ROM 输出寄存不带复位，避免限制 RAM 推断，其未初始化数据被有效流水线屏蔽。

## 本次验证

工具为 OSS CAD Suite 的 Verilator `5.053 devel rev v5.052-385-gb8e7f1e4b`。先运行当前 DDS 的真实 RTL 负对照：锯齿周期跳变 `got=-32768 / want=0` 导致数值检查失败，12 组频谱改善均为 0 dB，确认测试会识别缺失修正。

候选检查包含全部 16384 音高码的周期/边沿窗口边界、9 种 duty 边界分配、随机相位/音高/duty/四波形切换和间隙、运行期复位、64 帧×256 个独立元素的相位历史。共 **641882** 个有效包检查通过；其中 **50428** 个正弦/三角输出与当前 DDS 逐码一致。锯齿/PWM 与独立双精度 DaisySP 多项式的最大差异为 **1 个 Q2.16 输出码**，同时检查 11 级有效信号、一次模相位推进及输出范围。

频谱来自实际 RTL 输出的 65536 点相干矩形窗 FFT，采样率标注为项目约 `96008.04325 Hz`。排除 DC 与奈奎斯特以内的真实基音/谐波 bin，在 0..20 kHz 累加其余能量，相对于各自基音计算 dBc；该量不是音乐录音的噪声底或整机 THD。音高选自原 LUT，为了相干采样，实际音高与整数目标值有所不同。

| 实际基频 | Saw 混叠能量降低 | PWM 50% | PWM 10% | PWM 90% |
|---:|---:|---:|---:|---:|
| 1232.037 Hz | 34.61 dB | 35.31 dB | 33.86 dB | 33.86 dB |
| 3907.066 Hz | 35.19 dB | 37.65 dB | 43.66 dB | 43.66 dB |
| 7392.221 Hz | 37.12 dB | 34.83 dB | 40.63 dB | 40.63 dB |

12 组均通过至少 6 dB 的验收门限。局部编译没有本候选的位宽/符号/数组告警；复用的原斜率正弦存在一个已知未使用低位提示，未改原文件来消除此提示。

执行结果、输入字节快照、命令、日志、原始 RTL 数字样本及频谱 JSON 保存在 `/tmp/tang138k-dds-antialias-85cx0kxa/`。最终候选验证为 `green-final/`，其 `inputs.json` 标明本轮实际输入 SHA。该目录属于 2026-10-14 的独立候选任务，其编译产物已精确清理。本次主工程接入另见文末；两个任务的证据不混用。

从 `eda_proj/` 执行，输出目录必须为仓库之外的全新路径：

```bash
python3 -B src/DDS/DDS_improve/antialias/tools/generate_reciprocal.py --check
/home/ice/.local/bin/oss-cad python3 -B \
  src/DDS/DDS_improve/antialias/sim/run_checks.py --spectra \
  --out /tmp/dds-antialias-new-check
```

加 `--legacy-control` 可重复真实原 DDS 负对照，预期退出 1。运行脚本先复制实际输入，再在隔离副本编译、运行，不改正式工程。Python 数值频谱检查需要 NumPy，当前使用已安装 OSS CAD Suite 的 Python；没有安装或升级系统工具。

生成器 `--check` 为只读检查。需要重新生成时，指定全新的 `--out` 目录，核对 `coefficients.json` 中音高 LUT 哈希、公式及 ROM 哈希后再决定替换；已有目录被拒绝，避免覆盖。

## 范围与接入风险

PolyBLEP 减少混叠，仍是有限阶近似。频谱结果只覆盖上述静态音高和占空比；随机 duty/音高检查证明逐包数值与时序一致，并不证明任意快速 PWM、跳频、硬同步或不连续相位调制无混叠。当前正弦/三角的听感不会因本候选有所改善。

倒数 ROM 逻辑存储量为 **491520 bit / 60 KiB**，另有三条单边沿算子、共六个宽乘积。本次官方 Gowin 综合/布局布线的物理计数见下文，不能用逻辑存储量当作物理资源计数。后续可在保持独立波形接口的前提下评估共用 Saw/PWM 的周期边沿硬件，但本候选以清楚、可分别验证的模块为先。

2026-10-14 独立候选阶段的官方实现与集成为 NOT_RUN；这条历史边界已由下方主工程接入验证取代。完整 KS/FM 回归与实际 DAC/板上听音仍未运行。当前实例接入保持 11 级，滤波上下文及调度无需再补拍。

## 主工程接入验证（2026-10-15）

从实际 86 输入脏工作树保存恢复基线后，在完整副本接入八个模块；原正弦与 ROM 的 12 份文件逐字节保护。集成参考不改冻结 fixture，采用独立 64 位整数除法/多项式规格，复用原斜率数学参考，并补齐 8 拍传输；只比较有效数据，同时继续全周期比较公共输出、控制与实际写回 RAM。

未接入抗混叠的正式旧版被新参考实际拒绝；当前接入版的 32 帧 PCM/状态/标签/CSP/SPI/换页与相位+1负控通过。Verilator 641882 包、全部 16384 音高码、运行期复位/间隙与 12 组频谱复验通过，最大误差1输出码、50428个正弦/三角样本一致，频谱结果与上表相同。

首版官方布局布线出现 PWM 准备路径 −0.029ns 和既有 CSP −0.005ns；后续先并行步长解码；第二版仍有 −0.185ns PWM 输入违例。最终把边沿相位准备与窗口/距离选择分到 C1/C2，并取消原纯乘积复制级，以保持波形9级、完整11级。上电未知复位状态导致的两处仿真断言误报已在已知复位后启用检查；原正弦、CSP 及约束未修改。相关回归显式使用保留的 Icarus12；PATH中的14开发版对既有CSP前向引用有绑定错误，未修改CSP来迁就它。

最终副本的 30 项实际 core 执行记录（包含编译）全部通过，覆盖 32 帧独立对照及相位+1负控、SVF 系数/频响、33 拍回写和末元素 slot288 排空、SPI/换页、复位恢复以及 PCM 到 PT8211 输出。完整 KS/FM 专项回归与实板听音仍未运行。

Gowin V1.9.12.04 最终 `scripts/run_project.tcl all` 退出 0，94 项活动输入及相关 ROM/Tcl 共 117 个输入哈希在执行前后相同。当前既有约束下 setup/hold 违例均为 0，最小报告裕量为 setup **+0.037 ns**、hold **+0.165 ns**；recovery/removal 为 **+0.186/+0.152 ns**，最小脉宽裕量 **+0.763 ns**。首版两次 PWM 输入违例已由 C1/C2/C3 分工修复；当前最紧 setup 路径位于已有 FM 控制快照，裕量仍窄。保留 CSP 既有修改；用户明确本次聚焦音频、后续 CSP 时序报错不阻塞本次 DDS 接入，本轮最终报告没有 CSP 负裕量。

| 物理资源 | 原 86 输入版 | 当前 94 输入版 | 增量 |
|---|---:|---:|---:|
| Logic | 32851 | 34637 | +1786 |
| Register | 18001 | 18818 | +817 |
| BSRAM | 208/340 | 241/340 | +33 |
| DSP | 57/298 | 69/298 | +12 |

原版计数来自先前相同实际 86 输入工作树的官方报告；两个版本包含已有 FM/显示内容，数值不能作为 DDS 模块单独资源。当前 SDC/CST/设备保持原样，既有 I/O/时钟警告的约束适用边界仍保留。工具完成实现和当前约束 STA，并不证明实板音频表现。

本轮证据：[最终范围与输入身份](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/final-validation.json)、[官方实现报告](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/official-v3-report.json)、[core 执行结果](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/core-v3-icarus12/results.json)、[独立 DDS 检查](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/oscillator-v3/run.json)、[频谱](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/oscillator-v3/spectrum-results.json)、[精确清理](/tmp/tang138k-dds-antialias-integrate-zxv4u2kl/cleanup-report.json)。位流保留在验证副本，主工程 `impl/` 原产物未覆盖，未烧录。只删除本任务确认的仿真编译产物，保留源码、恢复副本、命令/日志/结果和官方输出。

相关检查命令：

```bash
python3 -B scripts/run_fpga_validation.py --section core \
  --iverilog /usr/bin/iverilog --vvp /usr/bin/vvp \
  --out /tmp/dds-polyblep-core-new
```
