# IP与初始化文件覆盖

初始公开版本已经包含归档现存的73项IP文件、30组目录：`fpga/ip/`有35项，RTL内部IP目录有38项。逐文件HEAD对象和工作树SHA匹配，没有被忽略规则屏蔽的已知IP源。71项与封存来源完全相同，2份减法DSP包装只改版权和许可注释，去注释逻辑相同。

主要入口为[AE350](../fpga/ip/ae350/)、[核心PLL](../fpga/ip/core_pll/)、[DSP包装](../fpga/ip/dsp/)、[视频PLL](../fpga/ip/video_pll/)；其余位于`fpga/rtl/**/ip/`、[板级时钟](../fpga/rtl/board/ip/)及[CH569 FIFO](../fpga/rtl/debug/ch569/pcm_sample_async_fifo_top/)。JT51、I2C、USB PHY、HDMI等配套保留自身许可。

Gowin清单278条、276条启用；364项官方扫描输入及独立process_config均已跟踪。另核查目录外2份OSER10包装、40份第三方IP配套、31份初始化/头文件；36处显式初始化引用对应16个唯一文件，均存在于仓库。

[control.bin](../fpga/firmware/ae350/images/control.bin)为55,228 B，与[AE350生成模型](../fpga/ip/ae350/RiscV_AE350_SOC_Top.vo)中32个SP重建的64KiB内容逐字相符。[libgcc.a](../fpga/third_party/gcc_runtime/rv32im/ilp32/libgcc.a)、四个ITCM lane及相关生成记录保留。

部分历史PLL/RAM/FIFO/FFT只保存了生成RTL，原归档也没有相应.ipc；这不是漏传，但不能承诺全部IP可仅凭现有配置重新生成。历史legacy IPC中的旧生成目录需在新机器重新选择；生成RTL来源注释不是运行时外部文件依赖。

control-build.json保留3个头文件的原构建源码SHA，现文件只改许可/版权注释；没有将收据改写成新固件编译。生成程序、ITCM和运行库字节绑定通过。查验覆盖与厂商实现、STA或板测不同，本轮没有执行新EDA流程。

只读输入检查：`python3 -B tools/verify_project.py`。硬件结构见[FPGA架构](FPGA_ARCHITECTURE.md)，环境见[搭建指南](ENVIRONMENT.md)。
