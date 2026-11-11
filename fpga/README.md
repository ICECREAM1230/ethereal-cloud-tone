# 空灵云音 FPGA 工程

目标为 Gowin GW5AST-LV138PG484AC1/I0（Tang Mega 138K），数据库 `gw5ast138c-007`，工具版本 Gowin V1.9.12.04，顶层 `control_lab_top`。当前器件、源文件顺序及启用状态以 [ethereal_cloud_tone.gprj](ethereal_cloud_tone.gprj) 为准。

`rtl/` 保存减法合成器、KS、FM、控制、音频、显示和传感器逻辑；`ip/` 保存厂商 IP 及初始化数据；`firmware/ae350/` 保存处理器源码及当前镜像；`constraints/` 为 CST/SDC；`scripts/` 为构建和输入核验入口。

## 构建

在本目录执行：

```sh
gw_sh scripts/run_control_project.tcl syn
gw_sh scripts/run_control_project.tcl pnr
```

后台构建及独立日志窗口：

```sh
python3 -B scripts/manage_control_build.py launch syn
python3 -B scripts/manage_control_build.py verify
python3 -B scripts/manage_control_build.py launch pnr
```

仅在综合成功且输入未变化后启动布局布线。`GOWIN_SH` 可指定启动器；否则使用 PATH 中的 `gw_sh` 或用户目录的 `.local/bin/gw_sh`。过程设置保存在 `impl/ethereal_cloud_tone_process_config.json`，生成结果和日志不纳入源码提交。关闭日志窗口不会停止后台构建。

工程输入、IP、初始化文件及必需运行库均随本工程提供。`firmware/ae350/images/control.bin` 为匹配当前生成 IP 的 55,228 字节镜像；`scripts/verify_ae350_itcm.py` 核对生成网表中的 32 个 SP 与 64 KiB ITCM。未因许可说明更新而重新生成受保护 IP。外部 RV32 GCC 用于 `scripts/build_control_firmware.py`，实际编译参数见该脚本。

当前 ESP32-S3 A 板维护入口为 [../esp32/a_board/](../esp32/a_board/)，`firmware/esp32/` 仅为原配套源码参考。项目的验证状态见 [../docs/VALIDATION.md](../docs/VALIDATION.md)，没有在本次源码发布中编程硬件。

## 许可

项目自有及已获授权部分采用 [MIT](LICENSE)，保留作者署名。独立第三方组件的许可见 [licenses/](licenses/) 和 [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。
