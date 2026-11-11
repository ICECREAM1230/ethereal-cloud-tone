# Linux Gowin 启动与构建故障：按阶段读取

用于 Linux HDL 编译、综合、布局布线、时序分析、位流生成及 IP/Tcl 调用中的故障；Programmer/USB 下载故障另行处理。先保存安装版本、原命令、cwd、真实退出码和本轮完整日志，再读取对应段落。区分查找、加载、显示、权限、许可证和设计阶段；不要重新安装或全局修改库来替代诊断。

## 当前调用环境未找到程序

本机当前入口（2026-10-10）是 `"${GOWIN_SH}" <Tcl脚本路径>`，GUI 使用现有 Gowin 启动器。先复用该入口及其子进程环境，不恢复 Gowin 全局 PATH 前置，不为 agent 调用自动 source `.bashrc`。另一主机或用户另选安装时，再核对对应入口。

先区分交互式终端与非交互式 agent/脚本；Bash 用 `type -t` 判断名称是否解析为函数，用 `type -P` 查外部程序路径。用户已验证的交互式函数与 PATH 配置应保留并在对应环境中复用，不能把旧的查找失败当作当前交互式问题。非交互式调用不保证读取 `.bashrc` 或继承函数，应使用明确的独立入口与已验证子进程配置，不自动 source 启动文件或导出函数。

只在当前调用环境确实未发现入口时，查看用户指定安装、项目已验证的启动脚本和已知安装根下的 `IDE/bin/gw_sh`。`command -v` 失败不代表未安装；绝对路径解决查找，不会解决动态库冲突。记录所选安装版本，任务内复用已验证的调用；独立脚本不依赖隐式 alias/function 或历史实验目录。

## fontconfig/freetype 符号冲突

2026-10-08 本机 Ubuntu 24.04.5 / Gowin V1.9.12.04 的早期探测使用原始 `IDE/bin/gw_sh` 绝对路径，未经过 shell 函数或启动 profile，返回 127，日志包含 `libfontconfig.so.1: undefined symbol: FT_Done_MM_Var`。已验证的 headless 启动配置使用安装自带库/Qt 插件、offscreen 与系统 freetype preload；这不是所有版本的默认配置。

历史启动验证（已被 2026-10-10 的环境结构取代）：此前用户将 `GOWIN_IDE`、PATH 和采用该子进程配置的 `gw_sh()` 持久化到 `.bashrc`；交互式 Bash 中 `GowinModGen` 可从 PATH 找到，`gw_sh` 为函数，`gw_sh -h` 返回 0。

2026-10-10 已移除永久 Gowin PATH 前置，保留已有交互式函数，并新增 `"${GOWIN_SH}"` 供非交互式调用。该独立入口复用 headless/freetype/库设置，启动探测返回 0；以 [Linux 调用](gowin-build-ubuntu.md) 的当前入口为准。历史查找与本次启动结果均未证明综合、PnR 或完整许可证可用性。

出现对应冲突且已确认当前系统库后，可用 [Linux launcher](../scripts/run_gowin_linux.py) 选择显式选项。下例是本机同版本已验证的完整 headless 配置，并未分别证明三个开关都由该符号错误所必需；已知匹配配置可复用，首次诊断另一环境时应按症状增量选择选项：

```bash
python3 "$skill_directory/scripts/run_gowin_linux.py" \
  --ide "$verified_gowin_ide" --headless --bundled-qt \
  --freetype "$verified_system_freetype" --cwd "$probe_directory" \
  --log "$fresh_probe_log" -- -h
probe_rc=$?
```

`--freetype` 需要真实已确认的库文件；不要把本机 `/lib/x86_64-linux-gnu/libfreetype.so.6` 路径推广到其他架构/发行版。该选项替换子进程 `LD_PRELOAD`；`--bundled-qt` 替换子进程 `LD_LIBRARY_PATH` 与 `QT_QPA_PLATFORM_PLUGIN_PATH`，并移除外来 `QT_PLUGIN_PATH`。父进程及 shell 不变。按本机 `ld.so(8)`，preload 文件名中的空白/冒号、library path 中的冒号/分号不能转义；helper 在这些列表分隔符出现时拒绝调用，普通工具参数与安装路径中的空格仍按参数数组传递。不要全局 export、改系统/安装库或改许可证。

## Qt 插件与无显示环境

区分缺少可用显示与加载了其他 EDA/Python 的 Qt 插件。必要时查看本次任务相关的 Qt/库变量与安装自带平台目录；不要输出整个环境或许可证内容。

- 仅 Tcl/headless 调用且确需无显示运行：显式使用 `--headless`，子进程移除 `DISPLAY`/`WAYLAND_DISPLAY` 并设置 `QT_QPA_PLATFORM=offscreen`。
- 已证实外来库/插件冲突：显式使用 `--bundled-qt`，先确认选定安装确有 `lib` 和 `plugins/qt/platforms`。
- GUI：保留显示变量；不要照搬 Tcl headless 配置或把本 helper 当作 `gw_ide` 启动器。

首次诊断未归因的故障时，逐项/增量验证解决症状的最小组合；已有匹配版本和环境的实测启动配置可直接复用，不必为每次调用重做探测。不要仅因为安装是 Linux 就自动 preload 或强制 offscreen。

## hostid、权限与许可证

历史受限沙箱曾拒绝 AF_NETLINK，阻止 hostid/许可证检测。先核对当前实际权限与日志；该错误不等于许可证无效，也不要求在现在的环境中机械提权。若确有执行限制，遵守当前会话提供的权限机制；若无此限制，再核对所选版本、配置与真实许可证错误。不要修改许可证来绕过检测。

## 启动成功但配置/构建证据不足

Banner or help output only confirms startup. For IP-specific command behavior, settings, and output checks, use [gowin-ip-generation](../../gowin-ip-generation/SKILL.md).

构建返回 0 或存在旧 `.fs`/报告仍需核对当前输入、实际阶段、输出新鲜度与完整日志；见 [Linux 调用](gowin-build-ubuntu.md)。同一 `.gprj`/`impl` 只允许一个写入者，隔离试验需完整保全实际输入。

## HDL 编译与综合

先找本轮日志中首个导致阶段失败的错误，再处理后续连锁报错。动态库/Qt 错误发生在工具启动阶段，不是 HDL 语法错误；已进入综合器后，不应继续靠切换启动库处理 RTL 错误。

| 症状 | 核对及处理 |
|---|---|
| 语法、语言标准或 include 错误 | 核对实际报错文件、宏、include 路径和选定语言标准；SystemVerilog 的本版调用差异见下一节。不要仅按文件扩展名推断标准。 |
| 找不到模块、顶层或初始化文件 | 以选定 `.gprj` 的启用文件、top、器件及实际 cwd 为准，检查外部引用、IP wrapper/网表、初始化文件是否完整。磁盘上有文件不等于参与综合。 |
| IP 缺失或模型不适配 | 区分仿真模型与官方可综合实现，按该 IP 的实际生成输出集成；不要用仿真 stub 或随意 black box 代替正式实现来宣称综合通过。 |
| 推断、位宽、多驱动、锁存器等报告 | 回到首个相关源位置和预期硬件结构；区分会改变功能/资源的警告与阶段失败。不要批量压低警告等级掩盖问题。 |

Verilator 的 lint/功能仿真通过与 Gowin 综合通过分别记录；前者不能证明官方综合器支持同一语法、IP 或资源映射。

## Tcl 与 IP：本机已确认的版本差异

以下实测仅适用于 **Linux Gowin V1.9.12.04**；其他版本先查选定安装的命令帮助、手册和实际配置。不要从 GUI 标签或另一版本示例猜 Tcl 参数。

| 调用或现象 | 已确认的处理 |
|---|---|
| `add_file -type sv` 被拒绝 | 已验证流程为 `add_file -type verilog` 配合 `set_option -verilog_std sysv2017`。 |
| `-synthesis_tool GowinSynthesis` 被拒绝 | 使用实际项目/帮助确认的值；该次新项目默认值为 `GowinSyn`。 |
| `-gen_text_timing_rpt 1` 报 `unknown option` | 2026-10-08 隔离 Tcl 探测确认本版拒绝该选项，`catch` 返回 1 且错误消息变量为空。该可选报告开关可省略，或只对此开关捕获错误并明确记录；不能据此跳过实际时序报告检查。 |
| 重跑时重复 `add_file` 报错 | 打开已有项目后先核对清单，不重复添加；不要为重跑删除原有启用文件。 |
| IP property output or coupled settings behave unexpectedly | Use the [IP Tcl notes](../../gowin-ip-generation/references/tcl-ipflow.md) for version-specific checks. |

IP 名称、属性、相关参数及产物核对步骤集中在 [官方 IP 生成](../../gowin-ip-generation/SKILL.md)。前三项的手册差异也见 [项目与构建 Tcl](tcl-project-build.md)。这些已观察错误不能推广成所有 IP 或所有版本的限制。

路径应明确相对于调用 cwd、脚本目录还是项目目录。需要脚本目录时用 `[file dirname [file normalize [info script]]]`，用 `file join` 和 Tcl list 传递路径；不要拼成未引用命令再 `eval`，或按空格拆开含空格/中文的路径。外层调用使用参数数组。即使 shell cwd 是隔离目录，脚本中的 `cd`/`open_project` 仍可能指向原工程；运行前检查脚本实际写入目标。

`catch` 只用于预期且允许失败的分支，例如上述可选报告开关。必需的项目打开、属性设置或构建失败应停止后续阶段并给出失败状态；不能统一 `catch` 后继续生成。即使命令不抛 Tcl 异常，也要检查日志和配置断言。

## 布局布线、时序与位流产物

`run syn` 只完成综合；`run pnr` 要有与当前输入一致的综合结果；需要完整实现时使用已确认的 `run all`。不臆造 `run bitstream` 或 `run clean`。选项与阶段语法见 [项目与构建 Tcl](tcl-project-build.md)。

| 阶段或症状 | 核对及处理 |
|---|---|
| CST/SDC 对象未匹配或引脚约束失败 | 检查启用的约束文件、完整器件/封装、顶层端口和综合后实际对象；不要关闭约束报错或删约束来换取成功。 |
| 资源、放置或布线失败 | 先看本轮资源利用率及首个失败原因，区分容量、专用资源/位置、I/O 与布线问题，再据证据改设计或实现选项。 |
| setup/hold 违例或路径未约束 | 分别报告时序违例和约束覆盖情况。核对时钟、生成时钟、I/O 延迟及例外的设计依据；位流生成不能作为时序收敛证据。 |
| 没有预期 `.fs`，或只有旧文件 | 确认本轮实际执行到哪一阶段、输出目录和 basename；核对当前日志、生成时间及输入记录，必要时比较前后哈希。相同哈希可来自确定性重建，不能单凭哈希断言未生成。 |

分别报告“综合通过”“PnR 完成”“本轮位流已生成”“时序满足且约束覆盖已核对”。只运行帮助、本轮返回 0、目录中有历史 `.fs` 或报告，均不足以宣称完整实现通过。本参考新增的阶段检查是诊断指引，不代表这些类别已在本机逐项复现。
