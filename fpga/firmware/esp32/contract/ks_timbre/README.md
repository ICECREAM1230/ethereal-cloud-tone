# KS timbre v1 契约

本目录的 `schema.json` 是常量、64-word索引和CRC参数的唯一生成输入。运行 `python3 generate.py` 更新 C/SV/Python；`python3 generate.py --check` 只验证一致性。完整语义依据 `docs/ks-timbres/2026-10-26-v1-design.md` 和本任务冻结的 contract-brief；这里没有改变协议。

- C: `generated/c/ks_timbre_contract.h`，以 include directory 加入固件与 host 编译。
- SV: `generated/sv/ks_timbre_contract_pkg.sv`。
- Python: `generated/python/ks_timbre_contract.py`。
- 模型头 packed bus 中 word i 为 `words[i*32+:32]`；文件 hex 每行一个 word，按索引升序。
- CRC-32/ISO-HDLC：反射多项式 `0xedb88320`，初值与最终异或 `0xffffffff`；**每个32位word先送最高字节**。

`model_crc` 覆盖64个头word（3/22/60/61置零）及256个note word（奇数word只保留低16调音系数）。`image_crc` 覆盖256 note word、128 pan word、64头word（61置零）及一个template_generation。普通rho/pan/decay/运行generation变化保持model_crc；同generation刷新仍需接收端逐word核对允许字段，CRC不替代该检查。

Legacy规范化：版本0x10001、id0、factory_revision1、Fs=91015625/948、w=16384、release=960/68/256、dry32768、fade480、flags0、valid mask对应MIDI19–127。其余physical字段为0；gen/decay/CRC与有效音符按当前图像设置。B3FF由接收端生成这些字，不能读取上一音色遗留头。

Q格式及更新顺序见 `physical_modeling/ks_timbres/README.md`。生成器仅生成声明，不把实验音色参数混入接口常量。修改布局或CRC需要协调全部生产者与消费者并更新模型版本。
