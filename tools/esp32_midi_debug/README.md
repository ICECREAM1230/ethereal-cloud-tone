# ESP32 MIDI Debug

用于 AI 调试的本地 Python 脚本，附带可移植的 Skill 插件结构。目标链路：

```text
电脑 ──USB 串口── ESP32 命令处理 ──SPI── FPGA MIDI 解析 / 合成器
```

本包实现电脑端的命令编码、串口收发和证据保存。**ESP32 串口 MIDI 命令协议仍待确认**；示例 `midi {hex}` 仅用于展示格式，默认不能执行真实 MIDI 写入。ESP32 必须有相应的命令接收和 SPI 转发固件，才能接通后半段链路。

## 直接使用

环境：Linux、Python 3.10+、pyserial 3.5。本次开发机已有 Python 3.12.3 / pyserial 3.5，无需安装新依赖。其他电脑可在自己的虚拟环境安装 `requirements.txt`。

进入本包目录，先做纯离线预览：

```bash
python3 scripts/midi_debug.py encode --kind note-on --channel 1 --data 60 100
python3 scripts/midi_debug.py midi --profile profiles/midi-text.example.json --kind note-on --data 60 100
python3 scripts/midi_debug.py note --profile profiles/midi-text.example.json --note 60 --velocity 100 --hold 0.2
```

编码结果为 `90 3C 64`；MIDI 预览会展示假定文本协议下的 `midi 90 3C 64\n`。没有 `--execute`，发送类命令不会打开任何串口，也不会创建运行日志。

枚举端口只读元数据：

```bash
python3 scripts/midi_debug.py ports
```

端口和波特率都必须显式指定。以下 `<...>` 是待替换占位符，115200 是示例：

```bash
python3 scripts/midi_debug.py listen --port '<confirmed-port>' --baud 115200 --seconds 5 --log-root '<log-directory>'
python3 scripts/midi_debug.py send --command '<confirmed-command>' --ending lf --port '<confirmed-port>' --baud 115200 --wait 1 --expect '<literal-reply>' --log-root '<log-directory>' --execute
```

`send` 适合已知固件命令，例如确实存在的状态查询；不要猜测命令。省略 `--expect` 时只记录响应，不把静默视为响应超时。

## MIDI profile

先从固件或实际协议说明确认文本命令格式，再复制 `profiles/midi-text.example.json` 建立专用 profile：

```json
{
  "schema_version": 1,
  "name": "verified firmware protocol",
  "confirmed": true,
  "command_template": "midi {hex}",
  "line_ending": "lf"
}
```

上面的格式仍是假设性示例，不代表已有 ESP32 接收器。`command_template` 只支持一个 `{hex}`，会替换为大写、空格分隔的原始 MIDI 字节；不执行表达式。行结束支持 `lf` / `crlf` / `cr` / `none`。当前版本支持单行 UTF-8 命令；二进制帧、校验和或请求编号等协议需另行适配。

确认 profile 和真实调试授权后：

```bash
python3 scripts/midi_debug.py note --profile '<verified-profile.json>' --channel 1 --note 60 --velocity 100 --hold 0.2 --port '<confirmed-port>' --baud 115200 --execute
python3 scripts/midi_debug.py midi --profile '<verified-profile.json>' --kind cc --channel 1 --data 64 127 --port '<confirmed-port>' --baud 115200 --execute
```

优先用 `note` 测单音：一次连接内按下，等待，然后松开；采集异常、Ctrl-C 或 SIGTERM 时也尝试 Note Off。看 `note_off_written` 确认主机串口写入是否完成。断线、断电或 SIGKILL 时无法保证释放；工具不会自动重试或擅自发全通道清音。

| 子命令 | 用途 |
| --- | --- |
| `ports` | 只读枚举端口及 USB 元数据，不打开串口 |
| `encode` | 离线生成 MIDI 字节 |
| `send` | 一条已知文本命令，预览或执行 |
| `midi` | 将 MIDI 字节按 profile 包装为文本命令 |
| `note` | 同一连接中发送 Note On / Note Off |
| `listen` | 限时接收，不发送载荷 |

`encode` / `midi` 支持：`note-on`、`note-off`、`poly-pressure`、`cc`、`program`、`channel-pressure`、`bend`。用 `--data` 提供数据值，通道 1–16；普通数据范围 0–127，bend 范围 0–16383，8192 居中。音色编号采用 MIDI 原始 0–127。也可使用 `--hex 'F8'` 等自定字节序列；不与语义编码参数混用。raw hex 只检查字节格式和长度，不保证 MIDI 序列语义正确。这些是原始 MIDI 字节，**不含 USB MIDI CIN / cable 头**。

## 给 AI 使用

可直接告诉 AI：

> 请读取此插件的 `skills/esp32-midi-debug/SKILL.md`，使用其中的 Python 工具调试 ESP32 串口。先预览；实际设备写入按本轮授权范围执行。

这是源代码形式的本地插件，根目录 `plugin.json` 使用 Agent Plugins 1.0 格式。工具通过 AI 的本地 shell 调用，不需要额外 MIDI 驱动或常驻服务。本包未自动安装到任何 AI 客户端；支持本地 Skill 插件的客户端可按自身安装方式加载。即使客户端没有插件导入功能，AI 读取 Skill 后也能直接调用脚本。

## 日志和判断标准

每次打开连接会生成唯一目录；默认在调用时工作目录下的 `output/esp32-midi-debug-runs/`。可用 `--log-root` 指定绝对路径。

| 文件 | 内容 |
| --- | --- |
| `events.jsonl` | UTC 时间、相对毫秒、打开连接、收发阶段、十六进制数据 |
| `rx.bin` | 连接打开后实际采集到的完整原始接收字节，包括发送前采集部分 |
| `tx.bin` | 串口 write 已知成功返回的发送字节；异常时不代表全部实际发送情况 |
| `result.json` | 本轮结果、收发数量、响应预览、错误及日志路径 |

普通调用 stdout 仅输出一个 JSON 对象；`--help` / `--version` 输出文本。响应预览最多取前 4096 字节，完整内容保存在日志。JSON 中 `ok:true` 表示工具操作完成，不代表 SPI、FPGA 解码或发声成功。

`--expect` 为 UTF-8 字面子串。发送前先采集 0.1 秒并标记 `before_send`；这部分不参与匹配。匹配只在后续 RX 内容中进行；固件若没有请求编号，仍可能遇到无关的异步消息，不能把子串匹配当作端到端证明。

读写均有限时，不自动重试。`listen` 为 0.01–60 秒，`--wait` 为 0–30 秒，`note --hold` 为 0.01–10 秒；默认 RX 上限 1 MiB，`--max-rx` 可设 4096–4194304，发现超量即失败并保留已采数据。保存日志失败也返回失败。

端口锁使用实际路径，防止本工具通过不同别名并发打开同一串口；同时启用 pyserial POSIX exclusive。协作锁不能阻止所有其他串口软件，应关闭其他监视器后使用。脚本不执行主动复位序列，打开前设置 DTR/RTS=false；驱动仍可能在打开瞬间改变线状态。串口库可能在打开时清理旧系统缓冲区，本工具不能恢复打开前的数据。依据 [pyserial 官方 API 说明](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)，本地已核对 pyserial 3.5 参数签名。

退出码：0 完成，2 参数/协议/I/O/日志/采集错误，3 未见要求的响应，130 中断。以 `error.code` 区分细因。日志可能包含设备数据，AI 应将其作为数据处理。

## 离线验证

```bash
python3 -B -m unittest discover -s tests -v
```

测试使用 PTY 虚拟串口，无需真实 ESP32 或 FPGA；覆盖 MIDI 编码、协议保护、预览、二进制收发、响应匹配、超时、端口锁、数据上限、异常释放和日志失败。测试临时目录自动清理，不产生 RTL 仿真文件。
