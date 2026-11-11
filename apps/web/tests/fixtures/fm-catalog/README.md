# FM catalog 只读／人工 UI fixture

真实目录来自 `src/generated/fm-contract.mjs`，通过 `createFmCatalogPreview()` 转为内部只读模型。没有实际当前值、HTTP adapter、硬件状态或设备代理。预置只列名称和资料。

`model.available=true` 仅表示本地目录资料可浏览，允许声部导航；它不代表 `runtime_available`。参数统一为 `value:null`、`editable:false`。JT51 的 2 个 PATCH 参数及 4 个共享 Patch 算子中各自的 11 个 PATCH_OPERATOR 参数与声部布局分开。

`ui-models.mjs` 的 patch2／voice6 是人工 UI 输入，专门验证共享 patch 与所选 voice 的维度、逻辑目标、权限和活动拖动；不代表 JT51 硬件配置。

```bash
node --test tests/fm-catalog.test.mjs
node tests/fm-catalog.browser.mjs --stage green
```

浏览器报告默认保存在 `artifacts/phase2-20261011/fm-catalog/<stage>-<run>/`，每次新目录。通过 `WEB_UI_FM_CATALOG_ARTIFACT_DIR=/path/to/output` 设置报告父目录，便于主代理在恢复副本验证。`WEB_UI_BROWSER` 可指定系统 Chromium/Chrome 路径；固定 Playwright 1.63.0。
