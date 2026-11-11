# Actual clocks from generated PLL configurations. No global STA disable.
create_clock -name board_ref_clk -period 20.000 [get_ports {sys_clk}]
create_generated_clock -name cpu_core_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 16 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT1}]
create_generated_clock -name dtcm_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name ahb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT2}]
create_generated_clock -name apb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT3}]
create_generated_clock -name rtc_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -divide_by 5 [get_pins {u_core_pll/u_pll/PLL_inst/CLKOUT4}]
# Headless control build: no video PLL or serializer. Both ends of the
# menu publication handshake use APB PCLK and retain ordinary setup/hold.
# The obsolete engine-switch ports are removed; no key CDC exception remains.
# 50MHz DTCM/AHB/APB retain one common PLL and in-phase 1:1 AHB/APB.
# All remaining recovery/removal checks remain enabled for review.
# UART debug monitor: only first synchronizer D endpoints are excepted.
# ENABLE_CH569_UART_DIAG=0 removes this observation-only mailbox.
# Selected before input freezing by scripts/sync_control_uart_sdc.py.
# Keep only plain timing commands in SDC; it is not general-purpose Tcl.
# BEGIN_CH569_UART_TIMING_LEGACY
# CH569_UART_OFF set_false_path -to [get_pins {u_debug_monitor/request_meta_s0/D u_debug_monitor/ack_meta_s0/D u_debug_monitor/flags_meta*_s0/D}]
# Snapshot is held through request/ack; constrain its held mailbox to one 50MHz cycle.
# CH569_UART_OFF set_max_delay -from [get_pins {u_debug_monitor/payload_hold*_s0/Q}] -to [get_pins {u_debug_monitor/payload_sys*_s0/D}] 20.000
# CH569_UART_OFF set_false_path -hold -from [get_pins {u_debug_monitor/payload_hold*_s0/Q}] -to [get_pins {u_debug_monitor/payload_sys*_s0/D}]
# END_CH569_UART_TIMING_LEGACY

# Original free-running audio/control PLLs; no new sample-rate clocks.
create_generated_clock -name control_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 24 -divide_by 25 [get_pins {u_system/u_control_input/u_usb_midi/usb_pll/u_pll/PLL_inst/CLKOUT0}]
create_clock -name esp_spi_sclk -period 100.000 -waveform {0.000 50.000} [get_ports {ESP_SPI_SCLK}]
create_generated_clock -name subtractive_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 158 [get_pins {u_system/u_clock_reset/u_subtractive_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name pcm_audio_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 474 [get_pins {u_system/u_clock_reset/u_audio_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name fm_native_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 2 -divide_by 5 [get_pins {u_system/u_clock_reset/u_fm_pll/u_pll/PLL_inst/CLKOUT0}]

# Only the asynchronous physical USB receive first-stage D endpoints.
set_false_path -from [get_ports {USB_MIDI_DP USB_MIDI_DM}] -to [get_pins {u_system/u_control_input/u_usb_midi/phy/*_ms_s0/D}]
# Preserve the existing SPI bundled-data budget, never disable general setup.
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}]
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}]
set_false_path -to [get_pins {u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/req_meta_s0/D u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/ack_meta_s0/D}]
# AE350 command/route: request and ACK only; response and payload stay timed.
set_false_path -to [get_pins {u_command_cdc/req_meta_s0/D u_command_cdc/ack_meta_s0/D u_route_cdc/req_meta_s0/D u_route_cdc/ack_meta_s0/D}]
set_max_delay -from [get_clocks {apb_clk}] -to [get_pins {u_command_cdc/dst_data_o_*_s0/D u_route_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_pins {u_command_cdc/dst_data_o_*_s0/D u_route_cdc/dst_data_o_*_s0/D}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {apb_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {apb_clk}]
set_max_delay -from [get_clocks {apb_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_clocks {control_clk}]
set_false_path -to [get_pins {u_feedback_cdc/req_meta_s0/D u_feedback_cdc/ack_meta_s0/D u_backend_status_cdc/req_meta_s0/D u_backend_status_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_ingress_cdc/byte_req_meta_s0/D u_ingress_cdc/byte_ack_meta_s0/D u_ingress_cdc/flush_req_meta_s0/D u_ingress_cdc/flush_ack_meta_s0/D u_ingress_cdc/done_req_meta_s0/D u_ingress_cdc/done_ack_meta_s0/D}]
# Native command/idle lanes and their readiness first stages. Generated
# blocks use dot-qualified instance names; preserve meta->sync/data timing.
set_false_path -to [get_pins {u_system/u_synth_manager/g_ae350_backend.*/req_meta_s0/D u_system/u_synth_manager/g_ae350_backend.*/ack_meta_s0/D u_system/u_synth_manager/g_ae350_backend.ready_meta*_s0/D}]
# Existing control->subtractive coherent state still carries output routing.
# ABI2 removes MIDI event CDC; the surviving audio-state handshake is nested
# under u_control_state/u_payload_cdc. Its return-status payload is optimized out.
set_false_path -to [get_pins {u_system/u_synth_manager/g_control_domain.u_audio_control/u_control_state/u_payload_cdc/req_meta_s0/D u_system/u_synth_manager/g_control_domain.u_audio_control/u_control_state/u_payload_cdc/ack_meta_s0/D}]
# Existing control/audio bundled transfers retain their 20ns setup budget.
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}]

# Parallel audio leaves response_error diagnostics unconsumed; their flops
# are absent from the actual VG. Keep the physical payload/synchronizers below.
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
# AE350 SPI whitelist removes the reverb write handshake; CS/read request remain.
set_false_path -to [get_pins {u_system/u_reverb_control/read_req_meta_s0/D u_system/u_reverb_control/cs_meta_s0/D}]
set_false_path -to [get_pins {u_system/u_reverb_control/cfg_reset_meta_s0/CLEAR u_system/u_reverb_control/cfg_reset_sync_s0/CLEAR u_system/u_reverb_control/synth_reset_meta_s0/CLEAR u_system/u_reverb_control/synth_reset_sync_s0/CLEAR}]
report_timing -setup -mod_ins {u_system/u_reverb_control} -max_paths 1000 -max_common_paths 1
report_timing -hold -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -recovery -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -removal -mod_ins {u_system/u_reverb_control} -max_paths 100 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# RVR1 synth_clk_i最终仍接subtractive_clk；ACK/读回时钟范围保持。
# @end effect.reverb_control

# ABI2 template FIFO: Gray pointers have a bounded physical datapath.
# Keep first-stage setup bounded (not a blanket false path); only hold is
# asynchronous. The fastest pointer source is the 50MHz APB POP clock.
# Gowin uses _s1 for CE flops here; only the suffix may vary. Gray bit6 equals
# binary bit6 and shares that physical flop. Include it without selecting any
# meta/sync source Q or second-stage destination D.
set_max_delay -from [get_pins {u_template_ingress/wgray_?_s*/Q u_template_ingress/wbin_6_s*/Q}] -to [get_pins {u_template_ingress/wgray_meta_?_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_template_ingress/wgray_?_s*/Q u_template_ingress/wbin_6_s*/Q}] -to [get_pins {u_template_ingress/wgray_meta_?_s*/D}]
set_max_delay -from [get_pins {u_template_ingress/rgray_?_s*/Q u_template_ingress/rbin_6_s*/Q}] -to [get_pins {u_template_ingress/rgray_meta_?_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_template_ingress/rgray_?_s*/Q u_template_ingress/rbin_6_s*/Q}] -to [get_pins {u_template_ingress/rgray_meta_?_s*/D}]
set_false_path -to [get_pins {u_template_ingress/overflow_meta_s0/D u_template_status_cdc/req_meta_s0/D u_template_status_cdc/ack_meta_s0/D}]
# CPU ACK words are held through the return handshake, including stopped SCLK.
set_max_delay -from [get_clocks {apb_clk}] -to [get_pins {u_template_status_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_pins {u_template_status_cdc/dst_data_o_*_s0/D}]

# Per-domain evidence prevents a single long path family hiding APB/control.
report_timing -setup -from [get_clocks {ahb_clk}] -to [get_clocks {ahb_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {apb_clk}] -to [get_clocks {apb_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {apb_clk}] -to [get_clocks {ahb_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {control_clk}] -to [get_clocks {ahb_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {control_clk}] -to [get_clocks {control_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {subtractive_clk}] -to [get_clocks {subtractive_clk}] -max_paths 100 -max_common_paths 1

# All ordinary data and all unexcepted reset checks remain visible.
# Board-specific SPI/DAC input/output delays have not been characterized.
report_timing -setup -max_paths 25 -max_common_paths 1
report_timing -hold -max_paths 25 -max_common_paths 1
# Expose reset paths hidden behind the previous top25 report limit.
# This changes reporting depth only; no timing exception or budget changes.
report_timing -recovery -max_paths 200 -max_common_paths 1
report_timing -removal -max_paths 200 -max_common_paths 1
report_min_pulse_width -nworst 20 -detail

# The prepared register view consumes a held SPI address after req synchronization.
set_false_path -to [get_pins {u_system/u_synth_manager/g_prepared_register_view.read_meta_s0/D}]

# FDC1 held bundles: 50MHz SYS <-> 50MHz APB. Payload remains timed.
# Request/ACK are asynchronous first-stage endpoints only; meta->sync,
# destination CE, reset recovery/removal, and all ordinary paths stay timed.
# u_command: logical width 1; physical constant/pruned bits require netlist proof.
set_max_delay -from [get_pins {u_fdc/u_mailbox/g_enabled.u_command/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_command/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {u_fdc/u_mailbox/g_enabled.u_command/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_command/dst_data_o*_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_command/req_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_command/req_meta_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_command/ack_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_command/ack_meta_s?/D}]
# u_snapshot: logical width 416; physical constant/pruned bits require netlist proof.
set_max_delay -from [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/dst_data_o*_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/req_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/req_meta_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/ack_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_snapshot/ack_meta_s?/D}]
# u_control: logical width 96; physical constant/pruned bits require netlist proof.
set_max_delay -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control/dst_data_o*_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control/req_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control/req_meta_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control/ack_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control/ack_meta_s?/D}]
# u_control_response: logical width 65; physical constant/pruned bits require netlist proof.
set_max_delay -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/dst_data_o*_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/req_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/req_meta_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/ack_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_control_response/ack_meta_s?/D}]
# u_status: logical width 67; physical constant/pruned bits require netlist proof.
set_max_delay -from [get_pins {u_fdc/u_mailbox/g_enabled.u_status/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_status/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {u_fdc/u_mailbox/g_enabled.u_status/held_data*_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_status/dst_data_o*_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_status/req_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_status/req_meta_s?/D}]
set_false_path -from [get_pins {u_fdc/u_mailbox/g_enabled.u_status/ack_s?/Q}] -to [get_pins {u_fdc/u_mailbox/g_enabled.u_status/ack_meta_s?/D}]
# External open-drain I2C inputs: only first synchronizer D endpoints.
set_false_path -from [get_ports {FDC_SCL}] -to [get_pins {u_fdc/u_acquisition/g_enabled.register_master/scl_sync_0_s?/D}]
set_false_path -from [get_ports {FDC_SDA}] -to [get_pins {u_fdc/u_acquisition/g_enabled.register_master/sda_sync_0_s?/D}]
# INT is not used by the polling collector; no empty INT synchronizer exception.

# CPU status to SPI071F; separate from all native/template ACK words.
set_max_delay -from [get_pins {g_fdc_status.u_publish/held_data*_s?/Q}] -to [get_pins {g_fdc_status.u_publish/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {g_fdc_status.u_publish/held_data*_s?/Q}] -to [get_pins {g_fdc_status.u_publish/dst_data_o*_s?/D}]
set_false_path -from [get_pins {g_fdc_status.u_publish/req_s?/Q}] -to [get_pins {g_fdc_status.u_publish/req_meta_s?/D}]
set_false_path -from [get_pins {g_fdc_status.u_publish/ack_s?/Q}] -to [get_pins {g_fdc_status.u_publish/ack_meta_s?/D}]

# SUB_PATCH1 CAP: software latch PCLK -> first SPI synchronizer D only.
# The second synchronizer D and all SPI readback logic retain ordinary timing.
set_false_path -to [get_pins {sub_patch1_cap_meta_spi_s0/D}]

# ITCM address-MUX placement evidence: one path through each real F pin.
# Reports only; preserve all existing clocks, budgets and exceptions.
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_2_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_3_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_4_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_5_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_6_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_7_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_8_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_9_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_10_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_11_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_12_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_13_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_14_s12/F}] -max_paths 1 -max_common_paths 1
report_timing -setup -through [get_pins {u_cpu/u_RiscV_AE350_SOC/u_gw_itcm_top/u_gw_itcm/u_ahb_to_ilm/ILMADDR_15_s12/F}] -max_paths 1 -max_common_paths 1

# Pass-count relocation coverage: retain increment/drain and capture timing.
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_csp/control_pass_count_*_s0/D}] -max_paths 16 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_csp/control_pass_count_*_s0/CE}] -max_paths 16 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_csp/control_pass_count_*_s0/D}] -max_paths 16 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/u_csp/control_pass_count_*_s0/CE}] -max_paths 16 -max_common_paths 1

# APB wdata30 relocation: inspect CPU input, enable/reset, and hold together.
report_timing -setup -to [get_pins {u_apb_pipeline/wdata_30_s0/D}] -max_paths 4 -max_common_paths 1
report_timing -setup -to [get_pins {u_apb_pipeline/wdata_30_s0/CE}] -max_paths 4 -max_common_paths 1
report_timing -setup -to [get_pins {u_apb_pipeline/wdata_30_s0/RESET}] -max_paths 4 -max_common_paths 1
report_timing -hold -to [get_pins {u_apb_pipeline/wdata_30_s0/D}] -max_paths 4 -max_common_paths 1
report_timing -hold -to [get_pins {u_apb_pipeline/wdata_30_s0/CE}] -max_paths 4 -max_common_paths 1
report_timing -hold -to [get_pins {u_apb_pipeline/wdata_30_s0/RESET}] -max_paths 4 -max_common_paths 1

# PCM5102A four-wire I2S forwards the existing serializer clock unchanged.
# These clocks describe output SCK; they do not change any PLL or sample rate.
create_generated_clock -name pcm_a_mck -source [get_pins {u_system/u_clock_reset/u_audio_pll/u_pll/PLL_inst/CLKOUT0}] -master_clock pcm_audio_clk -divide_by 1 [get_ports {PCM_MCK}]
create_generated_clock -name pcm_b_mck -source [get_pins {u_system/u_clock_reset/u_audio_pll/u_pll/PLL_inst/CLKOUT0}] -master_clock pcm_audio_clk -divide_by 1 [get_ports {PCM_B_MCK}]

# AOR1 independent output route: immutable SCLK command, audio-frame ACK.
# Budget only this new held payload/readback; ordinary setup/hold stay enabled.
set_max_delay -from [get_pins {u_system/g_output_routes.u_control/held_command*_s0/Q}] -to [get_clocks {pcm_audio_clk}] 20.000
set_false_path -hold -from [get_pins {u_system/g_output_routes.u_control/held_command*_s0/Q}] -to [get_clocks {pcm_audio_clk}]
set_max_delay -from [get_pins {u_system/g_output_routes.u_control/read_data_o*_s0/Q u_system/g_output_routes.u_control/read_selected_o_s0/Q}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_pins {u_system/g_output_routes.u_control/read_data_o*_s0/Q u_system/g_output_routes.u_control/read_selected_o_s0/Q}] -to [get_clocks {esp_spi_sclk}]
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_pins {u_system/g_output_routes.u_control/read_data_o*_s0/D u_system/g_output_routes.u_control/read_selected_o_s0/D}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_pins {u_system/g_output_routes.u_control/read_data_o*_s0/D u_system/g_output_routes.u_control/read_selected_o_s0/D}]
set_false_path -to [get_pins {u_system/g_output_routes.u_control/req_meta_s0/D u_system/g_output_routes.u_control/ack_meta_s0/D u_system/g_output_routes.u_control/ready_meta_s0/D u_system/g_output_routes.u_control/cs_meta_s0/D u_system/g_output_routes.u_control/read_meta_s0/D}]

# FDR1 diagnostics use the same held-bundle/req-ack contract as FDC status.
# Only this new128-bit bundle is constrained; all existing budgets stay intact.
set_max_delay -from [get_pins {g_fdc_telemetry.u_publish/held_data*_s?/Q}] -to [get_pins {g_fdc_telemetry.u_publish/dst_data_o*_s?/D}] 20.000
set_false_path -hold -from [get_pins {g_fdc_telemetry.u_publish/held_data*_s?/Q}] -to [get_pins {g_fdc_telemetry.u_publish/dst_data_o*_s?/D}]
set_false_path -from [get_pins {g_fdc_telemetry.u_publish/req_s?/Q}] -to [get_pins {g_fdc_telemetry.u_publish/req_meta_s?/D}]
set_false_path -from [get_pins {g_fdc_telemetry.u_publish/ack_s?/Q}] -to [get_pins {g_fdc_telemetry.u_publish/ack_meta_s?/D}]

# BEGIN_CH569_PCM_CAPTURE_TIMING
# Unchanged EXM_HSPI forwards inverted50MHz board clock.
# CH569_PCM_CAPTURE_OFF create_generated_clock -name ch569_hspi_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -divide_by 1 -invert [get_ports {HSPI_HTCLK}]
# Legacy reference provides no external HSPI input/output delay numbers.
# Preserve that coverage limit; no invented budget or broad timing disable.

# CH569 sample FIFO: unchanged generated IP uses Gray-pointer two-stage CDC.
# Bound each asynchronous first-stage datapath to the fastest source period
# (50 MHz = 20 ns). Only first-stage hold is asynchronous; meta->sync and
# all sample-buffer/RAM paths retain ordinary timing analysis.
# CH569_PCM_CAPTURE_OFF set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/u_pcm_sample_async_fifo/fifo_inst/Equal.rq1_wptr_*_s0/D}] 20.000
# CH569_PCM_CAPTURE_OFF set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/u_pcm_sample_async_fifo/fifo_inst/Equal.rq1_wptr_*_s0/D}]
# CH569_PCM_CAPTURE_OFF set_max_delay -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/u_pcm_sample_async_fifo/fifo_inst/Equal.wq1_rptr_*_s0/D}] 20.000
# CH569_PCM_CAPTURE_OFF set_false_path -hold -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/u_pcm_sample_async_fifo/fifo_inst/Equal.wq1_rptr_*_s0/D}]
# END_CH569_PCM_CAPTURE_TIMING

# Optional UART PCM diagnostics. Default top parameter0 removes all six FFs.
# The build-time selector follows the literal top parameter; actual-netlist
# coverage must independently require disabled0/0/0 or enabled3/3/1 objects.
# BEGIN_CH569_UART_TIMING_PCM
# CH569_UART_OFF set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/g_diagnostics.diag_audio_meta_r_*_s0/D}] 20.000
# CH569_UART_OFF set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/g_diagnostics.diag_audio_meta_r_*_s0/D}]
# CH569_UART_OFF report_timing -setup -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/g_diagnostics.diag_audio_sync_r_*_s0/D}] -max_paths 10
# CH569_UART_OFF report_timing -hold -to [get_pins {u_system/g_ch569_pcm.u_ch569_monitor/g_diagnostics.diag_audio_sync_r_*_s0/D}] -max_paths 10
# END_CH569_UART_TIMING_PCM

# SUB control optimization: diagnostic reports only; existing budgets/ exceptions stay intact.
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/validated_result_*_s0/D}] -max_paths 8 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/validated_result_*_s0/D}] -max_paths 8 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/generations*_s0/CE}] -max_paths 100 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/generations*_s0/CE}] -max_paths 100 -max_common_paths 1
report_timing -setup -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/off_pass*_s0/CE}] -max_paths 64 -max_common_paths 1
report_timing -hold -to [get_pins {u_system/u_synth_manager/g_subtractive.u_engine/u_pipeline/g_ae350_runtime.u_runtime/off_pass*_s0/CE}] -max_paths 64 -max_common_paths 1

# BEGIN_DISPLAY_10KHZ_KEYS_20261102
# Append to the selected control SDC only when ENABLE_HDMI=1.
# Generated PLL configuration: 50 MHz * 89/60, serial exactly five times.
create_generated_clock -name video_pixel_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 60 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name video_serial_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 12 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT1}]
create_generated_clock -name video_reset_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 89 -divide_by 60 [get_pins {u_system/g_hdmi.u_display/u_video_pll/u_pll/PLL_inst/CLKOUT2}]

# The 48-bit stereo pair is held through ACK, never fed back to the DAC.
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_display_pcm.u_snapshot/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_system/g_display_pcm.u_snapshot/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/g_display_pcm.u_snapshot/req_meta_s0/D u_system/g_display_pcm.u_snapshot/ack_meta_s0/D}]

# Acquisition publishes stable metadata/banks until pixel vblank ACK.
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/active_meta_o_*_s0/D}] 30.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/active_meta_o_*_s0/D}]
set_false_path -to [get_pins {u_system/g_hdmi.u_display/u_core/u_renderer/request_sync_0_s0/D u_system/g_hdmi.u_display/u_core/ack_sync_0_s0/D}]

# Keep ordinary data, synchronizer later stages, OSER reset, recovery/removal
# and minimum pulse width checks visible. No new reset false paths.
report_timing -setup -from [get_clocks {video_pixel_clk}] -to [get_clocks {video_pixel_clk}] -max_paths 100 -max_common_paths 1
report_timing -hold -from [get_clocks {video_pixel_clk}] -to [get_clocks {video_pixel_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {video_serial_clk}] -to [get_clocks {video_serial_clk}] -max_paths 25 -max_common_paths 1
report_timing -hold -from [get_clocks {video_serial_clk}] -to [get_clocks {video_serial_clk}] -max_paths 25 -max_common_paths 1
report_timing -setup -from [get_clocks {pcm_audio_clk}] -to [get_clocks {subtractive_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -from [get_clocks {subtractive_clk}] -to [get_clocks {video_pixel_clk}] -max_paths 100 -max_common_paths 1
report_timing -recovery -mod_ins {u_system/g_hdmi.u_display} -max_paths 100 -max_common_paths 1
report_timing -removal -mod_ins {u_system/g_hdmi.u_display} -max_paths 100 -max_common_paths 1

# Append after hdmi-enabled.sdc.fragment only when ENABLE_HDMI && ENABLE_FFT.
# Only the asynchronous request/mode first-stage D endpoints are excepted.
set_false_path -to [get_pins {u_system/g_hdmi.u_display/u_mode_mux/mode_sync_0_s0/D}]
set_max_delay -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/target_group*_s*/D}] 30.000
set_false_path -hold -from [get_clocks {board_ref_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/target_group*_s*/D}]
set_false_path -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_fft_core/u_display/request_sync_0_s0/D u_system/g_hdmi.u_display/g_fft.u_fft_core/ack_sync_0_s0/D}]

# One real sample per DAC frame, through a held SUB -> 50 MHz mailbox.
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/req_meta_s0/D u_system/g_hdmi.u_display/g_fft.u_pcm_cdc/ack_meta_s0/D}]
report_timing -setup -from [get_clocks {board_ref_clk}] -to [get_clocks {video_pixel_clk}] -max_paths 100 -max_common_paths 1
report_timing -setup -mod_ins {u_system/g_hdmi.u_display/g_fft.u_fft_core} -max_paths 100 -max_common_paths 1
report_timing -hold -mod_ins {u_system/g_hdmi.u_display/g_fft.u_fft_core} -max_paths 100 -max_common_paths 1

# Only the asynchronous display-button first synchronizer D is excepted.
set_false_path -from [get_ports {display_key_n}] -to [get_pins {u_system/u_chord_keys/key_meta_0_s0/D}]
# END_DISPLAY_10KHZ_KEYS_20261102
