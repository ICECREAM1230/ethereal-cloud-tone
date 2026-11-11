# S3 HTTP v2.0 控制契约

2026-10-11，HTTP owner 冻结的产品接口 v2.0。实现位于本 S3 包的 `http_v2.inc.h`、`web_control.c`、`voice_alloc.c`；v1 五整数接口原义保留。此文冻结 HTTP 格式和安全生命周期，**不宣称活动 FPGA 已接入 FM**。S3运行期驱动已接入；只有实际设备能力、ABI、版本／摘要、维度和READY检查匹配才接受FM写入，未匹配时返回 `503 fm_unavailable`。本轮未操作设备。

当前消费的统一生成输入为 **schema 1.0.1／ABI 1.1**，schema SHA `68d7d85ab97942edb128efcb3dffe01bdc50fb98baf9476ab4663d4f22c2cc73`，profiles SHA `51a6af55646fe2adb54bf4f9f3d06e2d262bd830ec5f30f80c0e5bfa2eadf49f`，组合身份 `86aa104f60f3e8fc586a9f64052032e45b24d7148f43610b3ef0e86c9e5fed38`。9份输入和五语言生成文件已同步，构建只读检查通过。原1.0.0检查点保留在 `snapshots/fm-http-v2-preintegration-20261011.tar.gz`；原失败／初次构建日志不覆盖。HTTP v2格式不依赖手写opcode／SPI地址／参数表。

## 请求入口

| 请求 | 输入 | 输出 |
| --- | --- | --- |
| `GET /api/v2/capabilities` | 无 | API版本、schema/profiles/contract SHA、`context_epoch`、限制、各引擎可用性和离线 profile 索引 |
| `GET /api/v2/state` | 无 | 一致 owner snapshot、模式、四客户端确认、FM状态、嵌套的完整 `legacy` v1 snapshot |
| `POST /api/v2/session` | `X-FPGA-Control: 1`，空 body | 201 `{api_major,api_minor,token,client,lease_ms,context_epoch}` |
| `POST /api/v2/command` | 相同 header，严格文本 body | 202 `{api_major,accepted,stage:"received",context_epoch,fpga:"unverified"}` |
| `GET /api/v2/fm/catalog` | 下述query | 生成 profile／参数／预置目录；离线描述不代表设备可用 |

全部同源，不添加 CORS；使用现有四槽 HTTP task-owned 会话池。v1/v2 token不能跨版本使用，共用容量但各自绑定协议。十秒有效命令活动会话、1200ms 演奏租约保持原义；状态读取不续租约。v2 会话绑定创建时的 context，切换／Panic／恢复后旧绑定失效，可立即复用旧v2槽，不必等十秒。

## 七整数命令

`token sequence context_epoch opcode a b c`，七个 uint32 十进制数，用单个 ASCII 空格隔开；不允许符号、换行、尾随数据、NUL或溢出。最长95字节。token、sequence、context_epoch均非零。序号共享全部op，成功received后严格递增；同步拒绝不消耗序号。`b/c` 保留完整32位，不能复用v1的uint8力度字段截断目标。

| op | a | b | c |
| --- | --- | --- | --- |
| 2 CC | v1白名单CC | 0..127 | 0 |
| 3 KEYS_UPDATE | 完整25bit目标位图，0..33554431 | 力度1..127 | 0 |
| 4 STOP | 0 | 0 | 0 |
| 5 ENGINE_SELECT | 0 DDS／1 KS／2 FM | DDS/KS为0；FM为非零backend ID | 0 |
| 6 KS_DECAY | T60，200..8000ms | 0 | 0 |
| 7 FM_PARAMETER | parameter ID | 生成helper打包的 `target_word` | 参数uint32 wire value |
| 8 FM_PRESET | 当前匹配profile的preset ID | 0 | 0 |

参数作用域由生成目录的parameter描述决定，客户端用同版target helper。PATCH的voice/operator均为NONE；PATCH_OPERATOR的voice为NONE，operator按显式patch_operator_count；VOICE_OPERATOR按所选voice实际算子数。i32若以后由profile提供，以二补码uint32运输，再按该参数有符号值域验证。当前JT51参数全部chip_code／next_note，不能标注为毫秒或dB。预置切换需清旧演奏意图后重新按键。

HTTP 的 `context_epoch` 是 S3 voice owner 的演奏上下文围栏，启动为1，任何全来源Panic、模式切换与恢复清理都会推进，回绕跳过0。它**不等于**FPGA原生 `fm.engine_epoch`，HTTP token也不等于FPGA session。客户端不可提供SPI sequence、voice_generation或寄存器；底层12word由唯一SPI owner绑定实际session/epoch/topology/generation并序列化。原七字段草案中的`engine_epoch`在本产品接口命名为`context_epoch`，以避免两层身份混用。

## 确认与错误

`state.clients[client]` 有 `token,received,processed,parameter_processed,stop_processed,keys,result_sequence,result_code,result_context_epoch`。token是owner已采用的槽身份；刚收到201时它可能尚未出现。received来自HTTP task，其他确认来自voice owner。按键需同时等待processed>=seq、token一致、keys等于目标；参数用parameter_processed；停止用stop_processed。`result_code` 使用生成result枚举，来源是ESP32 owner，不是FPGA ACK。queued命令若在owner采用前context变化，会以STALE_EPOCH拒绝并记录所属旧context，旧On不会恢复。

STOP／切换被采用后context会变化。客户端须清触点与旧队列，重新读状态和建会话；等待requested/applied一致、无switching/recovering并重新空位图就绪后再允许新按键。不能只把新context填入旧token请求继续发送。切换／恢复中的非零按键会同步返回503；owner在采用前再次检查，关闭HTTP检查到实际采用之间的竞态。零位图、Panic与原v1安全路径保留。

| HTTP状态 | `code` |
| --- | --- |
| 400 | `invalid_command`、`body_length`、`empty_body_required`、`invalid_query` |
| 403 | `header_required` |
| 404 | `unknown_profile` |
| 409 | `stale_sequence`、`stale_context` |
| 410 | `unknown_session`、`session_expired`、`protocol_session` |
| 503 | `session_full`、`owner_unavailable`、`engine_not_ready`、`ks_unavailable`、`fm_unavailable` |
| 500 | `state_too_large` |

错误JSON为 `{api_major:2,code:"..."}`。断开的socket／接收错误可以无JSON结束；超时仍属模糊结果，不可盲目重放On。客户端用状态收敛；上下文或token失效时清旧意图重新连接。HTTP202、ESP32采用、FPGA accepted/completed、实际发声分别显示。

## 目录与状态

catalog必须有 `profile=<uint32>`；`section=profile|parameters|presets`，省略section默认为profile。参数/预置分页用 `offset=0&limit=8`，limit范围1..8；返回 `total,offset,next_offset,items`，末页next_offset为null。重复或未知query key、无效值、编码歧义拒绝400。profile响应含真实生成的voice_count、operators_per_voice、patch_operator_count、operator_labels和数量；参数响应含id/name/range/scope_id/unit_id/apply_time_id/value_type_id；预置响应含id/name/key_on_logical_mask/values_count。前端不维护另一张参数范围表。

`state.mode` 使用通用0/1/2 engine ID；`legacy.engine`仍为现版DDS/KS字段。FM模式仅在SELECT完成确认后成为applied；切换期间阻止新按键。v1在FM期间返回ready=false，并拒绝普通写入，保留Stop／空位图路径。兼容host回归已覆盖该门控。

`state.fm` 规定 `available,valid,phase,reason,backend_id,profile_id,profile_version,profile_sha256,voice_count,operators_per_voice,patch_operator_count,session_id,engine_epoch,topology_generation,source_rate,output_rate,flags,features,accepted,completed,result,panic,parameter_values,voice_generations`。已接入驱动的非空对象格式如下，字段均为uint32，SHA为字符串：

- accepted／completed：`{session_id,engine_epoch,sequence_id}`。
- result：`{session_id,engine_epoch,sequence_id,code}`，code为生成result枚举。
- panic：`{accepted,completed,result}`，每项按上述独立Panic结果身份或null，不借用普通命令结果。
- source_rate／output_rate：`{numerator,denominator}`，分母必须非零。
- voice_generations：按实际voice_count排列的uint32数组；operators_per_voice同长度，不能填离线猜测容量。
- parameter_values：`{parameter_id,target_word,value_u32,sequence_id}`数组，代表owner按实际完成确认缓存的值，不称为芯片寄存器读回；pending值不覆盖已确认值。

accepted/completed/result/Panic身份必须属于原命令，不能从新current身份猜测。未发现匹配设备能力时，身份字段仍为null／空数组，容量为0未知，原因由 `capability_missing`、`abi_mismatch`、`profile_mismatch`、`spi_readback_failed` 等表示。目录存在不能开启FM控件。实际能力、ABI、版本、摘要、维度及READY均匹配后才呈现runtime可用；SELECTED与READY分开。

## BLE／MIDI边界

保留现有标准 BLE MIDI service、DIN／面板 MIDI 和各自来源身份，不增加私有GATT寄存器写入，也不增加USB Host。FM实际接入后，NoteOn/Off（含velocity0 Off）、CC7音量、CC10声像、CC64来源延音、pitch bend和Program Change按实际backend声明映射；preset只接受生成目录中存在的ID。CC123释放自己的来源，CC120全来源Panic；不将backend全局RELEASE_ALL用于误释放其他来源。触后或未声明CC不伪装成已支持。FM任意参数通过本HTTP目录与op7，不手写一套芯片CC／原生寄存器表。

## 同版本fixture与验证

[fixture目录](http-v2-fixtures) 内JSON由本轮实际编译的固件handler生成，包含无FM能力／状态、profile、13参数的两页与3preset，以及两份带观察FM输入的序列化示例。这是host指定默认patch／KS能力／context=9的fixture，不是实板读取。C输出已交由冻结生成JS交叉核对目录、范围和身份；`legacy`再次通过原Webschema。

运行期桥接已接入单一SPI owner和声部owner；最终host回归覆盖能力禁写、nonce、完成确认、generation、Panic取消、超时、session复位、来源／租约和HTTP真实handler。审查修正后的schema 1.0.1版已完成最终Wi-Fi开／关构建，两者退出码均为0；128份冻结源码和输入摘要保持一致。当前嵌入Web owner交付的53,952 B v1 DDS／KS产品HTML，SHA `359130200d92524ffbfb7c9962841855a078d8acebea919812724803406ab174`，已从最终ELF和BIN核对相同字节。Web／App的FM v2运行客户端属于对应owner，尚不能用该页面演奏FM。没有设备、FM音频或最终FPGA STA验收。日志、产物摘要与恢复入口见[运行期桥接交付](运行期桥接交付-20261011.md)。

## 运行期交接补充（1.0.1）

- `profile.id` 是目录键；`backend_id` 是SPI选择编号。发现按 `(backend_id, profile_version, profiles SHA256)` 匹配profile，再用其id查询参数／算子／preset；不能假定id与backend相同。真实驱动另有profile99/backend1生成输入复验。
- `runtime_available` 取设备已验证能力和READY，不直接采用离线目录的false常量。当前固件支持JT51 backend；MSFA仅目录。voice实际算子数由QUERY applied index和status generation确认，MAX仅作容量边界。
- 正常命令一次一个immutable在途body，只有未接受BUSY可原body／原序号重试。接受与完成分别公布原 `(session_id,engine_epoch,sequence_id)`。正常确认上限100ms；无确认则清意图、重新发现并以独立Panic恢复静音，等待显式新SELECT。
- 模式改变、Panic及preset清旧按键并推进S3 context；v2 token必须重建。SELECT新nonce，进入FM前等DDS双bank静音及KS退出；离开FM后才允许KS进入。旧v1在FM期间只允许Stop／空bitmap，普通写返回503，旧state.ready=false。
- native PLL失锁/session清0后不自动选择或回退DDS，不重播旧On。重新发现能力、新SELECT/new nonce成功后重新建v2 session、空bitmap确认，再由用户重新按键。
- MIDI映射：每来源／channel独立CC7音量、CC10声像、CC64延音、14bit bend（±2半音）和CC123释放；CC120全局Panic。ProgramChange采用当前profile的preset ID并清旧键／context，未声明的CC/pressure/SysEx不映射为FM寄存器。USB MIDI Host仍未实现。
- 参数为当前profile的PATCH／PATCH_OPERATOR目录，apply_time为NEXT_NOTE，修改不会重新触发已持有音符；HTTP202与owner result表示采用，不是芯片值读回。`parameter_values`仅在FPGA completed OK后更新确认缓存，preset完成则按同源生成values更新；它不是FM芯片寄存器读取。
- `docs/fm/http-v2-fixtures`是实际C handler的SDK边界模型输出，包含无FM与有FM的序列化示例。构造的host capability／状态输入不表示物理FPGA已被发现。客户端须以实际设备返回值运行相同门控。
