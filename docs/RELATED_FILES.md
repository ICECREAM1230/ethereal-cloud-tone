# 相关文件收录范围

仓库包含当前工程源码、实际IP/初始化、接口合同和必要离线依赖，并补充开发skills、已有可用产物与独立历史实验数据。复刻安装步骤见[README](../README.md)及[ENVIRONMENT](ENVIRONMENT.md)。

| 分类 | 当前入口 |
| --- | --- |
| 实际IP与依赖 | [IP覆盖](IP_INVENTORY.md)、[fpga](../fpga/)；73项IP/364官方输入 |
| 当前skills | [skills](../skills/README.md)；33项，包含两项来源中未启用的技能，不自动启用 |
| 技术报告 | [正式技术报告](technical/README.md)，2026-11-14 Word版，132页；附SHA256校验清单 |
| 固件、APK、网页、参考FS与报告 | [artifacts](../artifacts/README.md)；逐项SHA与版本边界 |
| KS/FM历史实测 | [实验数据](../experiments/ks_fm_20261102/README.md)；10段录音、RAW/PCM/WAV、分析图稿与代码闭包 |
| 当前介绍、架构和复刻说明 | [项目介绍](PROJECT_OVERVIEW.md)、[架构](ARCHITECTURE.md)、[环境](ENVIRONMENT.md) |

8套官方手册正文因再分发限制没有放入公开仓库，skills保留其版本、SHA和获取线索；原件仍在本地归档。Gowin/Qt/OSS工具、SDK、授权文件、虚拟环境、node_modules和可重建缓存由复刻机器准备。

完整历史恢复包、旧Git数据和不匹配当前工程的独立候选不作为当前运行依赖搬入；私钥、开发keystore、个人审计/编译数据库不公开。历史参考产物、技术文档与录音明确保留其日期，不能混写为当前main的新构建或板测结果。

完整性检查：`python3 -B tools/verify_related_files.py`。该入口只读文件清单、源码输入、skills和历史资料的SHA，不构建、不安装、不编程硬件。
