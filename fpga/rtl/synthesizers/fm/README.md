# FM 合成器

`fm_engine.sv` 为 FM 引擎入口；`control/`、`audio/` 保存控制及音频适配，`ip/` 保存本模块 IP。

- `jt51/hdl/`：完整的 22 份 JT51 上游 Verilog，原样归入 FM 目录。
- `native96/`：现有 native96 实现与时序适配。
- `native32/`：现有 32 声部实现与来源说明。

本次仅迁移 JT51 文件位置并更新工程引用，模块名、RTL 内容、编译顺序和启用状态保持。
上游版权头保留，许可见 [JT51 GPL-3 许可证](../../../licenses/upstream/jt51/upstream/LICENSE)。
原始搬迁清单与成功构建记录保持历史身份；本次路径证据见 [迁移记录](../../../../evidence/fm-rtl-relocation/receipt.json)。
