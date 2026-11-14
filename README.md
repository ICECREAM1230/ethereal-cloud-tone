# 空灵云音 · Ethereal Cloud Tone

基于高云 Tang Mega 138K（GW5AST-138C）与 AE350 的电子音乐合成器工程。包含减法合成器、KS物理建模、FM声音路线，ESP32 A/B控制器、Android应用、网页控制台、PCM采集工具，以及配套IP、开发skills、历史实验数据和配套资料。

[项目介绍](docs/PROJECT_OVERVIEW.md) · [技术报告](docs/technical/README.md) · [系统与FPGA架构](docs/ARCHITECTURE.md) · [完整环境搭建指南](docs/ENVIRONMENT.md) · [验证与已知问题](docs/VALIDATION.md)

## 仓库内容与入口

| 内容 | 入口与范围 |
| --- | --- |
| FPGA与AE350 | [fpga/](fpga/README.md)，主工程 [ethereal_cloud_tone.gprj](fpga/ethereal_cloud_tone.gprj) |
| IP、初始化与运行库 | [IP覆盖说明](docs/IP_INVENTORY.md)：73项IP文件、30组目录；`.ipc/.v/.vo/.mod`、ITCM、LUT与必需运行库已收录 |
| 当前ESP32 A板 | [esp32/a_board/](esp32/a_board/README.md)，ESP32-S3配套控制器 |
| ESP32 B板 | [esp32/b_board/](esp32/b_board/README.md)，独立激光竖琴、编码器与MIDI输入 |
| 手机与网页 | [Android](apps/android/README.md)、[网页](apps/web/README.md) |
| 上位机与调试 | [host/](host/README.md)、[FDC调试](debug/fdc2214_esp32s3/README.md)、[串口MIDI工具](tools/esp32_midi_debug/README.md) |
| 开发skills | [skills/](skills/README.md)，33个当前版本同级存放，含来源中2个未启用项；不自动安装或启用 |
| 技术报告 | [正式技术报告](docs/technical/README.md)，2026-11-14 Word版，132页，含目录、导航及参考文献 |
| 可用产物 | [artifacts/](artifacts/README.md)，冻结FPGA参考位流/报告、A/B固件、APK、网页及明确版本的AE350软件参考 |
| KS/FM实测数据 | [experiments/ks_fm_20261102/](experiments/ks_fm_20261102/README.md)，十段历史实测的WAV/PCM/RAW、分析、图稿和代码闭包 |
| 延迟测量与软件参考 | [T04/AT04](AE350_UART_Latency_T04/)、[AE350参考](integration/ae350_latest/README.md) |

详细的收录范围、去重位置和外部依赖见 [相关文件索引](docs/RELATED_FILES.md)。

## 复刻环境要求

先选择只阅读、构建软件，还是复刻硬件；不必为了运行网页或上位机安装全部EDA工具。

| 部分 | 版本或要求 |
| --- | --- |
| 已记录主机基线 | Ubuntu 24.04 / Linux x86_64、Python 3.12.3、CMake 3.28.3；这些是环境记录，不是测试过的最低配置 |
| FPGA | Gowin V1.9.12.04、有效授权；器件 `GW5AST-LV138PG484AC1/I0`，数据库 `gw5ast138c-007` |
| AE350重编 | RISC-V GCC 13.2.0来源记录，RV32IM/ILP32 soft-float multilib；仅重编CPU固件需要，使用已收录镜像不需要 |
| ESP32 A/B | ESP-IDF v6.1；分别使用各自配置。B板历史锁定提交 `fff9895c82d744c7237be8847347bdd1b07c6643`，组件版本以 `dependencies.lock` 为准 |
| 网页 | Node.js ≥22；Playwright 1.63.0、Terser 5.51.2及其他依赖由 `package-lock.json` 锁定；浏览器测试另需Chrome/Chromium |
| Python上位机 | Python 3.12基线；按所选工具的requirements安装PySide6、PyUSB、pyserial；Tk和libusb由系统提供 |
| Android | 仓库脚本锁定Temurin JDK 17.0.20.1+1、SDK Platform 36 revision 2、Build Tools 36.0.0；手机最低Android 8/API26，无需系统Gradle |
| 历史数据重分析 | NumPy、PySide6 Essentials、pyqtgraph和字体；精确历史版本见[数据包环境记录](experiments/ks_fm_20261102/05_版本与校验/运行环境.json)，校验数据本身只需Python标准库 |
| 可选工具 | 桌面日志窗口需gnome-terminal；RTL专项按需准备OSS CAD Suite；Android模拟器另需KVM，均不是单纯查看代码的前提 |

完整的系统包、SDK安装、版本证据和独立输出目录步骤见 [ENVIRONMENT.md](docs/ENVIRONMENT.md)。本项目没有验证最低CPU、内存或磁盘门槛；SDK、EDA、Android工具和构建缓存需要另留空间。当前依赖范围锁定与“逐字节重现所有历史产物”是不同目标；Windows/WSL未完成同等复刻验证。

## 环境搭建与快速检查

### 1. 获取源码与主机基础工具

```bash
# Ubuntu：先准备Git及基础工具，需要本机管理员权限。
sudo apt-get update
sudo apt-get install git wget build-essential flex bison gperf \
  python3 python3-pip python3-venv python3-tk cmake ninja-build ccache \
  libffi-dev libssl-dev libusb-1.0-0 libfreetype6 libfontconfig1 dfu-util gnome-terminal

git clone https://github.com/ICECREAM1230/ethereal-cloud-tone.git
cd ethereal-cloud-tone
export ECT_ROOT="$PWD"
export ECT_BUILD_ROOT="/tmp/ect-build"
mkdir -p "$ECT_BUILD_ROOT"

# 标准库只读检查，不启动构建、不访问设备。
python3 -B tools/verify_project.py
python3 -B tools/verify_related_files.py
```

输出目录使用ASCII路径。不要把另一台机器的`.venv`、SDK安装目录、旧绝对路径或开发签名直接复制成环境。

### 2. FPGA工具与IP

从Gowin官方渠道准备V1.9.12.04和适用授权，将`GOWIN_IDE`指向自己的IDE目录。需要本仓库启动器时，先核对并保留已有同名文件，再按[完整指南](docs/ENVIRONMENT.md#gowin安装位置授权与-cli-启动器)设置：

```bash
# 示例安装布局；请改成自己的实际IDE路径。
export GOWIN_IDE="$HOME/Gowin/V1.9.12.04/IDE"
install -Dm755 skills/host_support/gw_sh "$HOME/.local/bin/gw_sh"
export PATH="$HOME/.local/bin:$PATH"
export GOWIN_SH="$HOME/.local/bin/gw_sh"
command -v gw_sh
```

IP及生成数据已在仓库内。厂商软件、器件数据库、许可证和8套官方手册正文需自行准备；[skills手册索引](skills/gowin-arora-v-rtl/references/manuals/manifest.json)保留版本与获取入口。可选native IP桥有固定安装路径/Qt ABI限制，不能只改环境变量就认定可跨机使用。

确需重新实现时，按[FPGA说明](fpga/README.md)执行综合/PnR并检查输入和时序；本次资料上传不启动这些阶段，不编程硬件。

### 3. ESP32 A/B

按[SDK搭建步骤](docs/ENVIRONMENT.md#esp32-ab安装匹配-esp-idf分别构建)安装并激活ESP-IDF v6.1。要逐提交复刻历史构建，应核对固定SDK提交和递归子模块，而不是仅比较“6.1”版本文字。

```bash
# 首次安装：另建SDK目录；已有匹配SDK则跳过clone/install。
mkdir -p "$HOME/esp"
git clone --recursive --branch v6.1 https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.1"
export IDF_PATH="$HOME/esp/esp-idf-v6.1"
cd "$IDF_PATH"
./install.sh esp32s3
. "$IDF_PATH/export.sh"
idf.py --version

# A：8MB Flash，无PSRAM/OTA；采用本项目配置。
cd "$ECT_ROOT/esp32/a_board"
idf.py -D SDKCONFIG="$PWD/config/sdkconfig.esp32s3" \
  -B "$ECT_BUILD_ROOT/esp32-a" build size

# B：独立工程、16MB配置，保留组件锁，不能套用A配置。
cd "$ECT_ROOT"
export B_IDF_PATH="$IDF_PATH"
export B_BUILD_DIR="$ECT_BUILD_ROOT/esp32-b"
bash esp32/b_board/build.sh
```

以上为复刻者按需执行的构建命令，不包含flash。出厂DDS存储镜像与应用镜像分别管理；写入存储镜像会替换对应数据区，不能把普通应用更新视为覆盖User音色的授权。

### 4. 网页、Android和上位机

网页先安装官方Node ≥22，再按锁安装。也可在尚无`node_modules`时执行离线恢复：

```bash
cd "$ECT_ROOT/apps/web"
npm ci
npm run build:gzip
npm test
# 离线替代依赖恢复：从仓库根执行 python3 tools/restore_web_dependencies.py
```

Android使用仓库锁定工具链，首次下载需要网络；不必另外安装Gradle：

```bash
cd "$ECT_ROOT/apps/android"
unset FPGA_APP_TOOLCHAIN
python3 tools/setup.py
python3 tools/test.py
python3 tools/build.py
```

上位机建议使用独立虚拟环境，只安装要用的工具依赖：

```bash
cd "$ECT_ROOT"
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r host/pcm_capture/requirements.txt \
  -r host/b_board_debugger/requirements.txt \
  -r tools/esp32_midi_debug/requirements.txt
python -m host.b_board_debugger --demo
```

Tk不是pip包；USB需要libusb及设备权限。离线依赖不包含浏览器，浏览器测试需设置已有Chrome/Chromium路径；具体选择、模拟器/KVM、各类测试入口见[完整环境指南](docs/ENVIRONMENT.md)。

## 实物复刻需求

核心为Tang Mega 138K、配套ESP32-S3 A板、实际音频输出与稳定供电/公共地。按所选功能增加B板、十二路光电输入、四个EC11、FDC2214或PCM采集链。接口电压、引脚、Flash容量与分区须分别核对，[当前CST](fpga/constraints/control_lab.cst)、[A板配置](esp32/a_board/config/sdkconfig.esp32s3)和[B板接线](docs/laser_harp/hardware-wiring.md)为实际依据。A板当前使用单路PMOD1/J8，不推断第二条控制总线。

完整系统架构、协议和复刻边界见[系统架构](docs/SYSTEM_ARCHITECTURE.md)；本仓库不是完整机械、电源和可采购料号制造BOM，模拟器与host测试也不代表实板出声。

## 产物、版本与验证

[artifacts](artifacts/README.md)分别标识2026-11-04冻结FPGA参考位流、本次A板/Android实际构建和独立AE350参考。历史FS仍有时序违例，不能作为当前源码的新实现或STA闭合证明。KS/FM录音属于2026-11-02，不能据此声称当前main已完成新板测。

本次源代码统一命名与MIT许可，FPGA算法/引脚/时钟及数值表保持，DDS文档仍用`format = 1`。已有软件回归结果、历史测试失配和未执行项在[VALIDATION.md](docs/VALIDATION.md)逐项记录。用户已取消后续综合/PnR；本轮补文件及文档没有重新构建或写入设备。

## 许可

项目自有及已获重新授权的代码采用[MIT](LICENSE)，Copyright (c) 2026 ICECREAM1230。独立第三方组件、skills原有署名及厂商资源继续适用各自条款，详见[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。官方完整手册因再分发限制留在本地，只公开获取线索。
