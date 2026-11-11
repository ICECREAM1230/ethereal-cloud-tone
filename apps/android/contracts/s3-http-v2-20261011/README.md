# S3 HTTP v2 冻结输入与独立 host 验证

2026-10-11。用于原生 App 的协议接入验证。当前冻结 schema **1.0.1**、ABI **1.1**、HTTP **2.0**，JT51 profile.id=1 / backend_id=1 / profile_version=65536。profile 与 backend 的相等只描述这份实际输入，不构成客户端假设。

40份 S3 原始文件共143,830字节按 `s3/<原相对路径>` 逐字节保留，包含2份完整正式说明、8份实际 handler fixture，以及30份系统编译器确定的 C／header／SDK boundary stub 依赖。源根、仓库基线、每份原始相对路径、字节数与 SHA256 见 `manifest.json`；复制前后重复读取确认原始字节稳定。原生成 C header 与 App 冻结的 schema1.0.1 header 逐字节相同。没有裁剪生产源码或手写第二份 parser。

正式接口位于 `s3/docs/fm/HTTP-v2-contract.md`，运行期说明位于同目录的 `运行期桥接交付-20261011.md`。8份 JSON 位于 `s3/docs/fm/http-v2-fixtures/`。这些文件保持 S3 owner 原字节，包括文档内的历史路径／链接；若需追查完整 S3 产物，用原工程交付包。

## 复验入口

从 App 根目录运行：

```bash
python3 -B tools/test_http_v2_handler.py --check-only
python3 -B tools/test_http_v2_handler.py
python3 -B tools/test_http_v2_handler.py --vectors build/app-v2-vectors.json
```

只依赖 Python3、现有系统 `cc` 与标准 C library，不需要 SDK 下载、完整 ESP-IDF、Android/JVM 或 EDA 构建。默认入口先只读校验完整文件集合与 SHA256，再按原 host flags 编译和运行实际 `test_http_v2.c`，逐字节比较8份原始 JSON。它包含原 `test_http_api.c`、生产 `wifi_web.c/http_v2.inc.h`；另编译完整 `web_control.c` 与 `patch.c`。未调用的旧 v1 HTML 路径由原 `--gc-sections` 机制移除，不改测试源码、不虚构网页资源。

`--check-only` 不创建文件、不修复缺文件、额外文件、软链接或内容漂移。`--verify-origin` 额外只读检查原 S3 来源，正常复验不依赖原机绝对路径。每次编译输出、8份 JSON、桥程序和逐命令／逐向量结果保存在新的 ignored `build/http-v2-handler/run-*/`，入口打印 `summary.json` 路径；没有写入其他 App 构建目录或原 S3。

## App 向量接口

外部 JSON 为数组，每项必含 `body:string`、`words:[7个0..4294967295整数]`、`valid:boolean`；可选 `name`。words 顺序为 `token, sequence, context_epoch, op, a, b, c`。有效向量比较全部7个真实 C 解码值，不从body切分构造期望；非法向量要求解码失败。JSON 字符串中的换行／NUL按实际 UTF-8字节发送，不会被行读入或 C string 悄悄截断。

`tools/HttpV2ParseBridge.c` 与本目录 `bridge/HttpV2ParseBridge.c` 的逐字节一致性和 SHA256均纳入闭包。C bridge 的 stdin 是4字节大端长度加原body，stdout 每项为 `ok token sequence context_epoch op a b c`。它 include完整生产 `web_control.c`，调用真实 `web_control_v2_parse`。NUL和95字节上限是生产 `http_v2.inc.h` 中 parser 前的 HTTP envelope 检查，桥保持这层区别；它不将const char* parser的终止NUL冒充request拒绝能力。

`parser-vectors.json` 固定81项：16项接受、65项拒绝。包括全宽身份／target／value、每字段uint32溢出与正负号、空间／换行／NUL／额外字节、HTTP长度边界以及生产通用opcode值域。`valid`只表示HTTP envelope及生产parser通用验证；FM参数的匹配profile／作用域／具体范围、session／epoch／sequence、READY由真实handler及owner另行检查。桥中全uint32运输正例不代表当前JT51允许该参数值，更不代表设备执行成功。

## 验证边界与恢复

原 SDK boundary stubs 只建模同步HTTP调用；默认fixture含host指定默认patch／KS capability／context=9。两份live fixture的FM能力、身份与状态同样由host测试构造。它们用于检验生产handler序列化与客户端门控，不能声称实际设备发现、SPI accepted/completed读回、FM声音或物理端到端延迟。HTTP202表示received，其他确认遵循正式契约。

初次独立复编译已得到8/8逐字节相同的JSON，原产物与复编译输出均没有尾换行差异。当前验证证据见 App `docs/fm-runtime-20261011/handler-*.json|*.log`。本闭包没有修改SDK、S3、共享schema、生成Java、Git索引或 `.ai/`。

恢复时将本目录与 `tools/test_http_v2_handler.py`、`tools/HttpV2ParseBridge.c` 一起从App角色快照恢复到独立目录，先执行只读闭包校验，再运行host复验；不要覆盖当前脏工作树。manifest同时绑定外部wrapper的SHA，避免恢复一份未匹配的编译／校验入口。本目录本身是完整最小host输入，不是完整S3固件恢复包。
