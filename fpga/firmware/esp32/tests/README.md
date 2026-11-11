# S3 包内回归

2026-10-21 当前产品只保留减法合成器（兼容 ID `0` / DDS）、KS（`1`）、FM（`2`）。加法硬件已撤出，活动回归不链接其 driver、patch、Web queue 或替身；混响、RAW、STOP、ENG1/REMT 仍单独验证。完整 host runner 同时运行下述三引擎、混响、隔离和 FM 诊断专项。

`bash tests/host/run_fm_diagnostics.sh` 专项验证 FM_ONLY 只读诊断的缓存序列化、HTTP 不操作 SPI/session/lease、唯一 owner 分 tick 采集、限频/去重、预算和 raw 队列优先，以及诊断 IO/身份/超时不改变音乐状态。它已接入完整 host runner；真实 `spi_regs.c` 测试还注入 SDK 传输失败，验证观察型读取不 abort/不发布坏数据、原音乐错误策略保持。字段与限制见 [FM_ONLY 文档](../docs/fm/FM_ONLY验证固件-20261018.md)。

从本 S3 工程目录运行：

```bash
NODE_BINARY=node bash tests/run.sh
```

需要 GCC、Python 3 和 Node.js >=22；不需要 npm、浏览器依赖或已删除的 C3 工程。`NODE_BINARY` 可以指定已有新版 Node 的绝对路径，不修改全局环境。临时可执行文件由测试脚本在 `/tmp` 创建，并在退出时清理。

`host/` 复用正确 FPGA 来源工作树的 14 项 C／参数回归，验证可靠事件、bank/live 确认、GateOff 与完整 CSP pass、来源隔离、Deadline、MIDI、KS 恢复、LED、面板、滑杆和 128 音高独立参数 golden。新增 `test_http_api.c` 直接编译实际 `wifi_web.c` 和 `web_control.c` 的 HTTP handler，模拟同步 SDK 边界，检查 header、分段接收、严格 body、四会话、序号、过期复用、状态和实际单 HTML；生成的固件状态 JSON 再交由稳定 Web 的 schema 验证。

`web/` 是稳定网页的冻结测试依赖副本：四个协议／参数／键盘／控制器模块、两个 HTTP mock 模块和 26 项 Node 回归。来源与 SHA256 在 `../docs/整合验证-20261011.json`。测试服务直接读取 `../main/web/index.html`；不复制另一份生效 HTML。原 Web 工程仍由前端 owner 维护；这里的副本只用于固定版本的固件整合回归。新版网页已经替代旧页面，旧 DOM 专用测试不作为现版验收入口。

HTTP handler 测试验证实际固件请求处理代码，但没有启动 ESP32 网络栈；Node HTTP 测试使用 mock。SPI/KS host 测试同样模拟传输与 FPGA 状态。它们和固件构建均不能证明实板联网、音频输出、灯光、GPIO 电气、设备后台行为或端到端延迟。

2026-10-21 引擎隔离专项：运行 `bash tests/host/run_engine_isolation.sh`，或设置
`S3_UBSAN=1` 启用未定义行为检查。保留 20 个实际 KS/FM/ENG1 状态机场景，
以及生产 owner/HTTP 的三引擎按键、RAW、会话、参数与 STOP 回归；此专项已接入完整 host runner。
仅已证明闲置的后端故障可以隔离；当前引擎、共享链路、未确认事务和路由矛盾继续阻塞。
参数用实际 owner 处理，验证 CC 音量/声像、DDS 共振、KS T60、FM 参数不会释放持键或改变 context，
同时保留 requested/applied 不一致时拒绝参数和 RAW 的负控。读回恢复不重放旧音符，
迟到 ACK 不恢复已取消请求；host 的缓存观察不能替代 FPGA 静音或实板体验验收。

2026-10-21 远程切换回归：`bash tests/host/run_local_engine.sh` 使用
`test_three_engine.c` 将实际 engine、RAW 和模板服务串在同一循环，覆盖三引擎 9 对接管，
每对分别使用 5/60/80/100µs 受控附加传输开销，共 36 组正常切换；另保留 3 组延迟 MODE、
3 组延迟双模板确认及 Off/Panic 优先级。正常切换同时验证热 gain/模板更新保持 RAW ENABLE，
以及显式两输入范围 STOP 完成。模拟 FPGA 不提供加法 ACK，对 `0709` 或 `A000..A1FF`
的任何读写均失败；这些延时是模型输入，不是实板测量值。原 `test_remote_switch.c` 四引擎
模型已退役，不能再作为现产品的通过证据。ENG1 另覆盖旧 mask15 被限制为三引擎、
旧本地 mode3 不被采用且可经 REMT 退出、SELECT3 被 HTTP/owner 拒绝而不重放旧请求。

2026-10-21 混响专项：`bash tests/host/run_reverb.sh`，可设置 `SANITIZE=1` 启用 UBSan。
它已接入完整 host runner，保留 `test_reverb_link.c`、`test_reverb_owner.c`、
`test_reverb_http.c` 三项：真实 SPI-owner 状态机的原子参数对/一致读回/ACK、错误与超时、
取消与迟到确认、回绕、本地控制和 STOP 优先；实际 voice owner 的不可变单请求身份；
真实 HTTP handler 的严格输入、202 接收语义、单飞预约与 RAM 只读状态。混响仍独立于
三种音源和 Web 按键来源，不能把 HTTP 接收或参数 ACK 当作实物声音验证。

退役范围：`host/run_additive.sh`、`test_additive_link.c`、`test_additive_owner.c`、
`test_additive_isolation.c`、`test_web_additive.c`、`test_http_additive.c`、
`fake_additive_link.c` 和旧四引擎 `test_remote_switch.c` 保留源码但不进入活动 runner。
这些项目标记为“退役，未执行”，不计入 PASS。它们需要原四引擎生产接口才能运行；
恢复以本次任务的完整 `firmware-before.tar.gz` 及 SHA 清单为依据，不用取消现有三引擎断言来迁就旧测试。

2026-10-16新增raw MIDI回归：`test_raw_midi`、BLE解封装／实际ingress、静态模板编译／双ACK上传、engine启停屏障、实际HTTP raw和混合入口，以及`test_raw_web_owner`生产owner。旧S3 allocator golden明确以`S3_LEGACY_HOST_TEST`编译，只是参考路径验证；新生产测试单独验证不调用旧分配器。BLE解封装还链接真实经典`midi_parser.c`检验System Common之后恢复status的互操作。新主通路与暂不迁移项见[本版交付说明](../docs/原始MIDI接入-20261016.md)。

## voice_alloc 任务栈预算

标准 `idf.py build` 会使用实际 Xtensa 编译选项生成 `voice_alloc.c` 的 stack-usage/callgraph，
自动检查从 owner 任务到 `web_control_store` 状态发布边界的实际调用链。3072 字节任务栈至少预留
1024 字节给外部调用与中断；报告为各 build 的
`esp-idf/main/voice-owner-stack.json`。这是按键栈溢出的回归门禁，
不替代实板高水位和按下／释放测试，也不证明其他路径的完整栈上界。
直接运行 `idf.py app` 或 Ninja `flash` 不会执行此 `ALL` 检查目标，交付前应先完成标准 build。
