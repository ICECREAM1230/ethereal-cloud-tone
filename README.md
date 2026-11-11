# 空灵云音 · Ethereal Cloud Tone

基于高云 Tang Mega 138K（GW5AST-138C）与 AE350 的电子音乐合成器完整源码工程。包含减法合成器、KS 物理建模、FM 声音路线，以及 ESP32 A/B 控制器、Android 应用、网页控制台、PCM 采集和测量工具。

[项目介绍](docs/PROJECT_OVERVIEW.md) · [系统与FPGA架构](docs/ARCHITECTURE.md) · [验证记录](docs/VALIDATION.md)

## 工程入口

| 组件 | 入口 |
| --- | --- |
| FPGA、AE350 与音频逻辑 | [fpga/README.md](fpga/README.md)，主工程 [ethereal_cloud_tone.gprj](fpga/ethereal_cloud_tone.gprj) |
| 当前 ESP32-S3 A 板控制器 | [esp32/a_board/README.md](esp32/a_board/README.md) |
| ESP32-S3 B 板传感器及 MIDI | [esp32/b_board/README.md](esp32/b_board/README.md) |
| Android 客户端 | [apps/android/README.md](apps/android/README.md) |
| 网页控制台 | [apps/web/README.md](apps/web/README.md) |
| B 板调试台及 PCM 上位机 | [host/](host/) |
| FDC2214 调试 | [debug/fdc2214_esp32s3/README.md](debug/fdc2214_esp32s3/README.md) |
| 延迟测量代码、测试与数据示例 | [AE350_UART_Latency_T04/](AE350_UART_Latency_T04/) |
| AE350 控制服务参考及测试 | [integration/ae350_latest/README.md](integration/ae350_latest/README.md) |

## 使用

克隆仓库后先运行只读检查：

```sh
python3 -B tools/verify_project.py
```

FPGA 使用 Gowin V1.9.12.04，器件和源文件顺序以 `.gprj` 为准。官方构建方式：

```sh
cd fpga
gw_sh scripts/run_control_project.tcl syn
gw_sh scripts/run_control_project.tcl pnr
```

需要日志窗口和后台运行时，使用 `python3 -B scripts/manage_control_build.py launch syn`；综合成功后再执行同一入口的 `launch pnr`。设置 `GOWIN_SH` 可指定本机的 Gowin 启动器。工具链、SDK 与许可证由使用者在本机准备，构建不会自动写入硬件。

A/B 固件、Android、网页与主机工具的依赖和测试命令见各自说明。保留锁定依赖、测试合同、厂商 IP、初始化数据和 AE350 必需的程序镜像；Android 开发签名、旧固件安装包、历史实现缓存和个人审计日志未纳入仓库。

## 版本与验证范围

此仓库从已保存的完整工程建立，完成当前项目命名及许可统一，并重新检查源文件和工程依赖。FPGA 控制算法、时钟、引脚及数值表保持；ESP32 配置前缀统一为 `ECT_`；DDS TOML 文档仍使用 `format = 1`，存储格式保持。

`fpga/firmware/ae350/images/control.bin` 与生成 IP 内的 64 KiB ITCM 数据保留匹配的程序镜像，源文件中的许可和说明更新没有被写成一次新的固件编译。`fpga/firmware/esp32/` 是历史 FPGA 快照的配套参考，当前 A 板维护入口是 `esp32/a_board/`。

发布检查和本轮构建状态见 [docs/VALIDATION.md](docs/VALIDATION.md)。专项软件测试、官方综合、布局布线、STA 与实板联合验收分别记录；旧构建和旧板测不代表当前版本通过这些阶段。

## 许可

本项目自有及已获重新授权的代码采用 [MIT](LICENSE)，Copyright (c) 2026 ICECREAM1230。保留源文件中原有作者署名。

JT51、DaisySP、USB、HDMI、GCC 运行库和高云 IP 等独立第三方组件继续遵循各自许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；顶层 MIT 不覆盖这些组件的独立条款。
