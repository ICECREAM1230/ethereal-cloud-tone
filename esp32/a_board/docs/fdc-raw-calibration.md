# FDC 原始数据与音量标定

`ae350-fdc-raw-20261101` 新增只读原始数据。ESP32 仍通过唯一 engine task 读取 FPGA SPI；HTTP handler 只读 RAM，不直接访问 IIC/SPI，不启停传感器或声音。

需要配套支持 FDR1 的 FPGA 位流。仅更新 ESP32、仍运行旧8827位流时，网页显示“此码流尚未提供 FDC 原始数据”，`raw.supported=false`，原有 FDC 启停和 MIDI 保持可用，不用0冒充传感器值。

访问 `GET /api/v2/fdc`，原有字段/contract_version1保持，追加 `raw`：

- `ch0` / `ch1`：完整快照里的未滤波28bit原码。未有完整采样时为null。
- `gain_q16` / `gain_percent`：当前控制算法的幅度系数，65536=100%。不是DAC实测幅度或native应用回执。
- `valid`：读取到前后commit guard一致的SPI快照。
- `fresh`：快照发布仍更新且原始采样未过期；不能用连续HTTP读来给旧数据续命。
- `sample_valid`：原始双通道帧存在；质量标志和driver_fault仍须同时检查。
- `sample_fresh`：CPU按源时间保守计算采样未超过343ms。
- `quality_ch0/ch1`：原始AW/WD两位标志，bit0=AW、bit1=WD。
- `sequence`：源采样序号低8bit，可回绕；`revision`：32bit诊断发布序号，用于快照一致性。
- `muted/pitch_stale/volume_stale`：算法静音/过期原因。

网页FDC面板会显示两路原码和增益，支持FDR1时约200ms刷新。命令行采集约100ms一次：

```bash
python3 tools/fdc_raw_log.py --url http://ESP32地址 --out fdc_raw.csv --seconds 30
```

脚本只发GET，保留无效/过期状态以供排查，不自动启用FDC、不写校准值、不重发演奏。已有CSV不会覆盖。测量时分别把手保持在希望的远端/近端位置数秒，以完整、新鲜且无WD/驱动错误的数据估计端点与静止噪声sigma，再通过原六字段配置界面提交。

本版默认音量范围：far=13210613、near=13174639，跨度35974；旧范围far13246586、near13174639，跨度71947。近端静音点/平方曲线/滤波/0–100%上限保持，far向near收窄，避免反向修改使同一读数更小。新默认仍是历史实验参数的临时缩窄，不代表已完成本机天线标定。用户手动提交的配置以提交值为准。

FPGA只读寄存器：0730=`FDR1`/46445231，0731=32bit publication serial，0732=raw0/quality/present/fresh，0733=raw1同格式，0734=gain/flags/source sequence。CPU在APB174/178/17C暂存payload，再写170提交；未凑齐三项不更新原始快照。ESP分阶段读payload后重读0731，guard变化/超时/错误即丢弃；旧071F状态与原有控制协议独立保留。
