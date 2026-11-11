# 空灵云音 · 网页控制端

此目录是当前生产 `../../fpga/firmware/esp32/main/web/index.html` 的可编辑构建来源，包含新拨弦界面与实际 AE350 mode/SUB 能力门控、控制器、库/import8和混响客户端。

原 UI 来源任务：01a1885a-58ef-7a43-8580-8afc5379952a。接收方以当前 AE350 适配模块为准合并界面，没有使用旧 dist 覆盖现有协议。

使用 Node.js >=22：

```sh
npm ci
npm run build:gzip
npm test
```

`dist/index.html` 是单文件离线页面，`dist/index.html.gz` 使用固件的正式压缩脚本。只有在源码验证通过、核对生成 SHA 和差异后，才将 HTML 更新到 `../../fpga/firmware/esp32/main/web/index.html`，再走 ESP-IDF 构建与已授权 app-only 验证流程。构建脚本不会自行覆盖固件或操作设备。

迁入前历史构建记录（不作为本副本的新验证）：HTML 190574 B，SHA256 `a4785b744b4328974084aa4947f777b41d2b746d1f073581ddc825847493726e`；正式 gzip56527 B，SHA256 `cc91a8a263d971845b61bc805daa5663b4f5ed69ecbd0807d635eb2116ac950b`。

`docs/交接来源.json` 和 `docs/固件容量基线.json` 是构建报告读取的历史来源/容量基线，必须保留；它们不是最新固件的容量或板测结论。`artifacts/fm-v2-runtime-20261011/inputs/` 是 mock 和协议测试所需的静态输入，应纳入源码交付。node_modules、dist、其他 artifacts 为安装或生成输出，不纳入源码包。

迁入前源/离线验证曾记录于 `sim/results/esp32_plucked_ui_integration_20261030/`（不在本源码树内），新固件 app SHA为 `b68110f3fdbddffcc794b9999dfbd56ce21faeab0a253aed5b14fa3a0b5141e3`。用户已关闭FPGA，真实新UI拨弦、import存储与音频验证仍待重新开机；不得把 mock/Node 回归当作设备音频验收。mode路由不代表完整KS/FM参数支持，full SUB需要单独真实能力。

项目及上游许可见工程根 `../../LICENSE` 和保留的各文件许可声明。

完成依赖安装后，可从本目录运行 Node 回归及真实 Chrome 的 loopback mock 验证：

```sh
npm test
npm run build:gzip
npm run test:strings-browser
npm run test:fdc-browser
```

浏览器入口默认使用 `/usr/bin/google-chrome`；可用 `WEB_UI_BROWSER` 指定已有 Chrome/Chromium 可执行文件，`WEB_UI_ARTIFACT_DIR` 指定本次证据目录。这些检查运行本副本新生成的 `dist/index.html`，不会连接或写入实体设备。`tests/fdc-return-sub.browser.mjs` 则单独检查工程内保留的固件 HTML，不能据此宣称新生成网页已经合入固件。


## AE350 FDC 单字状态候选

FDC 面板通过 `GET/POST /api/v2/fdc` 使用现有网页会话。`071F` 的 FDC1 状态仅为观察值；revision、driver enabled 和用户配置标志不是某条命令的采用回执。202 表示排队，`transport_sent` 仅表示传输已发送，不代表 CPU/native 完成或已发声。首版六字段手填，不采集 raw、不自动标定、不自动启用/重发。

本机验证：`npm test`、`npm run build`、`npm run test:fdc-browser`（本机 Chrome＋loopback mock）。真实 C fixture 与生成 bundle 交叉：`node tests/fdc-c-fixtures.mjs --fixtures <C-handler-fixtures> --output <result-directory>`。硬件仍需重新开机后独立验收。本候选不改设备、原主工程或生产 S3。


## 本副本验证范围（2026-11-12）

当前维护线是此目录源码新生成的 `dist/index.html`。Node 回归 585 项、单文件/gzip 构建、主网页、拨弦、raw MIDI、恢复、启动和 FDC loopback Chrome 检查已实际通过。`dds-own-load.browser.mjs` 已修正中文路径解码并通过。

部分独立历史入口与维护线存在已确认的预期差异，未改业务或放宽断言来消除差异：`tests/v2-browser.mjs` 仍固定旧 8 声部预期，当前 mock/catalog 为 FM32；`tests/fm32.browser.mjs` 的 FM32/预置/布局等前 9 项通过后，在旧 FDC1 必须 disable-only 的断言处失败，而当前维护线已支持 FDC1 减法通路。`tests/fdc-return-sub.browser.mjs` 检查工程内保留的固件 HTML，该 HTML 仍是另一维护状态，FDC1 5 项失败、FDC2 兼容项通过。本次保留两份 HTML 的既有差异，没有把新网页覆盖到固件，也没有进行设备验证。因此不能宣称所有网页入口或设备链路已经通过。
