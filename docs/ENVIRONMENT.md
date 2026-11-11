# 环境搭建与复刻要求

本文以当前仓库的配置、依赖锁和已记录验证为准，检查日期为 2026-11-12。命令供读者在自己的机器上按需执行；编写本文时没有安装软件、运行构建、综合、布局布线或烧录。下文不把历史个人绝对路径当作安装位置。

## 先选择复刻范围

| 目标 | 需要准备 | 可以暂不准备 |
| --- | --- | --- |
| 只读代码、协议和架构 | Git、文本编辑器；可选 Python 3 做只读清单校验 | 全部 EDA、SDK、交叉工具链和硬件 |
| 构建软件、运行主机回归 | 按所选子项目准备 Python/Node/ESP-IDF/Android 工具；主机 C 测试需要 GCC | 不修改 CPU 固件时不需要 RV32 工具链；不做 FPGA 实现时不需要 Gowin |
| 重建 FPGA 实现 | 正式 Gowin、正确器件数据库、有效授权、当前 `.gprj` 的全部输入；日志查看工作流需要桌面终端 | 单纯综合现有 ITCM 镜像不要求重编 AE350 |
| 复刻实物系统 | 前述所需工具，以及 FPGA、控制器、面板、传感器、音频与供电连接；按需求增加 USB PCM 采集链 | Android 模拟器、桌面模拟界面不代替实物 |

软件构建、FPGA 实现、设备编程与实板验收是分开的步骤。已有二进制和历史记录不能证明另一台机器或另一套接线已通过。当前验证范围与已知历史测试失配见 [VALIDATION.md](VALIDATION.md)。

## 版本基线和锁定位置

| 部分 | 当前复刻基线 | 证据与限制 |
| --- | --- | --- |
| 主机 | Ubuntu 24.04 / Linux x86_64 | 本次只读环境为 Ubuntu 24.04.5 LTS、Python 3.12.3、CMake 3.28.3；不等于最低版本声明 |
| Gowin | V1.9.12.04，匹配 GW5AST-138C/138K 器件数据库与授权 | [正式工程](../fpga/ethereal_cloud_tone.gprj) 选择 `GW5AST-LV138PG484AC1/I0`、`gw5ast138c-007`；不任意替换器件/修订 |
| AE350 软件重编 | 生成记录为 GCC 13.2.0；RV32IM/ILP32 soft-float multilib | [镜像构建记录](../fpga/firmware/ae350/images/control-build.json)、[实际脚本](../fpga/scripts/build_control_firmware.py)；前缀虽可为 `riscv64-unknown-elf-`，输出必须是 RV32 |
| ESP32 A 板 | ESP-IDF v6.1 / 配置头 6.1.0；ESP32-S3；CMake ≥3.22 | [CMake](../esp32/a_board/CMakeLists.txt) 默认 [config/sdkconfig.esp32s3](../esp32/a_board/config/sdkconfig.esp32s3)；无额外 managed component 锁 |
| ESP32 B 板 | ESP-IDF 6.1.0；历史已验证 v6.1 提交 `fff9895c82d744c7237be8847347bdd1b07c6643` | 独立核对 [sdkconfig](../esp32/b_board/sdkconfig)、[dependencies.lock](../esp32/b_board/dependencies.lock) 与 [README](../esp32/b_board/README.md)；`esp_tinyusb=2.4.0`、`tinyusb=0.21.0~2`，不是从 A 板版本推断 |
| 网页 | Node.js ≥22 是项目约束；本次只读主机为 v26.13.0 | [package.json](../apps/web/package.json)、[package-lock.json](../apps/web/package-lock.json)；Playwright 1.63.0、Terser 5.51.2，其他包以锁文件为准 |
| Python 主机程序 | 已使用 Python 3.12.3；分工具安装 requirements | PCM：`PySide6>=6.6,<7`、`pyusb>=1.2.1,<2`；B debugger：Tk + `pyserial>=3.5,<4`；MIDI debug：`pyserial==3.5`。范围依赖未完全锁定，不宣称跨时间逐包一致 |
| Android | Temurin JDK 17.0.20.1+1；SDK Platform 36 revision 2；Build Tools 36.0.0 | [toolchain-lock.json](../apps/android/tools/toolchain-lock.json) 保存下载 URL/SHA；编译/目标 API 36、最低 API 26，当前脚本不使用 Gradle 下载链 |
| 可选模拟器 | 项目 [preview-toolchain-lock.json](../apps/android/tools/preview-toolchain-lock.json) | Linux 需可用 KVM；模拟器非 APK 构建必需 |

Windows、WSL 和其他 Linux 发行版没有在本次发布中完成同等搭建与硬件验证。保留 Windows 启动器不表示整套工程已跨平台验收。

## 主机基础环境

建议为源码、SDK、缓存与构建输出分别保留空间。首次 ESP-IDF 和 Android 工具安装需要网络；Gowin 需按厂商渠道取得安装包和授权。工具链、厂商软件安装目录、许可证、完整官方手册均不随仓库提供。Python 虚拟环境与 `node_modules` 应在本机重建。

Ubuntu 下，ESP-IDF v6.1 官方 Linux 指南列出的前置包如下；`build-essential` 用于本仓库的主机 C 回归，`python3-tk` 用于 Tk 界面，`gnome-terminal` 用于桌面日志查看：

```bash
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  build-essential python3-tk gnome-terminal
```

这些命令会修改系统，需要读者自己的管理员认证。Ubuntu 默认 Node 包不一定符合项目要求；请从 [Node.js 官方下载](https://nodejs.org/en/download) 安装所选 Node ≥22，并检查 `node --version` 与 `npm --version`。本项目只规定最低引擎范围，没有锁定任意 Node 22 小版本。

以下片段约定从仓库根目录开始：

```bash
export ECT_ROOT="$PWD"
# 输出目录只用 ASCII 字符；目录名可按本机情况调整。
export ECT_BUILD_ROOT="${TMPDIR:-/tmp}/ect-build-$USER"
mkdir -p "$ECT_BUILD_ROOT"
```

如 `TMPDIR` 或用户名含非 ASCII 字符，请自行选择全 ASCII 的输出路径。不要复制旧文档里的其他机器目录。

## Gowin：安装位置、授权与 CLI 启动器

从 [Gowin 官方 EDA 下载页](https://www.gowinsemi.com/en/support/download_eda/) 获取 **V1.9.12.04 Linux x86_64**，按对应安装说明放置到本机选定目录，并确认该版本的器件数据库包含正式工程指定器件。使用厂商支持的授权方式；CLI 能打印帮助不证明授权、器件数据库或完整实现已可用。

[run_gowin_synced.py](../fpga/scripts/run_gowin_synced.py) 的当前 `--launcher` 默认是 PATH 中的 `gw_sh`。部分外围工作流也约定用户的 `.local/bin/gw_sh`，因此仅设置一个 `GOWIN_SH` 变量还不够。确认本机 `GOWIN_IDE` 指向包含 `bin/`、`lib/`、`plugins/` 的 **IDE 目录** 后，可按以下方式配置用户启动器：

```bash
# 先把 GOWIN_IDE 设置为自己已安装的 V1.9.12.04 IDE 绝对目录。
: "${GOWIN_IDE:?set GOWIN_IDE to the installed Gowin IDE directory}"
export GOWIN_IDE
install -Dm755 "$ECT_ROOT/skills/host_support/gw_sh" "$HOME/.local/bin/gw_sh"
export PATH="$HOME/.local/bin:$PATH"
export GOWIN_SH="$HOME/.local/bin/gw_sh"
command -v gw_sh
```

已有同名用户启动器时先核对并保留原文件，再决定是否采用本包版本。此小启动器把 Qt/库设置限制在子进程；它依赖匹配的 Linux FreeType/Qt 库，不能用全局 `LD_LIBRARY_PATH` 或替换厂商库来掩盖缺失。构建入口支持显式 `--launcher "$GOWIN_SH"`；具体阶段和参数先以工程脚本为准。`gnome-terminal` 仅负责独立查看实时日志，关闭查看窗口不应停止后台任务；没有桌面会话时不能假定日志查看器可弹出。

此文不执行综合/PnR。正式输入、构建流程和实施前提见 [FPGA 架构](FPGA_ARCHITECTURE.md)、[Gowin 导航技能](../skills/gowin-project-navigation/SKILL.md) 与 [Linux 调用](../skills/gowin-project-navigation/references/gowin-build-ubuntu.md)。不要用开源工具结果替代正式 Gowin 工程实现验收。

8 套官方手册只保留版本/SHA/获取线索；正文需从已授权的本机 `IDE/doc` 或官方来源获得，按 [手册读取说明](../skills/gowin-arora-v-rtl/references/sources.md) 设置 `GOWIN_MANUAL_DIR`。`gowin-ip-generation` 自编 native 桥仍保留原主机固定安装路径和 Qt 5.15 ABI 假设：**`GOWIN_IDE` 不会单独使它跨机可用**。另一安装需协调适配源码/runner、重编并另行验收，详见 [native 桥适用范围](../skills/gowin-ip-generation/references/public-package.md)。常规项目构建与这个可选 IP 生成适配器不是同一个依赖要求。

## AE350：只有重编 CPU 固件才需要外部 RV32 工具链

当前 [control.bin](../fpga/firmware/ae350/images/control.bin) 与工程 ITCM/IP 镜像保留为配套输入。查看源码、主机协议测试或使用现有镜像进行 FPGA 构建，不要求重新安装 CPU 交叉编译器。

确需重编时，准备包含 GCC/binutils 和 **RV32IM/ILP32 soft-float libgcc** 的外部裸机 RISC-V 工具链。脚本默认工具前缀为 `riscv64-unknown-elf-`，实际使用 `-march=rv32im_zicsr -mabi=ilp32`，并拒绝不匹配的 libgcc。不能只凭编译器文件名判断架构兼容。来源构建记录的 GCC 为 `13.2.0-11ubuntu1+12`，本次未重新验证 RV32 目标；没有提供通用“一条安装命令即可逐字节重建”的承诺。

以下为只读预检及可选隔离构建示例；先让 `ECT_RV_PREFIX` 指向自己已有工具链前缀：

```bash
: "${ECT_RV_PREFIX:?set the installed toolchain prefix, ending in -}"
"${ECT_RV_PREFIX}gcc" --version
"${ECT_RV_PREFIX}gcc" -march=rv32im_zicsr -mabi=ilp32 -print-libgcc-file-name
# 需要重编时才运行；不更新工程 ITCM 输入，也不访问设备。
python3 "$ECT_ROOT/fpga/scripts/build_control_firmware.py" \
  --prefix "$ECT_RV_PREFIX" --out-dir "$ECT_BUILD_ROOT/ae350" --no-install-itcm
```

## ESP32 A/B：安装匹配 ESP-IDF，分别构建

安装、环境激活及 `-B` 参数以 **ESP-IDF v6.1** 官方源码文档和当前项目配置为准。可将 SDK 放到自己的用户目录；如需要逐提交复现 B 板历史构建，使用上表固定提交，并同时保留其递归子模块。

```bash
# 首次安装示例：保留已有其他版本，另建 v6.1 目录。
mkdir -p "$HOME/esp"
git clone --recursive --branch v6.1 https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.1"
export IDF_PATH="$HOME/esp/esp-idf-v6.1"
cd "$IDF_PATH"
./install.sh esp32s3
. ./export.sh
idf.py --version
```

已经安装时直接设置正确的 `IDF_PATH` 并激活其 `export.sh`；不要覆盖其他 SDK 或全局 Python。上述路径是新机器可选布局，不是工程写死的来源目录。

A 板从 [esp32/a_board](../esp32/a_board/) 构建，使用项目自带配置和 ASCII 输出目录：

```bash
cd "$ECT_ROOT/esp32/a_board"
idf.py -D SDKCONFIG="$PWD/config/sdkconfig.esp32s3" \
  -B "$ECT_BUILD_ROOT/esp32-a" build size
```

A 当前配置为 8 MB Flash、DIO/80 MHz、1500 KiB factory、Wi-Fi/BLE，无 PSRAM/OTA。不要按另一块板的物理 Flash 容量随意改分区。`idf.py set-target` 会影响配置，不是已有工程日常构建的必要步骤。

B 板是独立工程，用它自己的 [sdkconfig](../esp32/b_board/sdkconfig)、[sdkconfig.defaults](../esp32/b_board/sdkconfig.defaults) 和组件锁；当前为 16 MB Flash、DIO、单 factory，PSRAM 未启用。其 [build.sh](../esp32/b_board/build.sh) 支持以下两个覆盖变量，避免依赖脚本中的历史 SDK 默认位置：

```bash
cd "$ECT_ROOT"
export B_IDF_PATH="$IDF_PATH"
export B_BUILD_DIR="$ECT_BUILD_ROOT/esp32-b"
bash esp32/b_board/build.sh
```

不要删掉 B 的 `dependencies.lock` 后接受自动升级，也不要把 A 的配置复制到 B。以上只构建，不包含 `flash`、monitor 或任何设备写入；必要的软件回归入口见两板各自 README 和 [验证说明](VALIDATION.md)。

## 网页：锁包、离线恢复与浏览器选择

有网络时，从网页目录按锁安装：

```bash
cd "$ECT_ROOT/apps/web"
node --version
npm ci
npm run build:gzip
npm test
```

`npm ci` 要求 `package.json` 与锁文件一致，并会重建 `node_modules`；不要在其中保存手写代码。仓库也保留了 [离线依赖清单](../third_party/web-dependencies.json) 和对应归档。缺少 `node_modules` 时，可以从仓库根执行以下替代恢复步骤；校验失败或目录已存在时脚本会停止，不覆盖原内容，不联网：

```bash
cd "$ECT_ROOT"
python3 tools/restore_web_dependencies.py
```

离线依赖恢复只恢复 Node 包，不含浏览器二进制。浏览器测试默认使用 `/usr/bin/google-chrome`；要选已有 Chrome/Chromium，给 `WEB_UI_BROWSER` 一个真实可执行文件的绝对路径。可用 `WEB_UI_ARTIFACT_DIR` 选择本次输出目录，然后运行选定入口：

```bash
cd "$ECT_ROOT/apps/web"
: "${WEB_UI_BROWSER:?set the absolute path of an existing Chrome or Chromium}"
export WEB_UI_BROWSER
export WEB_UI_ARTIFACT_DIR="$ECT_BUILD_ROOT/web-browser"
npm run test:strings-browser
npm run test:fdc-browser
```

默认的历史浏览器入口与当前维护线有部分已知预期失配，见 [网页 README](../apps/web/README.md)；不能以搭建成功宣称全部历史 suite 通过。生成的新网页不会自动替换固件内嵌页面，也不会自动刷入设备。

## Python 主机工具：虚拟环境、Tk、串口和 USB

从仓库根创建自己的虚拟环境，按要使用的工具安装依赖；不要复制另一台机器的 `.venv`：

```bash
cd "$ECT_ROOT"
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r host/pcm_capture/requirements.txt
python -m pip install -r host/b_board_debugger/requirements.txt
python -m pip install -r tools/esp32_midi_debug/requirements.txt
```

无需使用全部工具时只安装对应 requirements。Tk 来自 Ubuntu `python3-tk`，不是 PyPI 包；PyUSB 还需要系统 `libusb-1.0-0`。Qt 界面需要可用图形会话和平台库。真实 USB/串口访问需要设备节点权限；按本机发行版和设备规则配置，不把以 root 运行 GUI 当作常规方案。

B debugger 可先离线启动：`python -m host.b_board_debugger --demo`。PCM 程序的入口和后端行为见 [PCM README](../host/pcm_capture/README.md)，串口/MIDI 调试见 [工具说明](../tools/esp32_midi_debug/README.md)。PCM GUI 的 Start 可发送既有采集启动请求；MIDI 工具 `--execute` 会发送数据。这些不是只看界面时应自动执行的动作。真实协议、端口和波特率必须匹配当前固件；示例文本 MIDI 协议尚不能视为已确认的固件能力。

## Android：使用仓库锁定工具，不依赖他人安装目录

从 Android 子项目运行自己的 [setup.py](../apps/android/tools/setup.py)。它核对 [固定工具链锁](../apps/android/tools/toolchain-lock.json) 的 SHA，下载到该子项目 `.toolchain/`，不需要系统 Gradle：

```bash
cd "$ECT_ROOT/apps/android"
unset FPGA_APP_TOOLCHAIN
python3 tools/setup.py
python3 tools/test.py
python3 tools/build.py
```

已有相同锁定版本工具链时，可把 `FPGA_APP_TOOLCHAIN` 设为自己的现有目录；不要照抄历史 README 的个人路径。`setup.py` 的首次下载需网络并适用 Android SDK 条款。项目脚本使用 Python 3.12 的安全解包接口，按上表基线准备 Python。APK 构建不要求 ADB、模拟器、KVM 或实体手机。

可选模拟器另有 [preview-toolchain-lock.json](../apps/android/tools/preview-toolchain-lock.json)，从子项目运行 `python3 tools/preview.py setup` 下载所需组件。运行前按 [Android 官方 Linux 加速说明](https://developer.android.com/studio/run/emulator-acceleration) 准备 KVM/虚拟化能力与访问权限；虚拟机内还需宿主支持相应虚拟化。该工具链约需额外 GB 级空间，不含在基础 APK 构建依赖里。运行、mock 网络和停止命令见 [Android README](../apps/android/README.md)。不要把模拟器回归视为真实 BLE、SPI 或声音验收。

## KS/FM历史数据的离线重分析

资料入口为[历史实验](../experiments/ks_fm_20261102/README.md)。仅做SHA校验使用Python标准库；重新分析需要NumPy，基础分析与图稿还需PySide6 Essentials、pyqtgraph及Noto Sans CJK SC/DejaVu Sans字体。原记录的采集分析环境为Python3.12.3、NumPy2.5.4、PySide6 Essentials/shiboken6 6.12.0、pyqtgraph0.14.0；另一次打包使用Python3.12.14、NumPy2.3.5、Pillow12.3.0、pypdf6.10.0。两个环境不应拼成一个已验证的新锁文件。

准确版本与角色见[运行环境.json](../experiments/ks_fm_20261102/05_版本与校验/运行环境.json)，按[离线说明](../experiments/ks_fm_20261102/06_离线复现/运行说明.md)准备独立venv和新输出目录。记录里的原主机executable路径只表示历史来源，不是复刻安装路径。原始音频/RAW与图稿已逐SHA保全；本轮没有重跑分析，也不声明新机器逐像素图稿或重新采集已复现。

## 实物复刻：以当前输入和接线资料为准

核心是 Tang Mega 138K/GW5AST-138C、A 板 ESP32-S3 控制器、实际音频输出和已确认供电/公共地；激光竖琴扩展增加 B 板 ESP32-S3、十二路光电输入和四个 EC11，FDC2214、CH569 USB3 PCM 链按所需功能准备。仓库不是一份已经覆盖所有机械、电源、电路和可采购料号的完整制造 BOM。

接线首先读取 [系统架构](SYSTEM_ARCHITECTURE.md)、[A 板 README](../esp32/a_board/README.md)、[B 板接线](laser_harp/hardware-wiring.md)、[GPIO 交接包](laser_harp/ec11-gpio-handoff-20261030/README.md) 和 [PCM 接线/格式](../host/pcm_capture/PCM_CAPTURE_GUIDE.md)。FPGA 引脚以 `.gprj` 实际启用的 CST/SDC 与当前 RTL 为准，A/B 引脚以各自配置和 pinmap 为准；不要把排针编号当成 GPIO 编号，或把历史接线直接套到现有板卡。

当前 A→FPGA 单路连接为 PMOD1/J8，SPI mode 0、10 MHz、MSB-first、3.3 V 单端并共地；PMOD0/J9 不因此自动成为另一条 ESP32 总线。两板独立 USB 供电的方案不并接 PMOD 3.3 V 电源针。A/B MIDI、BLE、HTTP 地址、帧格式及运行能力应使用现有固件/协议契约，尤其不能把 App/mock 具备某个按钮解释成 FPGA 已实现对应能力。设备编程仍需单独授权，本文没有提供自动烧录步骤。

## 官方资料与本次覆盖范围

- [ESP-IDF v6.1 官方 Linux 安装源码文档](https://github.com/espressif/esp-idf/blob/v6.1/docs/en/get-started/linux-macos-setup-legacy.rst)：前置包、install/export 和独立构建目录用法已按该版本核对。具体组件 API 继续以项目锁及 v6.1 源码为准。
- [npm ci 官方说明](https://docs.npmjs.com/cli/v11/commands/npm-ci/)：锁安装与重建 `node_modules` 行为；并非本项目 npm 小版本锁。
- [Playwright 浏览器官方说明](https://playwright.dev/docs/browsers)：浏览器选择的通用要求；项目实际入口、默认路径与环境变量以锁定的脚本为准，本次没有另下载浏览器。
- [Python 3.12 venv](https://docs.python.org/3.12/library/venv.html)：虚拟环境创建/激活；pip 包版本仍以本项目 requirements 为准。
- [Gowin 官方 EDA 下载](https://www.gowinsemi.com/en/support/download_eda/)：安装和授权入口；本次不把官网下载页当前默认版本当成 V1.9.12.04 的替代，版本细节以本机匹配版本帮助、项目和所选官方手册为准。
- [Android SDK 平台说明](https://developer.android.com/tools/releases/platforms) 与 [模拟器加速说明](https://developer.android.com/studio/run/emulator-acceleration)：官方工具背景与 Linux KVM 要求；本项目确切包版本和下载 SHA 优先取仓库锁。

除已明确引用的本地配置/锁和只读主机版本外，本次没有重装环境或重跑构建。手册、Gowin 授权、外部 RV32 multilib、目标 USB/串口权限和硬件连接仍需由复刻机器逐项确认。
