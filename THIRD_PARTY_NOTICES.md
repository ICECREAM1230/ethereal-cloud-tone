# 第三方许可与作者声明

项目自有及已获重新授权的合成器代码使用根目录 MIT 许可，Copyright (c) 2026 ICECREAM1230；源文件中原有作者署名保留。以下独立组件保留自己的条款及版权声明。

| 组件 | 许可资料 |
| --- | --- |
| JT51 FM 与派生的 FM 专用代码 | `fpga/licenses/upstream/jt51/upstream/LICENSE` 及对应源码的 GPL-3.0 标注 |
| DaisySP PolyBLEP | `fpga/licenses/upstream/antialias/LICENSE.DaisySP`，MIT / Electrosmith |
| Pololu / ST VL53L0X 寄存器序列 | `fpga/licenses/vendor/vl53l0x/LICENSE.txt`，原 MIT 与 ST BSD 声明 |
| HDMI、USB PHY 及其他接口组件 | `fpga/third_party/hdmi/UPSTREAM_LICENSE` 与对应源码头中的 Apache、LGPL 等声明 |
| GCC 运行库 | `fpga/licenses/gcc_runtime/`，含 GCC Runtime Library Exception |
| tomlc17 | 各 ESP32 与合同目录内 `main/vendor/tomlc17/LICENSE` |
| Espressif / TinyUSB | `esp32/b_board/managed_components/` 内各自 LICENSE |
| Gowin 生成 IP 与模型 | 生成文件内的厂商许可条款；本仓库不提供 EDA 工具或许可证 |

混合来源的文件按局部声明区分，例如 KS 数值表使用项目 MIT，FM attenuation 表与 JT51 编译代码保留 GPL 声明。不能仅依据根目录 LICENSE 将所有第三方内容视为同一种许可。

## 开发技能与厂商手册

`skills/`保留各自的原作者和适用条款，仓库顶层MIT不替代其独立声明。两项外部来源技能保留原MIT LICENSE/SOURCE；厂商手册PDF和完整Markdown正文不在公开包中，仅有自行编写的定位页和版本SHA索引。Gowin/Qt库和许可证不随包提供；本机自编native桥不改变厂商运行库的条款，且有固定主机路径/ABI限制，见skills/README.md。
