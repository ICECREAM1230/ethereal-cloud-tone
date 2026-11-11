# 可用产物与版本

本目录收录已经存在并核对SHA的产物，补充上传时没有编译、重新综合/PnR或编程设备。[manifest.json](manifest.json)记录实际公开文件SHA/字节、来源身份和元数据整理；原始二进制保持字节。公开JSON中的本机路径已归一，历史源哈希仍描述原构建，不作为当前源码新构建证明。

| 目录 | 用途与边界 |
| --- | --- |
| [fpga/frozen_20261104T034808Z](fpga/frozen_20261104T034808Z/) | 42,303,585 B参考FS及完整配套资源/时序/引脚报告。FS SHA `53868851a657598bca54604b196c820369f8b0230ec960e1854a3242b52c11ee`；来自11-04冻结工程，仍有setup/recovery/removal违例，未时序闭合，不能冒充当前main的新位流或新整机验收。 |
| [esp32_a/20261112](esp32_a/20261112/) | 当前A板ESP-IDF6.1实际成功构建：app 1,417,808 B，ELF、bootloader、分区表、2MiB DDS出厂存储镜像及栈预算。对应源码[esp32/a_board](../esp32/a_board/)，本次没有烧录。 |
| [esp32_b/20261103-serial-midi-channel](esp32_b/20261103-serial-midi-channel/) | B板已保存配套固件523,104 B、ELF和Flash参数；源码与当前B板2599文件逐字一致，原记录hardware_flashed=false。目录manifest区分历史绑定与当前公开文件SHA。 |
| [android/20261112](android/20261112/) | 0.10.0主APK、公开签名校验数据与instrumentation smoke APK；构建/签名/zipalign通过，未执行设备安装测试。APK含公开证书，私钥不提供。 |
| [web/current](web/current/) | 已构建HTML 220,398 B及gzip 63,291 B；gzip解压与HTML一致。这是独立网页维护线，不是自动替换A板内嵌页。 |
| [ae350_latest_reference](ae350_latest_reference/) | 55,152 B独立软件参考与ELF/栈资料；不同于FPGA内嵌55,228 B镜像，不得直接覆盖工程ITCM。 |

B板去重配置直接使用[dependencies.lock](../esp32/b_board/dependencies.lock)、[sdkconfig.defaults](../esp32/b_board/sdkconfig.defaults)和[partitions.csv](../esp32/b_board/partitions.csv)，目录manifest的path字段已给可解析相对位置。A板实际分区与[partitions.dds.csv](../esp32/a_board/config/partitions.dds.csv)对应；出厂存储镜像写入会覆盖对应数据区，不是普通应用升级的一部分。

校验：在仓库根执行`python3 -B tools/verify_related_files.py`。新机器的软件搭建见[环境指南](../docs/ENVIRONMENT.md)，验收范围见[VALIDATION.md](../docs/VALIDATION.md)。二进制存在、历史构建通过与当前实板验收是不同结论。
