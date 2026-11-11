# 本次最新数据：uart.txt

本目录替换原代码包的数据示例与数据描述。用户指定文件保持原字节：11,214B，SHA256 03065c2fec9f5ce9628f8fae244c99ebd1c64982c3abf1b1538a7846b2497b08。

- N20/N201，42条完整帧，CRC和格式全部通过。
- ID1..20齐全，22条相同重放，19条有效、ID13无效。
- 有效记录为RAW1→SUB；没有KS/FM有效样本。
- 按文件的50MHz和当前计数：M0→M1平均1.837270526316ms；M1→PCM平均5.016738947368ms；M0→PCM平均6.854009473684ms。
- 时间戳和timeout与此前“整数÷2、向下取整、重算CRC”的导出内容相同。本次只加入文件，没有再次变换。
- N201终点为abs(PCM16)>1，不是当前T04/48MHz的首个非零完整PCM24。此文件不是新T04采集，M0/M1/PCM不能改名为T0/T1/T2/T3。

uart.txt 是本次指定文件原样副本；raw-counts.csv 是20个唯一ID的计数导出，保留ID13；decoded.json 是当前N20解码器结果；summary.json说明来源、精确均值和口径。

在压缩包根目录执行（输出文件须尚不存在）：

```bash
python3 -B scripts/read_note20_uart.py --input latest_data/uart.txt --output latest_data/redecoded.json
```

预期退出码2，状态CAPTURED_WITH_INVALID_MEASUREMENTS，stream_integrity=PASS，19 qualified，22 repeats。这是一个完整含无效测量的文件，不能改报为20条全有效PASS。

不沿用其它数据的截图。原代码包和工程历史记录留在原位置，本更新包只以这份uart.txt作为数据示例。
