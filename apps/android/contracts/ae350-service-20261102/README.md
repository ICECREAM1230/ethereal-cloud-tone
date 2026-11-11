# AE350 服务端冻结合同与 C 夹具

2026-11-02。用于手机 App 接入当前 AE350 MAIN 配套 ESP32-S3 服务。此目录保存真实服务端 C 核心、编译器发现的最小传递依赖、原始主机测试及精确 JSON；无需访问原固件绝对路径即可复编译。没有修改固件、网页、FPGA 或 SDK，没有发送 HTTP／BLE／串口请求，没有设备操作。

`source/` 的生产文件与 SDK boundary stubs 保持来源原字节。`provenance.json` 记录每个来源路径、长度、复制前后 SHA256、编译依赖集合、Git 基线、编译器及 tomlc17 的固定提交和许可。来源仓库当时为 `main`，没有 HEAD，内容为未跟踪文件；因此以实际文件 SHA256 为冻结身份。App 主仓当时 HEAD 为 `c654e4bc3e74d44da6634d00ac73c1a569f2e78c`，已有的其他修改保持。

`manifest.json` 绑定本目录的完整文件集合与 SHA256，并绑定 `../../tools/test_ae350_service_capture.py`。82 份来源文件中有 76 份为真实编译依赖，其余为来源许可、vendor 说明、原交接文档和 `voice_alloc.c`／`owner_local.inc.h` 语义证据；没有整树固件、二进制或构建缓存。`source/LICENSE` 和 `source/main/vendor/tomlc17/LICENSE` 保留原许可，生产文件中的作者与版权注释保持。冻结时所有文件在复制前后重复读取一致，编译器依赖集合也一致。

## 复验

从 App 根目录执行：

```bash
python3 -B tools/test_ae350_service_capture.py --check-only
python3 -B tools/test_ae350_service_capture.py
python3 -B tools/test_ae350_service_capture.py --verify-origin
```

只需要本机 Python3、`cc`、标准 C library、libm、zlib 和 pthreads。编译使用 `-DCONFIG_FPGA_AE350_CONTROL=1` 和原测试的 HTTP／FreeRTOS stub；没有调用 ESP-IDF。默认复验不读取外部固件目录；`--verify-origin` 是明确选择的额外只读校验，要求当前原始文件仍与冻结字节匹配。其他代理随后修改原固件时，该选项可以报告正常的来源漂移，冻结合同仍可独立复验。

脚本先核验文件集合、字节长度、SHA256、稳定冻结证据及自身绑定，再在新的 `build/ae350-service-capture/run-*/` 编译三个入口。原 `test_http_v2.c` 与当前 `capture_control_api.c` 均保持源字节；App 扩展入口在 `capture/capture_service.c`，include 同一真实 `test_http_v2.c`，调用实际 `control_api_prepare/emit/release`。每份扩展响应重复 emit，确认响应字节和事件发布次数不变。每次复验逐字节比较原始产物，检查通道／身份／参数／故障语义。编译命令、日志、JSON、trace 和 `summary.json` 保留于该次运行目录；只删除该次生成的已知主机可执行文件与空编译临时目录。`--check-only` 不创建或修复文件。

## 数据入口

- `original-core/`：当前固件自带 `tests/host/capture_control_api.c` 原样采集的 12 份 JSON，含全局 SUB0、客户端 KS1 和通道 0/2/1 的回执／采用观察。
- `http-v2/`：原始 `tests/host/test_http_v2.c` 生成的 8 份 JSON；其中 `v2-live-*` 是原测试构造的 native FM 可用观察，**不能当作当前 AE350 native FM 能力**。
- `fixtures/`：App 扩展入口的 42 份实际核心响应与 `requests.tsv`。`trace.json` 为逐请求索引，保持 `request.body=null` 与空字符串的区别，记录原始 code、文件、字节数、SHA256、事件数和观察来源；v2 命令同时保留完整 7 个 uint32 字段。

与 2026-11-01 交接目录的12份原核心夹具比较，11份逐字节一致；仅 capabilities 的 profiles／contract 摘要已在此次冻结前变化。当前 profiles 为 `408ec706674c15671c752ac9e9f741a8d5247ec23d7df4b25f69600347c010ea`，contract 为 `9df9e901e63c6a18ca6bcee6f78b5a035cb1e8aec00ceae43d24ea1fca75bea0`；schema 仍为 `68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73`。这不是复制期间的混合输入；旧交接文档中“当前摘要匹配”的历史结论需要按当前 App 合同重新判断。本任务没有重生成或放宽 native FM 合同身份检查。

`fixtures/capabilities.json` 与 `fixtures/session.json` 是扩展场景入口。`state-initial-client-ks.json` 中会话已被主机 owner 模型接纳：client=0、token=1001、context=9、客户端 midi_channel=1，设备全局编辑器仍为 SUB0。`select-{sub,ks,fm}-{received,pending,adopted}.json` 保留收到回执、owner 尚未采用、显式主机模型采用的三个阶段。

## AE350 通道与编辑器语义

能力扩展为 `cpu_mode.version=1`、`route_only=true`、`channel_routes=true`、`modes=[0,1,2]`。通道选择请求为 `token sequence context 5 channel 0 0`，channel 0/1/2 分别为减法／KS／FM；其中 backend=0。采用确认应使用会话 client 对应的 `clients[client].midi_channel`，并结合 token、当前 context、`parameter_processed`、`result_sequence`、`result_code`、`result_context_epoch`。HTTP 202 中 `stage=received`、`fpga=unverified` 只表示接收，不能单独视为采用。

本场景所有状态的 `mode.requested/applied` 与 `cpu_mode.desired/applied` 均为0。它们仍描述设备的全局编辑器，不能用客户端演奏通道覆盖。待采用状态的 `received` 已前进，而 `processed`／`parameter_processed`／`midi_channel` 保留旧观察。普通键状态更新推进 `processed/result_sequence`，不推进 `parameter_processed`。

当前 AE350 场景始终为 `engines[id=2].available=false`、`fm.available=false`、catalog `runtime_available=false`；FM MIDI 通道仍可选择到2。`fm-native-parameter-rejected.json` 保留真实 `fm_unavailable` 拒绝，不据路由成功开放旧 native FM operator／preset API。FM schema/profiles/contract 身份保持在 capabilities 的原字段中。

CC7 用 `op=2,a=7,b=96,c=0`。生产 `sub_patch1_required_cc(7)` 要求 SUB_PATCH1 supported 与 valid，`cc7-sub-patch-invalid-rejected.json` 为真实503且不发布事件。CC修改共享编辑器 patch；主机模型按实际 `handle_cc` 的映射将 `legacy.volume` 观察设为192，而非设为该客户端专属音量。KS decay 用 `op=6,a=4200,b=0,c=0`，对应全局 `legacy.ks_decay_ms=4200`，也不是每通道独立编辑能力。

CPU `valid=false` 或 `fault=true` 的选择均为真实503 `cpu_mode_not_ready`，不消耗序号。当前核心和 owner 的通道选择路径没有单独以 `ready=false` 或 `busy=true` 拒绝；`state-cpu-not-ready.json` 与后续真实202／主机采用观察明确保留这一点。客户端可以采取更谨慎的 UI 门控，但不能把它描述成服务端必定拒绝。输入 ready／STOP pending、owner unavailable、恢复状态也有独立样例，不能仅看全局 mode 或 HTTP202 判定可演奏。

## 验证边界与恢复

所有 owner 采用、ready、fault、输入和 patch 观察都是 **HOST OWNER MODEL**；扩展入口没有执行真实 voice owner，也没有 SPI、NVS、无线或音频证据。`source/main/voice_alloc.c` 和 `owner_local.inc.h` 仅作为冻结的语义来源，不冒充实板完成。lease_ms=1200 来自原序列化；这些固定时间的主机夹具不证明手机实际250ms心跳或无线压力稳定性。

恢复时将本合同目录与匹配的 `tools/test_ae350_service_capture.py` 放入独立 App 根目录，再先执行 `--check-only`，然后执行正常复验。不要覆盖现有脏工作树。该包是 App 合同及主机测试闭包，不是完整 ESP32 固件恢复包。此任务未写 `.ai/`，未修改 Git 索引、分支、暂存或提交；本目录与脚本由 App 主任务纳入其源码快照。

`verification.json` 保留脱离原目录的 App 副本复编译与五项损坏负控结果：修改夹具、缺失夹具、额外文件、符号链接、修改 wrapper 均被只读核验拒绝，恢复原字节后通过。便携复验的重复输入副本及本任务已知可执行文件已清理；正式冻结源、C 捕获器、原始 JSON、trace、编译日志与运行结果保留。
