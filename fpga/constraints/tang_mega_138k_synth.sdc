# 当前顶层literal开关对应的手工活动SDC；由模块化模板离线渲染。
# auto_generate=false保持；改开关后必须同步维护本文件。
# Working template: constraints/templates/tang_mega_138k_synth_modules.sdc
# Preserved original: constraints/tang_mega_138k_synth.sdc (not a generation input)
# Top defaults: ENABLE_SUBTRACTIVE=1, ENABLE_KS=1, ENABLE_FM=1, ENABLE_ADDITIVE=0, ENABLE_REVERB=1, SHARE_ADDITIVE_DDS=1, ENABLE_OUTPUT_TILT=0, ENABLE_MIDI_STATUS_LED=1, ENABLE_MIDI_DEBUG_UART=0, ENABLE_HDMI=0, ENABLE_SENSOR_I2C=0
# Inactive blocks retain their complete commands as DISABLED comments.

# 唐 Mega 138K：音频分支回合主工程的模块化约束候选（2026-10-24）。
# 保留主工程01–14章节、功能开关、传感器与RVR1、外部I/O预算说明。
# 本文件是 prepare_engine_constraints.py 的唯一生成输入。
# auto_generate=false 时保留主工程手工模式及活动原文件名；不改 .gprj.user。
# auto_generate=true 时由原Python/TclPre流程生成并选择 engine_gates.sdc。
# 开关只读取主single_reverb顶层的literal bit默认值；不应用额外GUI/Tcl参数覆盖。
# 本次只做离线静态候选；实际对象命中、例外优先级及物理裕量需后续官方验证。
# 所有活动命令位于 @block 内；停用命令仍完整保留。无新增MCP、猜测I/O delay。
# 时钟名：board_ref_clk=50MHz，control_clk=USB48共享，subtractive_clk≈73.734MHz，
# pcm_audio_clk≈24.578MHz，fm_native_clk=16MHz。sys_clk端口名称不改。
# RVR1仍在subtractive_clk；原子{source,mute,test,wet,damp,revision}经u_controls进入pcm_audio_clk。
# 不保留旧板级参考→业务域的宽clock_groups，也不对业务域建立新宽切断。
# 像素与串行时钟仍来自同一视频PLL，保持1:5关系及30ns快照预算。

# @block shared.clocks when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 01 公共时钟与时钟关系 [常驻]
# ============================================================================================
# 01.1 板级参考、SPI和四个业务域；不为USB/KS共钟重复建钟。
create_clock -name board_ref_clk -period 20.000 -waveform {0.000 10.000} [get_ports {sys_clk}]
create_generated_clock -name control_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 24 -divide_by 25 [get_pins {u_system/u_control_input/u_usb_midi/usb_pll/u_pll/PLL_inst/CLKOUT0}]
create_clock -name esp_spi_sclk -period 100.000 -waveform {0.000 50.000} [get_ports {ESP_SPI_SCLK}]
create_generated_clock -name subtractive_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 158 [get_pins {u_system/u_clock_reset/u_subtractive_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name pcm_audio_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 474 [get_pins {u_system/u_clock_reset/u_audio_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name fm_native_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 8 -divide_by 25 [get_pins {u_system/u_clock_reset/u_fm_pll/u_pll/PLL_inst/CLKOUT0}]
# @end shared.clocks

# @block display.clocks when=ENABLE_HDMI
# ENGINE_GATES: INACTIVE [ENABLE_HDMI=0; manual constraints synchronized]
# 01.2 视频PLL [ENABLE_HDMI]；保留主工程g_hdmi层级。
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: create_generated_clock -name video_pixel_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 60 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT0}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: create_generated_clock -name video_serial_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 12 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT1}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: create_generated_clock -name video_reset_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 60 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT2}]
# @end display.clocks

# @block shared.clock_relationships when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# 01.3 时钟关系
# 删除旧参考域宽异步组：sensor启用后的50MHz↔48MHz载荷不得被掩盖。
# 也不导入源control↔video宽切断；显示的30ns载荷与像素/串行关系继续计时。
# 外部原始复位和真正异步的首级/资格引脚在所属章节单独例外。
# @end shared.clock_relationships

# @block sensor.cdc_budgets when=ENABLE_SENSOR_I2C
# ENGINE_GATES: INACTIVE [ENABLE_SENSOR_I2C=0; user confirmed 2026-10-25]
# 01.4 传感器可选输入：采集仍50MHz，modulation目的改为control48。
# SPI↔board_ref和board_ref↔control保留20ns与原hold-only预算。
# 只豁免原有首级D；meta→sync、held载荷、业务复位继续计时。
# DISABLED: set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {board_ref_clk}] 20.000
# DISABLED: set_max_delay -from [get_clocks {board_ref_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
# DISABLED: set_max_delay -from [get_clocks {board_ref_clk}] -to [get_clocks {control_clk}] 20.000
# DISABLED: set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {board_ref_clk}] 20.000
# DISABLED: set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {board_ref_clk}]
# DISABLED: set_false_path -hold -from [get_clocks {board_ref_clk}] -to [get_clocks {esp_spi_sclk}]
# DISABLED: set_false_path -hold -from [get_clocks {board_ref_clk}] -to [get_clocks {control_clk}]
# DISABLED: set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {board_ref_clk}]
# DISABLED: set_false_path -to [get_pins {u_system/u_control_input/u_sensor/g_sensor.u_registers/req_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_registers/ack_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_registers/armed_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_registers/cs_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_registers/reject_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_registers/read_meta_s0/D}]
# DISABLED: set_false_path -to [get_pins {u_system/u_control_input/u_sensor/g_sensor.u_modulation_cdc/req_meta_s0/D u_system/u_control_input/u_sensor/g_sensor.u_modulation_cdc/ack_meta_s0/D}]
# @end sensor.cdc_budgets

# @block shared.reset_keys when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 02 板级复位与按键 [常驻]
# ============================================================================================
# 原始外部复位和机械按键的异步入口。按键只例外到首级D，后级及消抖/帧控制继续计时。
set_false_path -from [get_ports {sys_rst_n}]
set_false_path -from [get_ports {Key_in[0] Key_in[1]}] -to [get_pins {u_system/u_chord_keys/key_meta_*_s0/D}]

# 02.1 源workingSDC定向PLL锁定例外；仅到control_clk，不切同域数据。
set_false_path -through [get_pins {u_system/u_control_input/u_usb_midi/usb_pll/u_pll_init/ks_clock_locked_s/F}] -to [get_clocks {control_clk}]
# @end shared.reset_keys

# @block input.spi_raw when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 03 ESP32 SPI与RAW MIDI接口 [常驻]
# ============================================================================================
# 层级：u_system/u_control_input/u_transport。音源关闭后，SPI控制与RAW MIDI输入仍可存在。

# --------------------------------------------------------------------------------------------
# 03.1 SPI↔synth载荷/读回预算
# --------------------------------------------------------------------------------------------
# 原预算20 ns保留；载荷由握手保持。全域hold-only例外的业务复位覆盖风险仍待专门处理。
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}]

# --------------------------------------------------------------------------------------------
# 03.2 RAW mailbox首级同步例外
# --------------------------------------------------------------------------------------------
# 只切req_meta/ack_meta的D，保留meta→sync与载荷抓取路径。
set_false_path -to [get_pins {u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/req_meta_s0/D u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/ack_meta_s0/D}]

# --------------------------------------------------------------------------------------------
# 03.3 SPI与RAW mailbox专项报告
# --------------------------------------------------------------------------------------------
report_timing -setup -to [get_pins {u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/*/D* u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/*/CE* u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/*/W* u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/*/RESET* u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {esp_spi_sclk}] -to_clock [get_clocks {esp_spi_sclk}] -max_paths 10
report_timing -setup -from_clock [get_clocks {esp_spi_sclk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 20
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {esp_spi_sclk}] -max_paths 20

# --------------------------------------------------------------------------------------------
# 03.4 外部I/O时序预算占位
# --------------------------------------------------------------------------------------------
# 待根据ESP32时序与板线延迟补MOSI/CS输入和MISO输出的max/min delay。
# 此处没有填写猜测数值；create_clock不能替代外部I/O时序预算。

# 03.5 SPI↔共享control48：RAW mailbox和公共控制/读回。
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}]
# @end input.spi_raw

# @block input.usb when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 04 USB MIDI输入与48 MHz共享域 [常驻]
# ============================================================================================
# 层级：u_system/u_control_input/u_usb_midi。48 MHz与KS共用，协议复位不等同于KS域复位。

# --------------------------------------------------------------------------------------------
# 04.1 USB packet/flush、local请求/释放及Host完成已共control48。
# 所有载荷和同步器D保留普通setup/hold；不沿用旧USB→synth跨域预算。

# --------------------------------------------------------------------------------------------
# 04.2 USB PHY、复位限定器与握手首级
# --------------------------------------------------------------------------------------------
# 只豁免异步接收首级D和原始断言引脚；同步释放后级与业务路径继续计时。
set_false_path -from [get_ports {USB_MIDI_DP USB_MIDI_DM}] -to [get_pins {u_system/u_control_input/u_usb_midi/phy/*_ms_s0/D}]
set_false_path -to [get_pins {u_system/u_control_input/u_usb_midi/reset_release_0_s0/PRESET u_system/u_control_input/u_usb_midi/reset_release_1_s0/PRESET u_system/u_control_input/u_usb_midi/reset_release_2_s0/PRESET}]
set_false_path -to [get_pins {u_system/u_control_input/u_usb_midi/*reset_release*/PRESET}]
set_false_path -to [get_pins {u_system/u_control_input/u_usb_midi/transfer/src_reset_meta_s0/CLEAR u_system/u_control_input/u_usb_midi/transfer/src_reset_sync_s0/CLEAR}]

# --------------------------------------------------------------------------------------------
# 04.3 USB复位与域内/返回路径报告
# --------------------------------------------------------------------------------------------
report_timing -recovery -to [get_pins {u_system/u_control_input/u_usb_midi/*reset_release*/PRESET}] -max_paths 20
report_timing -removal -to [get_pins {u_system/u_control_input/u_usb_midi/*reset_release*/PRESET}] -max_paths 20
report_timing -setup -from_clock [get_clocks {control_clk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 20
report_timing -setup -from_clock [get_clocks {control_clk}] -to_clock [get_clocks {control_clk}] -max_paths 10
report_timing -hold -from_clock [get_clocks {control_clk}] -to_clock [get_clocks {control_clk}] -max_paths 10
# @end input.usb

# @block midi.decode when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 05 公共MIDI解码与接纳统计 [常驻]
# ============================================================================================
# 层级：u_system/g_decode.u_midi_decoder，以及u_synth_manager/g_midi公共计数器。
# 各音源自己的MIDI stage报告放在06/07/08的对应音源段。

# --------------------------------------------------------------------------------------------
# 05.1 公共解码与接纳计数
# --------------------------------------------------------------------------------------------
report_timing -setup -to [get_pins {u_system/g_decode.u_midi_decoder/*/D* u_system/g_decode.u_midi_decoder/*/CE* u_system/g_decode.u_midi_decoder/*/W* u_system/g_decode.u_midi_decoder/*/RESET* u_system/g_decode.u_midi_decoder/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_midi.accepted_event_count_*/D* u_system/u_synth_manager/g_midi.accepted_event_count_*/CE*}] -max_paths 25 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# 05.2 各输入源parser
# --------------------------------------------------------------------------------------------
# 这些是专项诊断，不会豁免parser的真实时序路径。
report_timing -setup -to [get_pins {u_system/g_decode.u_midi_decoder/hub/sources[0].parser/*/D* u_system/g_decode.u_midi_decoder/hub/sources[0].parser/*/CE* u_system/g_decode.u_midi_decoder/hub/sources[0].parser/*/RESET* u_system/g_decode.u_midi_decoder/hub/sources[0].parser/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/g_decode.u_midi_decoder/hub/sources[1].parser/*/D* u_system/g_decode.u_midi_decoder/hub/sources[1].parser/*/CE* u_system/g_decode.u_midi_decoder/hub/sources[1].parser/*/RESET* u_system/g_decode.u_midi_decoder/hub/sources[1].parser/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/g_decode.u_midi_decoder/hub/sources[2].parser/*/D* u_system/g_decode.u_midi_decoder/hub/sources[2].parser/*/CE* u_system/g_decode.u_midi_decoder/hub/sources[2].parser/*/RESET* u_system/g_decode.u_midi_decoder/hub/sources[2].parser/*/SET*}] -max_paths 25 -max_common_paths 1
# DISABLED [RAW_SOURCE3_ENABLE=0 in synth_system.sv] report_timing -setup -to [get_pins {u_system/g_decode.u_midi_decoder/hub/sources[3].parser/*/D* u_system/g_decode.u_midi_decoder/hub/sources[3].parser/*/CE* u_system/g_decode.u_midi_decoder/hub/sources[3].parser/*/RESET* u_system/g_decode.u_midi_decoder/hub/sources[3].parser/*/SET*}] -max_paths 25 -max_common_paths 1
# @end midi.decode

# @block board.fm_diag_control when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# 05.3 FM诊断灯控制快照：control48→subtractive73，WIDTH=3。
# {fm_diag_requested,local_switch_mute,parsed_note_seen_control}原子发布。
# 诊断模块仍使用subtractive73；载荷由共享control↔subtractive 20ns约束。
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -to [get_pins {u_system/u_fm_diag_control_cdc/req_meta_s0/D u_system/u_fm_diag_control_cdc/ack_meta_s0/D}]
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -to [get_pins {u_system/u_fm_diag_control_cdc/src_reset_meta_s0/CLEAR u_system/u_fm_diag_control_cdc/src_reset_sync_s0/CLEAR u_system/u_fm_diag_control_cdc/dst_reset_meta_s0/CLEAR u_system/u_fm_diag_control_cdc/dst_reset_sync_s0/CLEAR}]
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_max_delay -from [get_clocks {control_clk}] -to [get_pins {u_system/u_fm_diag_control_cdc/dst_data_o_*_s0/D}] 20.000
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -hold -from [get_clocks {control_clk}] -to [get_pins {u_system/u_fm_diag_control_cdc/dst_data_o_*_s0/D}]
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_fm_diag_post_cdc/dst_data_o_*_s0/D}] 20.000
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_fm_diag_post_cdc/dst_data_o_*_s0/D}]
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -to [get_pins {u_system/u_fm_diag_post_cdc/req_meta_s0/D u_system/u_fm_diag_post_cdc/ack_meta_s0/D}]
# DISABLED [SYNTH_FM_DIAG_LEDS undefined] set_false_path -to [get_pins {u_system/u_fm_diag_post_cdc/src_reset_meta_s0/CLEAR u_system/u_fm_diag_post_cdc/src_reset_sync_s0/CLEAR u_system/u_fm_diag_post_cdc/dst_reset_meta_s0/CLEAR u_system/u_fm_diag_post_cdc/dst_reset_sync_s0/CLEAR}]
# @end board.fm_diag_control

# @block synth.subtractive when=ENABLE_SUBTRACTIVE
# ENGINE_GATES: ACTIVE [ENABLE_SUBTRACTIVE=1]
# ============================================================================================
# 06 减法合成器及内部DDS [可选，默认启用]
# ============================================================================================
# 主层级：u_system/u_synth_manager/g_subtractive.u_engine。
# ENABLE_SUBTRACTIVE=0时注释本节的专用报告；共享synth_clk仍由其他逻辑使用，保留01节。

# --------------------------------------------------------------------------------------------
# 06.1 减法MIDI stage与滤波器
# --------------------------------------------------------------------------------------------
# 滤波器、CSP等路径继续受synth域和全局STA覆盖。
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_midi.dds_stage/*/D* u_system/u_synth_manager/g_midi.dds_stage/*/CE* u_system/u_synth_manager/g_midi.dds_stage/*/W* u_system/u_synth_manager/g_midi.dds_stage/*/RESET* u_system/u_synth_manager/g_midi.dds_stage/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/C* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/D* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/OCE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/W* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/RESET* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/u_filter/*/SET*}] -max_paths 25 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# @end synth.subtractive

# @block synth.subtractive_internal_dds when=ENABLE_SUBTRACTIVE and not ENABLE_ADDITIVE
# ENGINE_GATES: ACTIVE [ENABLE_ADDITIVE=0, ENABLE_SUBTRACTIVE=1]
# 06.2 内部DDS：倒数ROM与PolyBLEP
# --------------------------------------------------------------------------------------------
# 仅在ENABLE_SUBTRACTIVE=1且ENABLE_ADDITIVE=0时启用06.2/06.3。
# 加法开启后，减法引擎使用外部DDS服务；此时注释内部DDS报告，不能按SHARE_ADDITIVE_DDS猜测实例。
report_timing -setup -from [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_csp/u_dmem_pitch/sdpx9b_inst_0/DO*}] -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_reciprocal/*/AD*}] -max_paths 20 -max_common_paths 1
report_timing -setup -from [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*step_c1*/Q u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*step_c1*/Q u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*step_c1*/Q}] -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*/A*}] -max_paths 20 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_saw/u_wrap_edge/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_rising_edge/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_pwm/u_falling_edge/*/B*}] -max_paths 20 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# 06.3 内部DDS：相位与正弦路径
# --------------------------------------------------------------------------------------------
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/AD* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/DI*}] -max_paths 25 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/AD* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_phase/*/DI*}] -max_paths 10 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/AD* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/DI*}] -max_paths 25 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/D u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/CE* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/A* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/B* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/AD* u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_renderer/g_internal_dds.u_dds/u_sine/u_sine*/*/DI*}] -max_paths 10 -max_common_paths 1
# @end synth.subtractive_internal_dds

# @block synth.ks when=ENABLE_KS
# ENGINE_GATES: ACTIVE [ENABLE_KS=1]
# ============================================================================================
# 07 KS物理建模 [可选，默认启用]
# ============================================================================================
# 层级：u_system/u_synth_manager/g_karplus_strong.u_engine。控制在synth域，算核在USB共享48 MHz域。
# ENABLE_KS=0时注释07整节；保留01.4时钟和04节USB本身需要的约束。

# --------------------------------------------------------------------------------------------
# 07.1 KS命令/状态与control48共钟，不再设置command/status跨域例外。
# 旧sample_tick/PCM桥在request模式停用；音频请求/结果约束见11.2。

# --------------------------------------------------------------------------------------------
# 07.2 toggle首级与原始复位限定器
# --------------------------------------------------------------------------------------------
# 只例外req_meta/ack_meta的D与限定器CLEAR，后级D/CE和业务复位不在本组豁免。
# RETIRED request PCM architecture: set_false_path -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_sample_tick_cdc/req_meta_s0/D u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_sample_tick_cdc/ack_meta_s0/D u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_pcm_cdc/req_meta_s0/D u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_pcm_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_reset.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_*_cdc/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_*_cdc/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_*_cdc/dst_reset_meta_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/u_*_cdc/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/ks_reset_meta_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/ks_reset_sync_s0/CLEAR}]

# --------------------------------------------------------------------------------------------
# 07.3 MIDI控制、owner与算核专项报告
# KS音色扩展：g_*midi同时覆盖Legacy与g_timbre_midi owner；仅扩展专项报告集合。
# 新模型配置握手与算核同control48，不新增跨域例外；扩展PCM仍沿用现有20ns载荷预算。
# 网表对象命中与setup/hold须由后续官方实现核对，本次未运行综合/PnR/STA。
# --------------------------------------------------------------------------------------------
# 算核每拍运行，保留单拍分析。专项端口集合并非精确的全核WNS证明，须同时看全局报告。
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_midi.ks_stage/*/D* u_system/u_synth_manager/g_midi.ks_stage/*/CE* u_system/u_synth_manager/g_midi.ks_stage/*/W* u_system/u_synth_manager/g_midi.ks_stage/*/RESET* u_system/u_synth_manager/g_midi.ks_stage/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/A* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/B* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/C* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/D* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/CE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/OCE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/W* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/RESET* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/A* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/B* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/D* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/CE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/W* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/RESET* u_system/u_synth_manager/g_karplus_strong.u_engine/u_control/g_*midi.performance/owner/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/D u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/CE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/A* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/B* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/C* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/OCE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/W* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/RESET* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/SET*}] -max_paths 1000 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/D u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/CE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/A* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/B* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/C* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/OCE* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/W* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/RESET* u_system/u_synth_manager/g_karplus_strong.u_engine/u_core/*/SET*}] -max_paths 20 -max_common_paths 1
report_timing -setup -mod_ins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc} -max_paths 30 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {control_clk}] -max_paths 25 -max_common_paths 1

# 07.4 分离音频域的原始复位资格，不豁免业务复位释放。
set_false_path -to [get_pins {u_system/u_synth_manager/g_karplus_strong.u_engine/g_audio_reset.u_reset/release_pipe_*_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/g_audio_reset.reset_meta_s0/CLEAR u_system/u_synth_manager/g_karplus_strong.u_engine/g_clock_bridge.u_cdc/g_audio_reset.reset_sync_s0/CLEAR}]
# @end synth.ks

# @block shared.fm_budgets when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 08 FM公共设施与可选FM引擎
# ============================================================================================
# 08.1为常驻PLL/音频基础设施使用的跨域预算；08.2/08.3才是ENABLE_FM控制的专用引擎。
# 本范本默认全部启用。RTL关闭FM时，同步停用08.2/08.3及10.3/10.4，保留08.1和10.1的常驻设施。

# --------------------------------------------------------------------------------------------
# 08.1 FM公共跨域预算与时钟域报告 [常驻]
# --------------------------------------------------------------------------------------------
# synth↔FM及SPI↔FM保留20 ns/hold-only预算；FM后端/FIFO同域路径仍按普通单拍计时。
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {fm_native_clk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {fm_native_clk}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {fm_native_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {fm_native_clk}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_clocks {esp_spi_sclk}]
report_timing -setup -from_clock [get_clocks {fm_native_clk}] -to_clock [get_clocks {fm_native_clk}] -max_paths 25 -max_common_paths 1
report_timing -hold -from_clock [get_clocks {fm_native_clk}] -to_clock [get_clocks {fm_native_clk}] -max_paths 25 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {fm_native_clk}] -max_paths 25 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {fm_native_clk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 25 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# @end shared.fm_budgets

# @block synth.fm when=ENABLE_FM
# ENGINE_GATES: ACTIVE [ENABLE_FM=1]
# 08.2 FM引擎首级与复位限定器 [可选，默认启用]
# --------------------------------------------------------------------------------------------
# 仅在ENABLE_FM=1并确认实际层级后使用本子段；关闭FM时整段注释，不切meta→sync后级。
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/command_req_meta_sys_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/panic_req_meta_sys_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/query_req_meta_sys_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/command_ack_meta_spi_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/panic_ack_meta_spi_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/query_ack_meta_spi_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/g_midi.performance/patch_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/g_midi.performance/patch_ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/g_route_cdc.route_meta_*_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/event_req_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/control_req_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/source_pending_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/event_ack_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/control_ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/req_meta_s0/D u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/dst_reset_meta_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/dst_reset_meta_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/u_control/g_async_readback.u_status_cdc/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/native_reset/release_pipe_*_s0/CLEAR u_system/u_synth_manager/g_fm.u_engine/g_guard_reset.u_reset/release_pipe_*_s0/CLEAR}]

# --------------------------------------------------------------------------------------------
# 08.3 FM控制、MIDI、FIFO及后端报告 [可选，默认启用]
# --------------------------------------------------------------------------------------------
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_midi.fm_stage/*/D* u_system/u_synth_manager/g_midi.fm_stage/*/CE* u_system/u_synth_manager/g_midi.fm_stage/*/W* u_system/u_synth_manager/g_midi.fm_stage/*/RESET* u_system/u_synth_manager/g_midi.fm_stage/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/*/A* u_system/u_synth_manager/g_fm.u_engine/u_control/*/B* u_system/u_synth_manager/g_fm.u_engine/u_control/*/C* u_system/u_synth_manager/g_fm.u_engine/u_control/*/D* u_system/u_synth_manager/g_fm.u_engine/u_control/*/CE* u_system/u_synth_manager/g_fm.u_engine/u_control/*/OCE* u_system/u_synth_manager/g_fm.u_engine/u_control/*/W* u_system/u_synth_manager/g_fm.u_engine/u_control/*/RESET* u_system/u_synth_manager/g_fm.u_engine/u_control/*/SET* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/A* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/B* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/C* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/D* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/CE* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/OCE* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/W* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/RESET* u_system/u_synth_manager/g_fm.u_engine/u_backend/*/SET*}] -max_paths 25 -max_common_paths 1
# RETIRED REQUEST1: old FIFO/SRC absent in current mapped netlist. report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/A* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/B* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/D* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/CE* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/W* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/RESET* u_system/u_synth_manager/g_fm.u_engine/u_resampler/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/A* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/B* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/D* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/CE* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/W* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/RESET* u_system/u_synth_manager/g_fm.u_engine/u_control/g_*midi.performance/owner/*/SET*}] -max_paths 25 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_control/status_generation_sys_*/D* u_system/u_synth_manager/g_fm.u_engine/u_control/status_generation_sys_*/CE*}] -max_paths 40 -max_common_paths 1
# RETIRED REQUEST1: old FIFO/SRC absent in current mapped netlist. report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/pcm_fifo/*wq1_rptr*/D u_system/u_synth_manager/g_fm.u_engine/u_backend/pcm_fifo/*rq1_wptr*/D}] -max_paths 18 -max_common_paths 1
# RETIRED REQUEST1: old FIFO/SRC absent in current mapped netlist. report_timing -setup -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/pcm_fifo/*wq2_rptr*/D u_system/u_synth_manager/g_fm.u_engine/u_backend/pcm_fifo/*rq2_wptr*/D}] -max_paths 18 -max_common_paths 1
report_timing -recovery -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/*/CLEAR u_system/u_synth_manager/g_fm.u_engine/u_backend/*/PRESET}] -max_paths 20 -max_common_paths 1
report_timing -removal -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/*/CLEAR u_system/u_synth_manager/g_fm.u_engine/u_backend/*/PRESET}] -max_paths 20 -max_common_paths 1
report_timing -setup -from [get_pins {u_system/u_synth_manager/g_fm.u_engine/u_backend/native_reset/release_pipe_*_s0/Q}] -max_paths 10 -max_common_paths 1

# 新FM联合链路复位仅豁免首级D。
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/link_reset_sync_0_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_fm.u_engine/g_event_cdc.u_events/dst_pending_meta_s0/D}]
# @end synth.fm

# @block synth.additive when=ENABLE_ADDITIVE
# ENGINE_GATES: INACTIVE [ENABLE_ADDITIVE=0]
# ============================================================================================
# 09 加法合成器与共享DDS [可选，占位]
# ============================================================================================
# 原始范本中的加法专项报告已是NOADD占位，没有可恢复的活动命令，本节不编造网表对象。
# 加法逻辑受01节synth_clk与13节全局STA约束；启用加法不需要删除公共时钟。
# ENABLE_ADDITIVE=1时，减法引擎的内部DDS被外部服务替代，需要注释06.2/06.3。
# 后续新增renderer/voices/spectrum/mailbox专项报告，集中放在本节，并先核对实际综合网表。
# @end synth.additive

# @block audio.shared_reset when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 10 公共音频输出与FM tap
# ============================================================================================
# 层级：u_system/u_audio_output。常驻复位设施与FM数据握手分开，避免模块关闭后引用不存在的寄存器。

# --------------------------------------------------------------------------------------------
# 10.1 FM host与音频复位设施 [常驻]
# --------------------------------------------------------------------------------------------
# 即使ENABLE_FM=0，仍保留的PLL/复位限定器继续使用原始CLEAR例外。
# 原混合集合中的fm_tap_reset_sync_r/fm_gate_sys_r移到10.3，与常驻复位限定器分开维护。
set_false_path -to [get_pins {u_system/u_clock_reset/u_fm_host_reset/release_pipe_*_s0/CLEAR}]
# RETIRED request PCM architecture: set_false_path -to [get_pins {u_system/u_audio_output/fm_reset_sync_r_*_s0/CLEAR u_system/u_audio_output/fm_ready_sys_r_*_s0/CLEAR u_system/u_audio_output/u_fm_tap_cdc/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_tap_cdc/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_fm_tap_cdc/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_tap_cdc/dst_reset_sync_s0/CLEAR}]
report_timing -setup -from [get_pins {u_system/u_clock_reset/u_fm_host_reset/release_pipe_*_s0/Q}] -max_paths 10 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# 10.2 PT8211/PCM5102A外部输出预算占位
# --------------------------------------------------------------------------------------------
# HP_*与PCM_*是两路接收器接口。后续按器件建立/保持时间与板线差补BCK参考和数据/WS的max/min预算。
# 当前不加入未经确认的I/O delay；重新布线后需要重新检查两路输出时序。

# --------------------------------------------------------------------------------------------
# @end audio.shared_reset

# @block audio.fm_data when=ENABLE_FM
# ENGINE_GATES: ACTIVE [ENABLE_FM=1]
# 10.3 FM音频数据与可消失的复位副本 [可选，默认启用]
# --------------------------------------------------------------------------------------------
# FM路径存在且对象匹配时使用本子段；关闭FM后，这些对象可能被常量优化删除，由本块开关停用。
# 关闭数据tap不等于删除10.1仍存在的常驻复位设施。
# RETIRED request PCM architecture: set_false_path -to [get_pins {u_system/u_audio_output/fm_enable_sync_r_0_s0/D u_system/u_audio_output/fm_gate_sys_r_0_s0/D u_system/u_audio_output/fm_ready_sys_r_0_s0/D u_system/u_audio_output/u_fm_tap_cdc/req_meta_s0/D u_system/u_audio_output/u_fm_tap_cdc/ack_meta_s0/D}]
# RETIRED request PCM architecture: set_false_path -to [get_pins {u_system/u_audio_output/fm_tap_reset_sync_r_*_s0/CLEAR u_system/u_audio_output/fm_gate_sys_r_*_s0/CLEAR}]

# --------------------------------------------------------------------------------------------
# 10.4 FM tap后级专项诊断 [可选，默认启用]
# --------------------------------------------------------------------------------------------
# 原始范本把FM tap与混响对象混在同一报告中；这里按数据/复位角色拆开。
# FM=0时req_sync/ack_sync可能被优化删除，由本块开关停用数据后级查询，避免原TA2003。
# 复位后级即使保留，也可能已常量化；若专项查询没有有效路径，核对后单独注释，保留全局分析。
# RETIRED request PCM architecture: report_timing -setup -to [get_pins {u_system/u_audio_output/u_fm_tap_cdc/req_sync_s0/D u_system/u_audio_output/u_fm_tap_cdc/ack_sync_s0/D}] -max_paths 30 -max_common_paths 1
# RETIRED request PCM architecture: report_timing -setup -to [get_pins {u_system/u_audio_output/u_fm_tap_cdc/src_reset_sync_s0/D u_system/u_audio_output/u_fm_tap_cdc/dst_reset_sync_s0/D}] -max_paths 30 -max_common_paths 1
# @end audio.fm_data

# @block effect.reverb_control when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 11 混响控制接口与混响DSP
# ============================================================================================
# 11.1 RVR1与11.2统一PCM调度常驻，ENABLE_REVERB只控制worker效果。
# 当前顶层开关保持，REVERB=0时调度与串行输出继续运行。

# --------------------------------------------------------------------------------------------
# 11.1 RVR1 SPI控制接口 [常驻]
# --------------------------------------------------------------------------------------------
# 层级：u_system/u_reverb_control。沿用03.1的SPI↔subtractive载荷/读回20ns预算；只例外首级D和原始CLEAR。
set_false_path -to [get_pins {u_system/u_reverb_control/req_meta_s0/D u_system/u_reverb_control/ack_meta_s0/D u_system/u_reverb_control/ready_meta_s0/D u_system/u_reverb_control/fault_meta_s0/D u_system/u_reverb_control/read_req_meta_s0/D u_system/u_reverb_control/cs_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_reverb_control/cfg_reset_meta_s0/CLEAR u_system/u_reverb_control/cfg_reset_sync_s0/CLEAR u_system/u_reverb_control/synth_reset_meta_s0/CLEAR u_system/u_reverb_control/synth_reset_sync_s0/CLEAR}]
report_timing -setup -mod_ins {u_system/u_reverb_control} -max_paths 1000 -max_common_paths 1
report_timing -hold -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -recovery -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -removal -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# RVR1 synth_clk_i最终仍接subtractive_clk；ACK/读回时钟范围保持。
# @end effect.reverb_control

# @block effect.common_pcm when=ALWAYS
# 统一PCM调度和串行器常驻；REVERB=0仅旁路worker，仍保留这些CDC。
# 输入载荷沿用20ns上限和hold-only例外，req/ack只切首级D。
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/u_audio_output/u_controls/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/u_audio_output/u_controls/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_controls/req_meta_s0/D u_system/u_audio_output/u_controls/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_controls/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_controls/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_controls/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_controls/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_synth_manager/g_output_status.u_status/u_snapshot/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_synth_manager/g_output_status.u_status/u_snapshot/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_output_status.u_status/u_snapshot/req_meta_s0/D u_system/u_synth_manager/g_output_status.u_status/u_snapshot/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_output_status.u_status/u_snapshot/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_output_status.u_status/u_snapshot/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_output_status.u_status/u_snapshot/dst_reset_meta_s0/CLEAR u_system/u_synth_manager/g_output_status.u_status/u_snapshot/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_synth_manager/g_output_status.u_status/u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_audio_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/alive_meta_*_s0/D}]
report_timing -setup -mod_ins {u_system/u_audio_output} -max_paths 1000 -max_common_paths 1
report_timing -hold -mod_ins {u_system/u_audio_output} -max_paths 100 -max_common_paths 1
# 首级后的req_sync/ack_sync和业务路径保持STA；综合网表对象匹配待官方构建验证。
# @end effect.common_pcm

# @block audio.request_sub when=ENABLE_SUBTRACTIVE or ENABLE_ADDITIVE or ENABLE_FLUTE
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_request/req_meta_s0/D u_system/u_audio_output/u_sub_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_response/req_meta_s0/D u_system/u_audio_output/u_sub_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_sub_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_sub

# @block audio.request_ks when=ENABLE_KS
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_request/req_meta_s0/D u_system/u_audio_output/u_ks_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {control_clk}] -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_response/req_meta_s0/D u_system/u_audio_output/u_ks_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_ks_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_ks

# @block audio.request_fm when=ENABLE_FM
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_request/req_meta_s0/D u_system/u_audio_output/u_fm_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_response/req_meta_s0/D u_system/u_audio_output/u_fm_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_audio_output/u_fm_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_fm

# @block audio.pcm_status_snapshots when=ALWAYS
# ENGINE_GATES: ACTIVE [ENABLE_REVERB=1]
# 11.4 PCM1快照held数据与首级/资格；统一PCM管线常驻。
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_pcm_status/u_*_snapshot_cdc/dst_data_o_*_s0/D u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/u_pcm_status/u_*_snapshot_cdc/dst_data_o_*_s0/D u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_data_o_*_s0/D}]
# 三组计数器快照均由audio24进入control48，使用显式20ns载荷预算。
set_false_path -to [get_pins {u_system/u_pcm_status/u_*_snapshot_cdc/req_meta_s0/D u_system/u_pcm_status/u_*_snapshot_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_pcm_status/u_*_snapshot_cdc/src_reset_meta_s0/CLEAR u_system/u_pcm_status/u_*_snapshot_cdc/src_reset_sync_s0/CLEAR u_system/u_pcm_status/u_*_snapshot_cdc/dst_reset_meta_s0/CLEAR u_system/u_pcm_status/u_*_snapshot_cdc/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/req_meta_s0/D u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/src_reset_meta_s0/CLEAR u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/src_reset_sync_s0/CLEAR u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_reset_meta_s0/CLEAR u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_reset_sync_s0/CLEAR}]
report_timing -setup -mod_ins {u_system/u_pcm_status} -max_paths 100 -max_common_paths 1
# @end audio.pcm_status_snapshots

# @block display when=ENABLE_HDMI
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_display_pcm.u_snapshot/dst_data_o_*_s0/D}] 20.000
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_display_pcm.u_snapshot/dst_data_o_*_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_display_pcm.u_snapshot/req_meta_s0/D u_system/g_display_pcm.u_snapshot/ack_meta_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_display_pcm.u_snapshot/src_reset_meta_s0/CLEAR u_system/g_display_pcm.u_snapshot/src_reset_sync_s0/CLEAR u_system/g_display_pcm.u_snapshot/dst_reset_meta_s0/CLEAR u_system/g_display_pcm.u_snapshot/dst_reset_sync_s0/CLEAR}]
# ENGINE_GATES: INACTIVE [ENABLE_HDMI=0; manual constraints synchronized]
# ============================================================================================
# 12 示波显示与HDMI [ENABLE_HDMI]
# ============================================================================================
# 层级：u_system/u_display。视频像素/串行PLL关系定义在01.3；关闭音源不等于关闭显示。
# 只有在RTL/工程中确实移除整个显示逻辑后，才注释本节并重新核对01.3/01.5相关时钟和分组。

# --------------------------------------------------------------------------------------------
# 12.1 快照元数据跨域
# --------------------------------------------------------------------------------------------
# 元数据保持至ACK，三级请求同步及垂直消隐接纳配合30 ns预算；只例外请求/ACK首级D。
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/active_meta_o_*_s0/D}] 30.000
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/active_meta_o_*_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/request_sync_0_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/u_core/ack_sync_0_s0/D}]

# --------------------------------------------------------------------------------------------
# 12.2 HDMI串行器复位释放
# --------------------------------------------------------------------------------------------
# visual_reset_n含异步LOCK和参考域资格信号；只豁免异步入口到
# core/pixel/pixel_ready复位同步链的CLEAR。链内D及释放到业务逻辑继续计时。
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/core_reset_sync_*_s0/CLEAR u_system/g_hdmi.u_display/pixel_reset_sync_*_s0/CLEAR u_system/g_hdmi.u_display/u_hdmi/pixel_ready_q_*_s0/CLEAR}]
# 保留三条lane的recovery/removal，后级reset_sync的D路径与最小脉宽检查。
# 已有历史recovery负裕量不能通过注释本子段或添加false_path来认定已解决。
#report_timing -recovery -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -removal -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -recovery -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -removal -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -recovery -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -removal -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/u_serializer/RESET}] -max_paths 4
#report_timing -setup -from [get_pins {u_system/g_hdmi.u_display/u_hdmi/pixel_ready_q_0_s0/Q}] -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/pixel_ready_q_1_s0/D}] -max_paths 4
# report_min_pulse_width -nworst 20 -detail [get_regs {u_system/g_hdmi.u_display/u_hdmi/lanes[*].u_phy/reset_sync_*}]

# --------------------------------------------------------------------------------------------
# 12.3 串行器数据与lane同步后级
# --------------------------------------------------------------------------------------------
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/reset_sync_*/D}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[0].u_phy/reset_sync_*/D}] -max_paths 20
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/reset_sync_*/D}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[1].u_phy/reset_sync_*/D}] -max_paths 20
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -setup -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/reset_sync_*/D}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/u_serializer/D*}] -max_paths 20
#report_timing -hold -to [get_pins {u_system/g_hdmi.u_display/u_hdmi/lanes[2].u_phy/reset_sync_*/D}] -max_paths 20

# --------------------------------------------------------------------------------------------
# 12.4 显示时钟域与跨域报告
# --------------------------------------------------------------------------------------------
#report_timing -setup -from_clock [get_clocks {video_pixel_clk}] -to_clock [get_clocks {video_pixel_clk}] -max_paths 10
#report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {video_pixel_clk}] -max_paths 10
#report_timing -setup -from_clock [get_clocks {video_serial_clk}] -to_clock [get_clocks {video_serial_clk}] -max_paths 10
#report_timing -hold -from_clock [get_clocks {video_serial_clk}] -to_clock [get_clocks {video_serial_clk}] -max_paths 10
# KEY1 is a held mode level: only the first pixel synchronizer input is async.
# @end display

# @block display.fft when=ENABLE_HDMI and ENABLE_FFT
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/u_mode_mux/mode_sync_0_s0/D}]
# FFT payload is held until ACK. target_group is the FIRST pixel-domain data
# capture (two-stage 16-way selection); later target/update_target is not CDC.
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_max_delay -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/target_group*_s*/D}] 30.000
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -hold -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/target_group*_s*/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/request_sync_0_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/ack_sync_0_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: report_timing -setup -from_clock [get_clocks {board_ref_clk}] -to_clock [get_clocks {video_pixel_clk}] -max_paths 10
# 73.7 MHz audio tap -> 50 MHz FFT: held payload and two-stage req/ack.
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/dst_data_o_*_s0/D}] 20.000
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/dst_data_o_*_s0/D}]
# DISABLED [ENABLE_HDMI=0; restore with HDMI constraints]: set_false_path -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/req_meta_s0/D u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/ack_meta_s0/D}]
# @end display.fft

# @block shared.reports when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 13 全局时序报告 [常驻]
# ============================================================================================
# 各模块专项报告不能替代全局STA。保留setup、hold、recovery、removal、最小脉宽及公共域报告。
# Nothing to report/空集合不等于通过；某个模块没有专用报告也不等于它未受公共时钟约束。

# --------------------------------------------------------------------------------------------
# 13.1 全局setup/hold与复位检查
# --------------------------------------------------------------------------------------------
report_timing -setup -max_paths 25 -max_common_paths 1
report_timing -hold -max_paths 25 -max_common_paths 1
report_timing -recovery -max_paths 25 -max_common_paths 1
report_timing -removal -max_paths 25 -max_common_paths 1
report_min_pulse_width -nworst 20 -detail
report_timing -setup -max_paths 10000 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# 13.2 板级参考与公共synth域
# --------------------------------------------------------------------------------------------
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 10
report_timing -setup -from_clock [get_clocks {board_ref_clk}] -to_clock [get_clocks {board_ref_clk}] -max_paths 10
# @end shared.reports

# @block shared.control_audio_cdc when=ALWAYS
# ENGINE_GATES: ACTIVE [ALWAYS]
# ============================================================================================
# 14 新模块维护占位与待补项目
# ============================================================================================
# 复制本文件新建SDC时，在对应模块附近增加14.x同样的层级即可，先声明时钟，再添加经证明的例外，最后添加报告。

# --------------------------------------------------------------------------------------------
# 14.1 新模块：时钟/端口/实际层级
# --------------------------------------------------------------------------------------------
# 填写实际时钟来源、频率、复位来源与模块实例路径；共享已有时钟时不要另造重复clock。

# --------------------------------------------------------------------------------------------
# 14.2 新模块：跨域结构与物理预算
# --------------------------------------------------------------------------------------------
# 先确认双触发器、稳定载荷握手或FIFO结构，再为具体接收集合设置预算或首级例外。
# 不要对整个模块添加false_path，也不要用多周期掩盖每拍运行的逻辑。

# --------------------------------------------------------------------------------------------
# 14.3 新模块：诊断与覆盖
# --------------------------------------------------------------------------------------------
# setup/hold与recovery/removal分别核对，记录具体对象是否命中；必要时补最小脉宽和外部I/O预算。

# --------------------------------------------------------------------------------------------
# 14.4 既有问题边界
# --------------------------------------------------------------------------------------------
# SPI业务复位释放、板级SPI/DAC的I/O预算及HDMI既有recovery违例仍需后续专门处理。
# 本候选改变时钟拓扑和对象集合；旧实现报告不再覆盖本候选。

# 14.5 control48↔subtractive73/FM12与PCM状态载荷预算 [常驻]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_clocks {control_clk}]
# 14.6 新公共控制CDC：只列首级D与原始复位资格。
set_false_path -to [get_pins {u_system/u_synth_manager/g_control_domain.*/event_ack_meta_s0/D u_system/u_synth_manager/g_control_domain.*/control_ack_meta_s0/D u_system/u_synth_manager/g_control_domain.*/event_req_meta_s0/D u_system/u_synth_manager/g_control_domain.*/control_req_meta_s0/D u_system/u_synth_manager/g_control_domain.*/source_pending_meta_s0/D u_system/u_synth_manager/g_control_domain.*/dst_pending_meta_s0/D u_system/u_synth_manager/g_control_domain.*/req_meta_s0/D u_system/u_synth_manager/g_control_domain.*/ack_meta_s0/D u_system/u_synth_manager/audio_alive_sync_0_s0/D}]
set_false_path -to [get_pins {u_system/u_clock_reset/u_control_host_reset/release_pipe_*_s0/CLEAR u_system/u_clock_reset/u_subtractive_host_reset/release_pipe_*_s0/CLEAR u_system/u_synth_manager/g_control_domain.*/release_pipe_*_s0/CLEAR u_system/u_synth_manager/g_control_domain.*/src_reset_meta_s0/CLEAR u_system/u_synth_manager/g_control_domain.*/src_reset_sync_s0/CLEAR u_system/u_synth_manager/g_control_domain.*/dst_reset_meta_s0/CLEAR u_system/u_synth_manager/g_control_domain.*/dst_reset_sync_s0/CLEAR}]
# 公共音频host资格常驻；PCM1 route/reset/read窗口不依赖128位stats。
set_false_path -to [get_pins {u_system/u_clock_reset/u_audio_host_reset/release_pipe_*_s0/CLEAR}]
# 单项req节点可能合并/优化；与常驻route/reset首级用同一非空并集查询。
set_false_path -to [get_pins {u_system/u_pcm_status/req_meta_s0/D u_system/u_pcm_status/audio_reset_meta_s0/D u_system/u_pcm_status/g_subtractive_status.route_meta_s0/D}]
# 14.7 两个已知空节点查询继续保持注释，未偷偷恢复为活动命令。
#set_false_path -to [get_pins {u_system/u_pcm_status/req_meta_s0/D u_system/u_synth_manager/g_control_diagnostics.request_meta_s0/D}]
# set_false_path -to [get_pins {u_system/u_synth_manager/g_config_reset.u_reset/release_pipe_*_s0/CLEAR}]
# 若最终综合保留诊断请求首级，须核对真实非空并集后再补，不能凭旧报告认定。
# 14.8 新域与跨域诊断；不代替实际STA或例外有效性检查。
report_timing -setup -from_clock [get_clocks {control_clk}] -to_clock [get_clocks {control_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {pcm_audio_clk}] -to_clock [get_clocks {pcm_audio_clk}] -max_paths 50 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {control_clk}] -to_clock [get_clocks {subtractive_clk}] -max_paths 50 -max_common_paths 1
report_timing -setup -from_clock [get_clocks {subtractive_clk}] -to_clock [get_clocks {control_clk}] -max_paths 50 -max_common_paths 1
# @end shared.control_audio_cdc
