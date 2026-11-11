# FM 面板纯界面 fixture

这些数据仅描述内部呈现模型，不是 HTTP schema、芯片 profile、真实设备能力或实板音频验收。测试只在 `127.0.0.1` 的白名单静态文件服务加载实际 `src/fm-panel.mjs`，不启用 mock 设备或同源设备代理。

运行：

```bash
/home/ice/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node tests/fm-panel.browser.mjs --stage green
```

测试和截图仅写入 `artifacts/phase2-20261011/fm-panel/`。`--stage red` 用于保留最初缺失组件时的失败报告，不修改源码。

内部接口由 `src/fm-panel.mjs` 的 JSDoc 说明；`contextId` 必须由 adapter 生成。fixture 的 `contextId`、backend、voice 和 operator 身份都是任意界面示例，不得用于网络请求。
