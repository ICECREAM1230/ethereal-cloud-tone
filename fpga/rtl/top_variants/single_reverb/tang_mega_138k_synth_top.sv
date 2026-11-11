`default_nettype none
module tang_mega_138k_synth_top #(
    parameter bit ENABLE_MIDI_STATUS_LED = 1'b1, // MIDI 状态灯：1=启用，0=关闭
    parameter bit ENABLE_MIDI_DEBUG_UART = 1'b0, // MIDI 调试串口：默认关闭，“全模块开启”时也保持关闭；仅专项诊断时手动开启，会显著增加 BRAM 占用
    parameter bit SHARE_ADDITIVE_DDS = 1'b1, // 加法 DDS 共享：1=共享，0=独立；加法合成器启用时生效
    parameter bit ENABLE_REVERB = 1'b1, // 混响 DSP：1=启用，0=旁路；SPI 混响控制接口仍保留
    parameter logic [15:0] REVERB_WET_Q15 = 16'd0000, // 混响湿声比例默认值（Q15）；8192 对应 25%，可由 SPI 更新
    parameter logic [15:0] REVERB_DAMP_Q15 = 16'd0000, // 混响阻尼默认值（Q15）；8192 对应 0.25，可由 SPI 更新
    // 合成器编译使能：修改后需重新综合，并同步手工 SDC 中的对应模块约束。
    parameter bit ENABLE_SUBTRACTIVE = 1'b1, // 减法合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_KS_TIMBRES = 1'b0, // KS竖琴/古筝：0保留旧KS控制与数值路径
    parameter bit ENABLE_KS_LIMITER = 1'b1, // KS动态峰值限幅：0恢复保守/16输出，1启用正常增益及限幅
    parameter bit ENABLE_KS = 1'b1, // KS 物理建模合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_FM = 1'b1, // FM 合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_ADDITIVE = 1'b0, // 加法合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_FLUTE = 1'b0, // 笛物理模型：1=启用独立模式4，0=移除电路及模式；首版normal单声部
    parameter bit ENABLE_OUTPUT_TILT = 1'b0, // 输出低通滤波：1=启用，0=旁路；同时作用于普通 PCM 与 FM PCM
    parameter integer OUTPUT_TILT_SHIFT = 3, // 默认3；2/4分别用于更亮/更暗的数字录音对照
    parameter bit ENABLE_HDMI = 1'b0, // HDMI示波/FFT：0移除整条视频链
    parameter bit ENABLE_FFT = 1'b0, // KEY1切换FFT；0只保留示波器
    parameter bit ENABLE_SENSOR_I2C = 1'b0, // 传感器采集：1=综合 FDC2214、双 VL53L0X 和共享 Master，0=移除整组电路；启用前落实注释中的引脚
    parameter integer SENSOR_I2C_HZ = 100_000, // 从系统 50MHz 时钟产生 I²C 位时序，不新增逻辑时钟域
    parameter bit FM_DIAG_LED_ACTIVE_LOW = 1'b1 // FM 诊断灯极性：1=低电平点亮，0=高电平点亮
) (
    input  wire       sys_clk,
    input  wire       sys_rst_n,
    input  wire [1:0] Key_in,
    input  wire       ESP_SPI_SCLK,
    input  wire       ESP_SPI_CS_N,
    input  wire       ESP_SPI_MOSI,
    inout  wire       USB_MIDI_DP,
    inout  wire       USB_MIDI_DM,
    output wire       ESP_SPI_MISO,
    output wire       MIDI_STATUS_LED,
    output wire       MIDI_DEBUG_TX,
    // ESP32 uses J8/PMOD1 only.
    input  wire [3:0] ESP_PMOD1_PASSIVE,
    // FM 诊断灯默认移除；恢复时定义此宏，并恢复 CST 中 FM_DIAG_LED 约束。
`ifdef SYNTH_FM_DIAG_LEDS
    output wire [7:0] FM_DIAG_LED,
`endif
    output wire       HP_BCK,
    output wire       HP_WS,
    output wire       HP_DIN,
    output wire       PCM_BCK,
    output wire       PCM_WS,
    output wire       PCM_DIN,
    output wire       PA_EN,
    output wire       tmds_clk_p,
    output wire       tmds_clk_n,
    output wire [2:0] tmds_data_p,
    output wire [2:0] tmds_data_n
    // parameter 不能删除模块端口。最终引脚确认后，定义该宏才声明物理 I/O；
    // 当前默认不定义，避免关闭传感器时出现未约束、被自动选脚的端口。
`ifdef SYNTH_SENSOR_I2C_PORTS
    ,inout  wire      scl_io,
    inout   wire      sda_io,
    output  wire      fdc_addr_o,
    output  wire      fdc_sd_o,
    input   wire      fdc_int_ni,
    inout   wire      tof1_scl_io,
    inout   wire      tof1_sda_io,
    inout   wire      tof1_xshut_io,
    input   wire      tof1_int_ni,
    inout   wire      tof2_scl_io,
    inout   wire      tof2_sda_io,
    inout   wire      tof2_xshut_io,
    input   wire      tof2_int_ni
`endif
);
    // Only the board-wide reset drives the dedicated, active-low GSR.
    // PLL-qualified domain resets retain their local synchronous release;
    // ready/ack/flush/route controls must never reset unrelated domains.
    GSR GSR (.GSRI(sys_rst_n));

    synth_system #(
        .ENABLE_MIDI_STATUS_LED(ENABLE_MIDI_STATUS_LED),
        .ENABLE_MIDI_DEBUG_UART(ENABLE_MIDI_DEBUG_UART),
        .SHARE_ADDITIVE_DDS(SHARE_ADDITIVE_DDS),
        .ENABLE_REVERB(ENABLE_REVERB),
        .REVERB_WET_Q15(REVERB_WET_Q15),
        .REVERB_DAMP_Q15(REVERB_DAMP_Q15),
        .ENABLE_SUBTRACTIVE(ENABLE_SUBTRACTIVE),.ENABLE_KS(ENABLE_KS),.ENABLE_KS_LIMITER(ENABLE_KS_LIMITER),.ENABLE_KS_TIMBRES(ENABLE_KS_TIMBRES),
        .ENABLE_FM(ENABLE_FM),.ENABLE_ADDITIVE(ENABLE_ADDITIVE),.ENABLE_FLUTE(ENABLE_FLUTE),
        .ENABLE_OUTPUT_TILT(ENABLE_OUTPUT_TILT),
        .OUTPUT_TILT_SHIFT(OUTPUT_TILT_SHIFT),
        .ENABLE_HDMI(ENABLE_HDMI),.ENABLE_FFT(ENABLE_FFT),
        .ENABLE_SENSOR_I2C(ENABLE_SENSOR_I2C),.SENSOR_I2C_HZ(SENSOR_I2C_HZ),
        .SHARED_REVERB(1'b1),.FM_DIAG_LED_ACTIVE_LOW(FM_DIAG_LED_ACTIVE_LOW)
    ) u_system(
        .sys_clk(sys_clk),
        .sys_rst_n(sys_rst_n),
        .Key_in(Key_in),
        .ESP_SPI_SCLK(ESP_SPI_SCLK),
        .ESP_SPI_CS_N(ESP_SPI_CS_N),
        .ESP_SPI_MOSI(ESP_SPI_MOSI),
        .USB_MIDI_DP(USB_MIDI_DP),
        .USB_MIDI_DM(USB_MIDI_DM),
        .ESP_SPI_MISO(ESP_SPI_MISO),
        .MIDI_STATUS_LED(MIDI_STATUS_LED),
        .MIDI_DEBUG_TX(MIDI_DEBUG_TX),
        .ESP_PMOD1_PASSIVE(ESP_PMOD1_PASSIVE),
`ifdef SYNTH_FM_DIAG_LEDS
        .FM_DIAG_LED(FM_DIAG_LED),
`else
        .FM_DIAG_LED(),
`endif
        .HP_BCK(HP_BCK),
        .HP_WS(HP_WS),
        .HP_DIN(HP_DIN),
        .PCM_BCK(PCM_BCK),.PCM_WS(PCM_WS),.PCM_DIN(PCM_DIN),
        .PA_EN(PA_EN),
        .tmds_clk_p(tmds_clk_p),
        .tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p),
        .tmds_data_n(tmds_data_n)
`ifdef SYNTH_SENSOR_I2C_PORTS
        ,.scl_io,.sda_io,.fdc_addr_o,.fdc_sd_o,.fdc_int_ni,
        .tof1_scl_io,.tof1_sda_io,.tof1_xshut_io,.tof1_int_ni,
        .tof2_scl_io,.tof2_sda_io,.tof2_xshut_io,.tof2_int_ni
`endif
    );
endmodule
`default_nettype wire
