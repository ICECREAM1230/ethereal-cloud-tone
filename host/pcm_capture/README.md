# CH569 实时 PCM 采集

复用原工程的 USB 3.0 Bulk IN 接收方式和现有界面。CH569 固件、启动控制请求、HSPI 时钟/握手/CRC、PC 可见帧格式保持兼容，不新增应用层握手。

## 使用

Linux 运行当前工程中的 `launch_pcm_capture_gui.sh`。本机已在工具目录 `.venv` 安装 PySide6/PyUSB，不改系统 Python。其它电脑可创建 Python 虚拟环境后安装 `requirements.txt`，再执行 `python -m pcm_capture.app`。Windows 原有 `.cmd` 启动入口保留，仍需要原来已可用的 libusb 驱动。

1. 选择 **USB**，默认 VID `359F`、PID `3001`、Bulk IN `82`，保持原 CH569 固件及连接。
2. **Duration = 0** 连续采集，或输入固定秒数，点击 **Start Capture**。
3. 查看实时波形、帧数、接收速率、序号间断、RMS 和满量程样本数。**Stop / Finalize WAV** 停止并完成 WAV 头。
4. 每次生成独立目录：`raw.bin`（原始接收）、`payload.pcm`（当前为s32le左右交错）、`audio.wav`、`stats.json`；故障也保留已收到的数据。

默认要求实际链路为 USB3 SuperSpeed。显式取消该选项可诊断旧后端不报告速率或 USB2 连接；此时界面/统计仍报告实际或未知速率，不将其称为 USB3。Fake 只测试界面，始终标注 SIMULATION / FAKE。

Linux GUI 点击 Start Capture 后先读取4096字节，只有超时才发送原 `40/8f/007f` START 请求；已在运行的流直接续读，预取字节完整保留。保留既有设备配置和接口0，不 clear_halt 或 reset。Windows 保留原 configure/clear/START 行为。CLI 默认只读，需要启动空闲设备时使用 `usb --arm-if-idle`。`stats.json` 的 `arm_policy` 和 `start_request_sent` 记录实际行为，不增加应用层协议或逐帧握手。

**连续传输没有累计字节或时长上限。** 按已核对的参考测速固件逻辑，START 后会持续把 HSPI 数据转发给 USB；4KiB 是每个发送块的大小，发完继续下一块，不需要逐块 START。GUI 设 **Duration = 0** 并让电脑持续及时读取，即可持续采集；固定秒数到时停止的是上位机读取。缓冲水位只用于暂停/恢复接收，电脑停止读取、USB 断连/复位、溢出或 STALL 等异常可能中断传输。标准 WAV 的4GiB文件限制仍存在（当前格式约93分钟），长录音需分段；这不是固件传输总量门限。详细停止条件见 `PCM_CAPTURE_GUIDE.md` 的“连续采集与停止条件”。

2026-11-01 的625A实板复现：同一USB3设备只读6秒0B，随后仅发送原START后6秒5251072B/8974有效帧/574336左右对，0无效帧/跳号/重同步，双声道S32 WAV校验通过。对应参考测速固件上电状态0会丢弃HSPI帧，START使其等待约20ms后在完整帧边界发布给EP2。此短测不证明长测无丢样或音质通过；原二进制未读回与网上源码逐byte对照。

随后真实只读连续采集60.008秒，收到52,715,520字节、90,090个有效帧、5,765,760组左右样本，无效帧/跳号/重同步均为0，本次没有发送START，WAV与payload逐字节校验通过。数据全部静音，这项结果证明该次传输持续正常，不代表有声音质、小时级无丢样或仿真逐样本一致性已验收。证据在 `sim/results/ch569_625a_usb_recovery_20261101/continuous60_20261101T104429Z`（相对工程根目录）。

当前 GUI 提供双声道波形、RMS 和过零频率估计；尚未实现真正的 FFT 频谱显示。

当前 FPGA 接入点为 **PCM-B / 最终 SUM 在DAC截位前的左右完整24位**，固定此点；不是独立某一路合成器。每个声道以32位PCM容器承载：`L32=L24<<8`、`R32=R24<<8`，低8位补零，原24位没有截断。WAV保存为双声道32位PCM，左右波形分开显示。既有DAC串行输出位宽不改。频率沿用既有 audio PLL，约96008.043 samples/s；原格式整数头字段及 WAV 标为96008。输出静音期间持续传输零值，不使用有效音符事件代替采样时钟。尚未播放的新码流必须另行明确授权烧录。

新版仍支持旧mono16头flags0；本轮stereo32使用已有flags3，每512B帧64组左右样本，每4096B块448组，聚合时间约4.67ms（不含USB/界面调度延迟）。

完整接线/兼容性和验证边界见 `PCM_CAPTURE_GUIDE.md`。本工具不烧录 FPGA/CH569/ESP32，不连接 ESP32 HTTP。
