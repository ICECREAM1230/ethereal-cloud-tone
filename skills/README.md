# 高云 FPGA 开发 Skills

这里收录 33 个当前技能及其脚本、参考资料和必要运行资源，用于理解与修改 Gowin FPGA 工程、RTL、约束、IP、验证，以及 Gowin/AE350 固件配套工作。所有技能彼此同级，跨技能相对引用保持有效。入口可从 [fpga](fpga/SKILL.md)、[gf](gf/SKILL.md)、[工程导航](gowin-project-navigation/SKILL.md)、[Arora V RTL](gowin-arora-v-rtl/SKILL.md) 和 [IP 生成](gowin-ip-generation/SKILL.md) 按任务选择。

这是普通仓库目录。读取或下载本目录不会安装、启用技能、修改全局配置或运行工具。来源快照日期为 2026-11-12：31 项当时可用，`gf-pcb`、`gf-release` 当时明确未启用；本包保留两项内容和状态记录，不替使用者启用它们。目标主机的实际配置仍为准。不要因技能目录存在就执行其中的生成、构建或设备操作。

## 技能目录

| 技能 | 来源状态快照 | 入口 |
| --- | --- | --- |
| `embedded-firmware` | 来源中可用 | [SKILL.md](embedded-firmware/SKILL.md) |
| `fpga` | 来源中可用 | [SKILL.md](fpga/SKILL.md) |
| `gf` | 来源中可用 | [SKILL.md](gf/SKILL.md) |
| `gf-architect` | 来源中可用 | [SKILL.md](gf-architect/SKILL.md) |
| `gf-build` | 来源中可用 | [SKILL.md](gf-build/SKILL.md) |
| `gf-cocotb` | 来源中可用 | [SKILL.md](gf-cocotb/SKILL.md) |
| `gf-errors` | 来源中可用 | [SKILL.md](gf-errors/SKILL.md) |
| `gf-expand` | 来源中可用 | [SKILL.md](gf-expand/SKILL.md) |
| `gf-formal` | 来源中可用 | [SKILL.md](gf-formal/SKILL.md) |
| `gf-fusesoc` | 来源中可用 | [SKILL.md](gf-fusesoc/SKILL.md) |
| `gf-ip` | 来源中可用 | [SKILL.md](gf-ip/SKILL.md) |
| `gf-ip-detect` | 来源中可用 | [SKILL.md](gf-ip-detect/SKILL.md) |
| `gf-learn` | 来源中可用 | [SKILL.md](gf-learn/SKILL.md) |
| `gf-learn-ctx` | 来源中可用 | [SKILL.md](gf-learn-ctx/SKILL.md) |
| `gf-lint` | 来源中可用 | [SKILL.md](gf-lint/SKILL.md) |
| `gf-pcb` | 来源中未启用 | [SKILL.md](gf-pcb/SKILL.md) |
| `gf-pinmap` | 来源中可用 | [SKILL.md](gf-pinmap/SKILL.md) |
| `gf-plan` | 来源中可用 | [SKILL.md](gf-plan/SKILL.md) |
| `gf-pnr` | 来源中可用 | [SKILL.md](gf-pnr/SKILL.md) |
| `gf-project` | 来源中可用 | [SKILL.md](gf-project/SKILL.md) |
| `gf-protocols` | 来源中可用 | [SKILL.md](gf-protocols/SKILL.md) |
| `gf-release` | 来源中未启用 | [SKILL.md](gf-release/SKILL.md) |
| `gf-router` | 来源中可用 | [SKILL.md](gf-router/SKILL.md) |
| `gf-sim` | 来源中可用 | [SKILL.md](gf-sim/SKILL.md) |
| `gf-summary` | 来源中可用 | [SKILL.md](gf-summary/SKILL.md) |
| `gf-synth` | 来源中可用 | [SKILL.md](gf-synth/SKILL.md) |
| `gf-tui` | 来源中可用 | [SKILL.md](gf-tui/SKILL.md) |
| `gf-viz` | 来源中可用 | [SKILL.md](gf-viz/SKILL.md) |
| `gowin-arora-v-rtl` | 来源中可用 | [SKILL.md](gowin-arora-v-rtl/SKILL.md) |
| `gowin-ip-generation` | 来源中可用 | [SKILL.md](gowin-ip-generation/SKILL.md) |
| `gowin-project-navigation` | 来源中可用 | [SKILL.md](gowin-project-navigation/SKILL.md) |
| `soc-integration` | 来源中可用 | [SKILL.md](soc-integration/SKILL.md) |
| `tb-best-practices` | 来源中可用 | [SKILL.md](tb-best-practices/SKILL.md) |

## 使用与环境

先读取所选 `SKILL.md`，再按其链接加载所需参考。需要软件调用时，确认该任务的授权范围、实际 `.gprj`、器件、工具版本及输入。构建产物不代表允许烧录硬件。

Linux 文档中的 `SKILLS_ROOT` 表示本目录的绝对路径，`OSS_CAD` / `GOWIN_SH` 表示已核对的启动器绝对路径。可在任务自己的 shell 中设定：

```bash
# 从仓库根目录执行；只设置本 shell 的变量，不安装或启用技能。
export SKILLS_ROOT="$PWD/skills"
export OSS_CAD="$SKILLS_ROOT/host_support/oss-cad"
export GOWIN_SH="$SKILLS_ROOT/host_support/gw_sh"
```

`host_support/` 保留两个小启动器供检查和按主机选择。`gw_sh` 使用 `GOWIN_IDE`，未设置时沿用 `$HOME/Gowin/V1.9.12.04/IDE`；`oss-cad` 使用 `OSS_CAD_SUITE`，未设置时沿用 `/opt/eda/oss-cad-suite/current`。这些是来源环境的约定，使用前应把变量指向现有、版本匹配的本地安装。运行环境只传给子进程。启动器还依赖主机上匹配的 FreeType/Qt 库；复制文件不证明这些依赖可用。

Gowin IDE、厂商插件/模型、设备数据库、许可证、OSS CAD Suite、SDK/BSP 与硬件均不随包提供。cocotb、FuseSoC/Edalize、ModelSim/Questa、KiCad 等只在选择对应流程时需要。官方 Gowin `.gprj` 继续使用正式 Gowin 流程，不因本包存在而改用开源工具。技能不会自动安装缺失软件。

### 官方手册需从本机获取

8 套官方手册的 PDF 及完整 Markdown 转换正文不随公开包提供，其版权和再分发限制仍属于 Gowin。原有 `manuals/markdown/` 路径放置本项目编写的短定位页，保留引用页码并清楚标注正文外置；没有伪造 PDF 文件。

[手册索引](gowin-arora-v-rtl/references/manuals/manifest.json) 保留准确版本、日期、PDF SHA-256、页数与官方来源，[转换索引](gowin-arora-v-rtl/references/manuals/conversion.json) 保留原转换 SHA。请从已获授权的本地 Gowin `IDE/doc` 或官方入口取得匹配版本，把 `GOWIN_MANUAL_DIR` 指向该外部目录。读取器接受其下 `ID.pdf` 或 `pdf/ID.pdf`，可选完整转换为 `markdown/ID.md`；不会自动下载或复制文件。没有匹配文件时明确报缺失，不能把定位页当正文引用。具体命令见 [手册读取](gowin-arora-v-rtl/references/sources.md)。官方网站链接可能更新版本，部分本次查询超时或返回 403；逐项情况在索引中披露。

### native IP 桥的适用范围

`gowin-ip-generation` 的 Python/shell 适配器、C++ 源码和配套的本机自编 `.so` 保留；未镜像 Gowin/Qt 运行库。它绑定 Linux x86_64、Gowin V1.9.12.04、Qt 5.15 和来源安装路径。该路径在相关源文件及已编译库内一致保留。设定 `GOWIN_IDE` 不能单独使这套桥跨机可用；另一安装需要协调适配、重编和实际产物验证，见 [公开包与 ABI 限制](gowin-ip-generation/references/public-package.md)。

IP 实例库方面，`gf-ip` 没有随附已经验收的通用 RTL/TB/formal 库；示例目录与目录表不会被当成可直接使用的 IP 库。

## 来源与许可

仅收录当前 33 项；不含历史版本、原机全局配置或个人源目录映射。`embedded-firmware` 与 `soc-integration` 保留各自的 MIT `LICENSE`、原作者 `chuanseng-ng` 和固定上游提交的 `SOURCE.json`。其他既有署名与适用条款继续保留；不得把仓库对自有内容的 MIT 许可理解为对 Gowin 文档、软件、商标或其他第三方内容重新授权。官方手册仅提供获取线索；本包未分发正文。源码记录和原机生成测试结论只代表当时的来源证据，不代表本公开适配包已在另一主机验收。

## 只读完整性校验

```bash
python3 -B skills/_tools/verify_bundle.py
```

从仓库根目录运行。校验器依据 `_metadata/resources.json` 核对公开资源的完整集合、字节数、SHA-256 与 Git 可保存的可执行位，同时检查 33 项入口、31/2 状态和跨技能链接。它不依赖原主机路径，不安装、不启用、不执行技能脚本、EDA 或硬件操作，也不写校验报告到仓库。`--verify` 与该校验无关：手册读取器的 `--verify` 另外验证外部手册原文。

本次公开适配只做文件整理、格式/静态检查及上述只读校验；未运行 IP 生成、仿真、综合、布局布线或设备写入。适配清单与来源摘要在 `_metadata/`，外部工具的运行可用性、native ABI、厂商模型及实板行为仍需在获授权的目标环境另行验证。
