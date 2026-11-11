# FM 参数、音色与网页修复合入主工程

2026-11-02。用户授权将独立 FM 实验的成果合回 `ae350_fpga_menu_lab_20261028`。来源为同级 `ae350_fm_timbres_lab_20261101T165829`；原始旧主库保持不动。此次按文件核对和合入，没有 Git 暂存、提交、推送或设备写入。

## 当前功能

- 控制路径为 ESP32 请求 → 原有模板 FIFO → AE350 校验并采用下一次起音模板 → FPGA prepared JT51 执行。
- 仅支持 JT51 后端 1；保留原预置 0..2，增加电钢琴 `electric_piano_tine`（3）、金属钟声 `metal_bell`（4），共 46 项共享参数。现有声部保留起音快照，新音符采用新配置。
- 并行架构下，网页按客户端 MIDI 通道选择 FM（通道 3，API 下标 2）；不要求全局 mode 为 2。仍校验 CPU、FM、合同、会话、故障和切换状态。
- 应用预置等待新的匹配 native completed/result，随后复用健康的原 Web 会话，保留 FM 通道。真正新建会话时的默认 KS 策略保持。失败、超时和复位不伪报成功。

## 合入方法与主工程内容保留

核对时，主工程的 FDC 工作已先行从该实验副本引入 31 个路径的 FM 合并结果，包括合同、AE350 编译器、ESP32 参数通道及网页两处修复。本次逐项核对已有结果并补齐缺失的验证代码；没有用旧实验工程覆盖当前主树。

保留主工程的手机 App 共用 HTTP/BLE 控制核心、来源隔离、CH569、SUB/APB 优化、AB13/Y12 旧开关释放、现有 SDC 渲染器，以及正在进行的 FDC 电钢琴工作。没有回迁旧 FPGA/IP/ITCM 镜像、构建输出或暂停中的时序分析脚本。

新增和更新的测试位于 `sim/tb/`、`sim/config/`、`control_migration/esp32/esp32s3/tests/host/` 和 `control_migration/esp32/web-ui/tests/`。FM HTTP 测试 runner 已适配主工程现有的共用控制核心依赖，没有修改生产 HTTP 实现。

## 验证

- 主工程真实 C/FIFO/APB/CDC/JT51 联合仿真通过 5,000,060 项检查；电钢琴、钟声均有非零 PCM，覆盖 next-note、持音、CRC、后端拒绝、8 声部、Panic 与复位。此为主机编译 C 加真实 RTL/模型的联合仿真，未执行硬核 AE350 指令。
- SPI 位级通道通过 43,107 项检查、85 帧；覆盖模板提交、状态返回、溢出拒绝和复位丢弃。
- ESP32 控制端 7 组 host 测试通过；真实 FM HTTP 处理器验证预置 3/4、参数和 JT51 请求，并拒绝越界与不支持的后端。
- 按现有锁文件安装网页本地依赖，package 与 lock 字节保持；单 HTML 构建通过。网页全套 530/536 通过，6 个失败均位于未修改的 `fdc-controller.test.mjs`，合入前实际主树快照独立复现同样 6 项。FM 并行能力和预置会话回归均通过。

命令入口：

```text
python3 -B sim/config/run_fm_ae350_parameters.py --run <unique-run>
python3 -B sim/config/run_prepared_spi_template.py --run <unique-run>
python3 -B control_migration/esp32/esp32s3/tests/host/run_ae350_host.py --run <unique-run>
python3 -B control_migration/esp32/esp32s3/tests/host/run_ae350_fm_http.py --run <unique-run>
# 在 control_migration/esp32/web-ui 中：
npm ci --ignore-scripts --no-audit --no-fund
npm run build
npm test
```

本次结果和可恢复备份位于 `sim/results/fm_merge_main_20261102/`；联合仿真、SPI 与 ESP32 的实际运行记录由其中 `result.json` 引用。`main-before-merge.tar.gz` 保存合入前 1,712 个实际文件并逐文件验证，恢复时应按 `applied-files.json` 选择路径，避免覆盖后来其他任务的改动。

## 实现与硬件边界

本次操作没有修改任何 FPGA 构建输入，因此未另启综合、PnR 或定时检查。主工程另项 FDC 工作的新固件、IP 和实现仍由原任务验收；实验副本的旧网表、位流与硬件测试不作为当前主树的实现证明。

本次没有重新构建或烧录 ESP32 固件。保留主工程自己的版本标识及生成页面，实验副本已实板验证的 `ae350-fm-web2-20261102` 仅作为来源证据。原 FM 时序优化任务继续暂停，不宣称时序收敛。
