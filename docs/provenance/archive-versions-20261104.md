> 2026-11-04历史归档来源说明。下述大小、源路径、位流和验收状态描述当时版本；当前公开产物及实际SHA见[artifacts/manifest.json](../../artifacts/manifest.json)，本轮结果见[发布检查](../VALIDATION.md)。

# 来源、版本与范围

FPGA：成功快照 c04cd847a067b737bab020ec2804a73dbc060bd6，生产源 b1ff9b8b16afece1c9645bb2fea49d27263368c6。源/impl 来自后续单点任务已冻结的 baseline/source-before.tar.gz 和 impl-before.tar.gz，使用前核对两份归档 SHA256。SYN114448/PnR120111 均有原始 exit0 日志。对照 352/364 输入、49 实现文件及 FS，保留源、实现和内嵌固件的一致性。

A 板：实际 AE350 主树 control_migration/esp32/esp32s3；版本 ae350-dds-depth-20261104，APP 1,417,824 B。原构建的 185 输入清单逐项核对当前副本。原完整 host 的既存 test_transport_progress.c:94 / fm60 失败继续保留，不能将专项通过说成全套通过。

B 板：实际“激光竖琴”工作树 firmware/b_board 与 host/b_board_debugger。APP 523,104 B；原始构建、烧录和 UART 编号 1 / public channel 2、velocity [20,500,127,16] 的记录一并保存。本次不读取设备状态。

Android：实际主库 development/用户界面开发/app，0.10.0 / versionCode12，APK 2,765,442 B。源目录含未提交内容，按文件 SHA256 取实际工作文件，不以旧 Git HEAD 代替。

网页：AE350 主树的最新 web-ui，原 dist 为 220,291 B，A 板 main/web/index.html 为 220,398 B，已有 FDC 页面等版本差异；两份原样保存，各自匹配来源。固件实际页面以 A 板 main/web/index.html 为准，未合并或重建网页。

PCM 上位机：AE350 主树为当前采集维护线；my_project 还有较早的独立 measurement 功能，不与当前版本拼装，保存为 pcm_measurement_reference 供查阅。

所有技术来源、许可、锁文件保留。历史文档描述它们各自日期的状态，不能代替本次数据清单或整套实板验收。
