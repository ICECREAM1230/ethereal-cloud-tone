# AE350 软件参考

此处保存独立维护线的应用、控制运行时、FDC 集成、接口和 C 测试。主 FPGA 构建继续使用 `../../fpga/firmware/ae350/` 与其配套 IP 初始化数据；本参考目录不替换它们。公开源码仓库不包含历史构建镜像。

`control_migration/tests/run_control_service.py` 和 `run_prepared_cpu.py` 支持 `--output`、`--work-dir` 指定新的外部结果及临时目录，RV32 工具链通过 `--rv32-prefix` 传入或从 PATH 取得。`--host-only` 明确只执行主机 C 检查，不代表 RV32、FPGA 或硬件验收。

2026-11-12 公开前验证保留两项既存测试问题：service 旧入口缺少 `control_compile.c` 编译单元而无法链接；prepared 的 `test_control_compile.c:42` 包头断言与当前实现不一致，同一断言也在原始归档源码复现。未改变实现或放宽测试期望。

FDC 的 `build_target.py` 需要未随参考目录收录的 `sim/tb/fdc_ae350_firmware_entry.c`；`build_rtl_candidate.py` 需要原始修改前的 RTL 基线。缺失时入口会明确报出依赖，不使用当前主 FPGA 树代替历史基线。
