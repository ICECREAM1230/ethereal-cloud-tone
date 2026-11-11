# 冻结的共享 FM 生成输入

这是 `development/tang_mega_138k_synth/interfaces/fm_control/` owner 的 schema、profiles、两份生成脚本及五语言生成物的逐字节副本。来源与 SHA 见 `../../verification/20261011-fm-http-v2/contract-input.json`。不在本包维护第二张 opcode／地址／参数表，不手改生成物或这份规格副本。

固件通过 `main/fm_contract.h` 消费生成的 C header。构建和 `tests/run.sh` 均执行 `tools/check_fm_contract.py`：先对冻结副本运行生成器 `--check`，在 canonical 仓库中再比对唯一共享来源。检查只读；若 owner 发布新冻结版本，由 S3 owner 按新交接同步整个副本并重新验证，不在这里独立改规格。独立恢复的 S3 包不需要 FPGA/C3 源码即可检查冻结输入与构建。

schema 1.0.0／ABI 1.1 规定命令 codec/catalog，不证明活动 FPGA 已实现 FM。当前 runtime gate 关闭，HTTP 的离线目录和未知状态不作为设备能力。
