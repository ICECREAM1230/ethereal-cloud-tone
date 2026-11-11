# B 板生命周期、模块调度与上位机 Implementation Plan

> 使用 TDD；纯控制逻辑与上位机按固定协议并行，主任务负责输入核心及ESP-IDF调度。用户已授权可逆实现选择自主决定，不增加设计审批/提交/烧录流程。

**Goal:** B 板可测量、可配置、可维护地运行，提供可实际上板调音的桌面程序。

**Architecture:** 输入/音乐状态单owner；MIDI router、串口序列化、NVS写入分别在独立低优先级任务；有界队列与维护状态机隔离阻塞。纯C控制/协议/持久化可主机验证，PC GUI通过同一串口协议调试。

**Tech Stack:** 本机ESP-IDF v6.1；原有TinyUSB/NimBLE；C11；Python3.12/Tk/pyserial3.x，不增加全局依赖。

**Spec:** `docs/b-board-debug-protocol.md`；用户当前模块化/生命周期/上位机要求；既有释放后发声和高有效逻辑。

## 约束

- 独立B板，不动A/FPGA/CAD，不连接或烧录实物，不暂存提交。保留当前未提交基线，前镜像位于`.ai/evidence/lifecycle-debug-20261028/preimages/`。
- notes/gains/曲线/CC7可调；逐路映射互异、越界静音；有效电平完整结束才发声。未知编码器GPIO保持-1。
- UART115200、默认5Hz遥测、有界RPC/事件队列；回复在真实应用/保存完成后产生，状态版本冲突不能部分修改。
- 采样任务不做UART打印/JSON格式化/USB或BLE写/NVS；记录采样间隔、工作耗时、错过周期及丢弃计数。延迟后不连续补采样伪造边沿。
- 保存/硬件配置用旧通道清音并确认router已处理指定代次，再暂停输入、异步维护、重新武装。Flash期间不声称硬实时。
- 现有NVS v2可迁移，v3验证版本/长度/CRC，坏配置回退但不自动擦除。RAM应用与Flash保存分开。

## 执行任务

- [x] 纯C b_control：配置/atomic patch、严格文本RPC解析、受限JSON格式化、v3持久化/v2迁移、维护状态机及主机测试。拥有`components/b_control/`与新增控制测试，迁移原纯C pinmap至该模块。
- [x] 上位机：`host/b_board_debugger/`及其测试/README/启动脚本，按协议实现GUI、线程隔离、超时、revision、PTY/演示、配置/CSV。只拥有host目录。
- [x] 主任务：harp_core增加逐路notes/gains、倒向曲线和标记软件试奏；保留旧回归并补RED/GREEN。实现启动装配、router、storage、owner和console worker，调整构建/测试脚本。
- [x] 集成验证：C ASAN/UBSAN、配置/协议/生命周期测试、Python unittest+PTY、Xvfb GUI smoke、ESP32-S3实际构建。模拟/界面演示不代表实物验证。
- [x] 新鲜只读审查，修复实质问题、必要回归；更新生命周期/接口/使用文档和.ai；复核diff/status及产物指纹。

## 验收重点

串口慢/断连不拖采样；BLE/USB拥塞保留确切pending；维护不得用新channel关旧channel的音；旧router quiet不能提前许可Flash；非法或冲突配置原子拒绝；主机草稿不能因状态刷新被覆盖；写超时不盲目重试；所有队列和帧有上限。

## 实施选择

- 桌面Tk使用本机现有包；与Web Serial相比不依赖浏览器授权/HTTPS，也不用给B增加WiFi服务。原生USB接口保持MIDI，调试走既有CH343串口。
- 采用人可读文本请求+有版本JSON回复；经本机REPL缓冲限制核对后，使用专属UART RX/TX任务。扫描与发送分离通过最新目标快照，极端拥塞的中间状态允许合并并明确计数/边界。
- 当前权限只允许workspace和/tmp写入，新构建输出选/tmp英文路径。工具链/SDK只读，不修改全局安装。

## 实施记录

- Ruling: 本机ESP-IDF REPL helper固定RX256/TX0且长行可能截断，专属有界UART收发更适合原子调试请求；因此保留harp/rpc指令但移除REPL transport，RX/TX各4KB，超长整行丢弃，主机兼容旧回显。用户随后明确选择UART，符合此接法。
- TDD：映射/gain/倒向/软件击弦3组RED→GREEN；控制域配置/协议/维护/CRC RED→GREEN；PC 27项含同UART MCU复位和Xvfb真实窗口通过；原15组和54包A兼容保持通过。9次跨语言事务用实际PC编码/解析连接真实C核心。
- 新鲜只读审查未见Critical/Important；后续父任务补上PC同UART重启回读与对应回归，复查新增分支。
- 实际ESP-IDF v6.1构建通过，无新第三方固件依赖，未写实物。测试LSAN受ptrace限制关闭，ASAN/UBSAN继续有效。
- 板PR门限按原图单列说明，GUI不虚构其电压或远程控制。
