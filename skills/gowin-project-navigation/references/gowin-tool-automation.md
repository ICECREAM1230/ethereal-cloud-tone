# 高云命令行工具与 Tcl 编排（Linux）

用于约束工具职责以及已授权的 Programmer 操作。仅记录命令行、文件接口与已验证能力；不包含 GUI 启动或操作步骤。工程/IP 构建仍读取 [Linux Gowin 调用](gowin-build-ubuntu.md)。

## 按任务选择接口

| 任务 / 工具 | 自动化入口与边界 |
| --- | --- |
| 工程、综合、PnR、STA：`gw_sh` | 原生 Gowin Tcl，见 [工程 Tcl](tcl-project-build.md) 与 [IP 生成](../../gowin-ip-generation/SKILL.md)。 |
 |
|  |
| 布局约束：`floorplanner` | 识别其约束编辑器职责。AI 使用 [CST 文件接口](cst-physical-constraints.md)，再由选定工程的 `gw_sh` 流程检查。 |
| 时序约束：`gw_sdceditor` | AI 使用 [SDC 文件接口](sdc-timing-constraints.md)，再由工程的官方 STA 检查；编辑器文件名不证明存在批处理 API。 |
| 下载：`programmer` / `programmer_cli` | `programmer` 是图形前端；AI 自动化使用已核对版本的 `Programmer/bin/programmer_cli`。 |

`gw_sh` 原生 Tcl、SDC 约束语言、CST 声明以及 Tcl 的 `exec` 编排是不同接口。Tcl 可以通过参数列表调用外部 CLI，这不把该 CLI 的参数变成 Gowin Tcl 命令。不要把 CST/SDC 语句直接交给 `gw_sh` 解释；通过 `.gprj` 的有效约束条目接入工程。

## 已核对的 Linux 安装

Programmer 位于 `Programmer/bin`。这是当前机器的发现证据，不是其他安装的固定路径。复用已确认路径；查找失败时再检查环境及已知安装根。

只读帮助调用：

```bash
"$gao_sh" -h
"$gvio_sh" -h
"$programmer_cli" --help
```

本机 GAO/GVIO 帮助退出码均为 **2**，Programmer 帮助退出码为 **0**。保存原始文本和状态；这些帮助结果不证明设备连接、采样或下载成功。`programmer_cli` 显示 `V1.9.12.04 (64-bit) build(2743)`；其他版本需重新核对。命令行探测默认保留环境；发生启动错误后按该工具自身依赖处理，不能把 `gw_sh` 的环境配置机械套给其他工具。

## GAO 采样与导出

依据安装附带 **SUG114-3.5E 第5章**及本机 `gao_sh -h`，入口为：

```bash
# 仅在板上调试已授权、配置与已加载设计匹配时执行。
"$gao_sh" -gao "$gao_config" -cable "$cable_type" \
  -device "$device" -device_version "$device_version" \
  -location "$cable_location" -out "$capture_vcd" \
  -report "$capture_report" -stop_timeout "$timeout_seconds"
```

必需参数是 `-gao` 与 `-cable`；其余参数按任务和实际帮助选择。本版接受 `.gao`/`.rao` 配置，导出 `.csv`/`.vcd`/`.prn`/`.gwd`，报告为 `.json`。器件、版本、线缆类型和 location 从真实工程与连接取得；输出使用新的任务目录，避免 `-overwrite` 覆盖旧证据。

先核对选定 `.gprj` 的 GAO 配置、插入调试核的构建结果与板上已加载镜像，不能用过期 `.rao` 解释另一版设计。`-force_trigger_timeout` 会强制触发，`-auto_run` 会重复采样，仅在任务需要时选择。退出码、报告、触发/采样完成状态和实际导出内容共同决定结果；GAO 实板采样与 RTL 仿真是不同证据。

## GVIO 能力缺口

本机确实存在 `gvio_sh`，但其 `-h` 输出的是 **`gao_sh -gao ... -cable ...`**，没有给出 GVIO probe 的批量读写参数或 Tcl 脚本入口。
GVIO 任务先核对 `.gvio`、相关调试核、器件、构建产物与所需 probe 行为。需要自动读值或驱动输出时，继续寻找该版本明确公开的命令/接口；不要把 GAO 采样命令伪装成 GVIO 驱动，也不启动 GUI 补齐自动化。

## Programmer 命令行

依据 **SUG502-2.5.2E 第4章**及本机 `--help`，已核对的命令形状为：

```bash
# 这是待填实参的设备写入模板；构建 bitstream 不授权执行它。
"$programmer_cli" --device "$programmer_device" \
  --operation_index "$operation_index" --fsFile "$bitstream" \
  --cable-index "$cable_index" --uid "$cable_uid"
```

`--device` 使用该 CLI 帮助列出的器件名，与 `.gprj` 的完整料号核对后选择，不能直接复制完整 SKU。SRAM 配置、SRAM 校验和内/外部 Flash 擦写是不同 operation；本机帮助中 2 是 `SRAM Program`，4 是 `SRAM Program and Verify`，不是跨版本通用编号。Flash 操作按目标器件和当前帮助单独确定，不能从 SRAM 模板推断。

执行前将实际镜像路径/hash、器件、操作含义、线缆选择方式和菊花链参数具体化，保留日志与退出码。上面的模板仅用 UID 选择线缆；若任务使用 location/channel，则替换 UID 选择方式，不混用。**本机帮助与手册有冲突**：SUG502 §4.6 声称 location 优先于 UID，而本机 `--help` 声称指定 UID 时忽略 location 与 channel。当前未用设备验证优先级，不能静默依赖其中一说。`--scan-cables` / `--scan` 会接触连接设备，仅在任务需要且设备操作范围允许时调用；帮助验证不需要扫描。权限失败应报告实际症状，不自动修改 udev、安装驱动或以 root 重试。

Tcl 可用参数列表编排已授权的 CLI 操作，例如：

```tcl
set command [list $programmer_cli --device $programmer_device \
    --operation_index $operation_index --fsFile $bitstream \
    --cable-index $cable_index --uid $cable_uid]
set tool_failed [catch {exec {*}$command 2>@1} tool_output tool_options]
puts $tool_output
if {$tool_failed} {
    set tool_error [dict get $tool_options -errorcode]
    puts stderr $tool_error
    if {[lindex $tool_error 0] eq "CHILDSTATUS"} {
        exit [lindex $tool_error 2]
    }
    exit 1
}
```

参数应来自已核对的值；用 `list` 与 `{*}` 保持路径/参数边界，不拼 shell 字符串。原始 CLI 状态与实际报告仍需记录；Tcl 脚本走到末尾不能代替设备操作验收。2026-10-08 的调用验证仅覆盖帮助与接口发现，未验证扫描、下载、擦除或板上调试行为。
