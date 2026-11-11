# B 板调试协议 v1

2026-10-28。B 板仍是独立激光输入板，A/FPGA/CAD 保持原样。调试通过 USB1/CH343P UART0 115200，原生 USB2 继续 MIDI。本文是固件和上位机共同实现契约。新固件使用专属有界UART RX/TX任务，人工harp命令保持兼容；主机也兼容旧REPL回显/日志。

## 请求与回复

请求为 ASCII 单行，以 LF 结束，最多 1024 字节（不含 LF）：

```text
rpc <id> <expected_revision> <verb> [arguments]
```

`id` 是 1..4294967295，`expected_revision` 为 -1（不作版本比较）或 0..4294967295。旧 `harp ...` 命令继续可用，作为 id=0、不作版本比较的人工入口。

回复/遥测以 `@B ` 开始，后跟单行 JSON。上位机须忽略前面的 REPL 提示、回显、ANSI 和普通日志；帧可能分片或多帧一次到达。协议版本 `v=1`，不执行从设备接收的文本。

```json
{"v":1,"type":"reply","id":1,"ok":true,"code":"ok","revision":4,"config":{}}
```

失败 `ok=false`，code 为 `bad_request`、`invalid_config`、`revision_conflict`、`busy`、`queue_full`、`io_error`、`storage_error`、`drain_timeout` 或 `unsupported`。读/写成功后都返回实际配置版本；配置回复含完整 config。写入只有在 owner 实际应用后才应答。超时的写请求不自动重发，先 get 回读确认；主机断开不会让固件停止演奏。

## 人工串口选择MIDI通道

通过UART0（USB1串口桥或FT232R）、115200 baud、8N1，以换行结束发送：

```text
harp channel
harp channel 1
harp get
harp save
```

`harp channel`/`harp get` 为只读配置查询。`harp channel N` 接受报文编号0～15，转换成现有内部配置 `channel=N+1`，经原子apply和旧通道清音屏障后生效。默认编号1对应常用通道2，`harp channel 0`/`harp channel 15`分别对应常用通道1/16；负数、16及溢出数值均拒绝。设置不自动保存，按需要再显式 `harp save`。人工命令回复id=0。

所有带配置的回复额外包含顶层只读 `midi_channel_id`（0～15），例如查询默认设置时为1，而 `config.channel` 为2。此字段不属于config/apply参数；旧RPC `channel`、JSON导入导出及NVS继续使用1～16。桌面界面显示/输入报文编号0～15，并在界面边界转换为现有RPC配置，协议版本仍为v1。

## verbs

| verb | 参数/语义 |
| --- | --- |
| `ping` | 协议/在线探测，成功回复含 config |
| `get` | 读取完整 config |
| `status` | 读取 state 快照 |
| `apply` | 一个或多个 `key=value`；整个请求全部校验后原子应用，失败不部分修改 |
| `panic` | 清音、重新武装当前被遮挡通道 |
| `save` | 停止演奏、释放旧音符、进入维护、异步写NVS，完成后回复；不是烧录 |
| `strike` | `channel duration_ms`，channel 为1..12，duration_ms为1..60000，软件试奏当前映射；事件标记 synthetic=true |
| `stream` | `period_ms` 为0或200..2000；0关闭，默认200ms（5Hz） |

例：

```text
rpc 1 -1 get
rpc 2 4 apply volume=90 transpose=0 velocity=20,500,127,16
rpc 3 5 apply notes=60,62,64,65,67,69,71,72,74,76,77,79 gains=100,100,100,100,100,100,100,100,100,100,100,100
rpc 4 -1 strike 1 100
rpc 5 -1 stream 200
rpc 6 -1 save
```

## config（JSON 字段与 apply key 相同）

```json
{"channel":2,"notes":[60,61,62,63,64,65,66,67,68,69,70,71],"gains":[100,100,100,100,100,100,100,100,100,100,100,100],"velocity":[20,500,127,16],"gate":200,"volume":100,"transpose":0,"fine":0,"cutoff":127,"resonance":0,"polarity":1,"debounce":2,"enc_edges":2,"enc_reverse":0,"route":"auto","pins":[[-1,-1,-1],[-1,-1,-1],[-1,-1,-1],[-1,-1,-1]]}
```

- channel：常用叫法的MIDI通道1..16，报文通道编号为 `channel-1`（0..15）。激光竖琴默认 `channel=2`，对应用户约定的报文编号1；十二路弦号仍为1..12，不用于选择MIDI通道。
- notes：12个互不重复的MIDI编号0..127。有效音高=notes[i]+transpose；超出0..127的通道暂时静音、state.notes[i]=-1，避免端点夹紧造成重叠。默认仍为C4–B4。移调/映射改变先释放旧音高。
- gains：12个力度百分比，25..200，默认100；全局曲线计算后乘该路百分比、四舍五入并限制1..127。CC7通道音量是独立参数，不混为velocity。
- velocity：四元组 `[min_ms,max_ms,start_velocity,end_velocity]`，1≤min_ms<max_ms≤60000；两个力度端点各为1..127，默认 `[20,500,127,16]`，短遮光更响，长遮光更轻；仍允许配置任意方向的曲线。gate为10..5000ms。
- volume/cutoff/resonance：0..127；transpose为−24..24；fine为−100..100 cents（仍按接收端±2半音换算）。
- polarity：0/1，原图默认1；debounce：1..50ms；enc_edges：2/4；enc_reverse：0..15。
- route：auto/usb/ble/both。pins为4×3完整绑定，-1 -1 -1禁用；其余按既有B GPIO白名单、全组完整性和重复引脚校验。apply中pins用12个逗号分隔整数（按4×3行展开）。
- 未知key、重复key、缺值、溢出、非法数组长度或范围全部拒绝。模型不接收代码、文件路径或任意寄存器写。

只改变参数、音符映射、曲线可在RUNNING原子生效；改变MIDI channel、polarity或pins先进入QUIESCING，用旧通道释放音符，再改动并重新武装。save同样先清音。500ms仍未获得router对指定目标代次的quiet报告则返回drain_timeout，不写Flash/不改硬件。

## state 和击弦记录

```json
{"v":1,"type":"state","uptime_ms":1000,"revision":4,"lifecycle":"running","raw":0,"shade":0,"playing":0,"duration":[0,0,0,0,0,0,0,0,0,0,0,0],"velocity":[0,0,0,0,0,0,0,0,0,0,0,0],"notes":[60,61,62,63,64,65,66,67,68,69,70,71],"hits":[0,0,0,0,0,0,0,0,0,0,0,0],"controls":{"transpose":0,"volume":100,"fine":0,"cutoff":127,"resonance":0},"links":{"usb":false,"ble":false,"selected":0,"usb_sent":0,"ble_sent":0,"usb_retries":0,"ble_retries":0,"ble_errors":0},"timing":{"last_gap_us":1000,"max_gap_us":1000,"max_work_us":0,"missed_periods":0},"drops":{"commands":0,"replies":0,"events":0},"illegal_edges":0,"encoder_enabled":0,"ab":[3,3,3,3],"pressed":0,"heap_free":0,"events":[]}
```

status 回复把上述快照放在 `state` 对象中。state无需携带完整config。异步state包含各路最近一次实际遮光结果；`hits`递增，避免短脉冲落在两次遥测之间时被误当没有发生。

events是紧凑数组，每项为 `[event_seq,time_ms,channel,duration_ms,velocity,note,synthetic,revision]`；channel为1..12，note=-1表示当前有效音高越界。每帧最多8项。固件事件队列64项，满队列不阻塞采样、计数drops.events；主机按seq去重并可导出CSV。snapshot最新值可覆盖，RPC回复单独排队，不让大量遥测占用控制回复。

lifecycle为 `boot`、`running`、`quiescing`、`saving`、`reconfiguring`、`fault`。link离线不会终止采样；单路初始化失败进入可诊断的降级状态。维护期间不承诺1ms采样，结束后重新武装，不能把写Flash导致的停顿算作真实遮光。

## 上位机行为

Python3/Tk + pyserial，串口I/O在工作线程，Tk只在主线程更新。连接后先ping/get/status/stream，不自动覆盖设备配置或自动写Flash。编辑配置形成草稿；点击“应用”才用已读revision提交；冲突时回读并提示，不静默覆盖编码器的新调整。显式“暂停演奏并保存”才save。

包含12路状态/音符/力度校准、全局曲线/CC7/微调/route、软件试奏、Panic、JSON配置导入导出、CSV击弦记录、调度统计和离线演示。导入JSON只改草稿。真实串口仅由用户选择后打开；测试使用PTY或演示，不连接/烧录实物。打开串口前将DTR/RTS设为禁用，实际驱动可能仍有瞬时跳变，不宣称绝对不复位。
