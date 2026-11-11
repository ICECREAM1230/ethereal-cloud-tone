# Independent enabled wrapper: preserved production constraints with u_lab instance prefix.
# Actual clocks from generated PLL configurations. No global STA disable.
create_clock -name board_ref_clk -period 20.000 [get_ports {sys_clk}]
create_generated_clock -name cpu_core_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 16 [get_pins {u_lab/u_core_pll/u_pll/PLL_inst/CLKOUT1}]
create_generated_clock -name dtcm_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_lab/u_core_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name ahb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_lab/u_core_pll/u_pll/PLL_inst/CLKOUT2}]
create_generated_clock -name apb_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 1 [get_pins {u_lab/u_core_pll/u_pll/PLL_inst/CLKOUT3}]
create_generated_clock -name rtc_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -divide_by 5 [get_pins {u_lab/u_core_pll/u_pll/PLL_inst/CLKOUT4}]
# Headless control build: no video PLL or serializer. Both ends of the
# menu publication handshake use APB PCLK and retain ordinary setup/hold.
# Only asynchronous key synchronizer first-stage D endpoints are excepted.
set_false_path -from [get_ports {keys_n[*]}] -to [get_pins {u_lab/u_apb/key_meta*_s0/D}]
# 50MHz DTCM/AHB/APB retain one common PLL and in-phase 1:1 AHB/APB.
# All remaining recovery/removal checks remain enabled for review.
# Standalone Note20 selects its own UART TX; unused legacy debug monitor
# and its mailbox are trimmed. Normal control_lab.sdc retains their checks.

# Original free-running audio/control PLLs; no new sample-rate clocks.
create_generated_clock -name control_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 24 -divide_by 25 [get_pins {u_lab/u_system/u_control_input/u_usb_midi/usb_pll/u_pll/PLL_inst/CLKOUT0}]
create_clock -name esp_spi_sclk -period 100.000 -waveform {0.000 50.000} [get_ports {ESP_SPI_SCLK}]
create_generated_clock -name subtractive_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 158 [get_pins {u_lab/u_system/u_clock_reset/u_subtractive_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name pcm_audio_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 233 -divide_by 474 [get_pins {u_lab/u_system/u_clock_reset/u_audio_pll/u_pll/PLL_inst/CLKOUT0}]
create_generated_clock -name fm_native_clk -source [get_ports {sys_clk}] -master_clock board_ref_clk -multiply_by 8 -divide_by 25 [get_pins {u_lab/u_system/u_clock_reset/u_fm_pll/u_pll/PLL_inst/CLKOUT0}]

# Only the asynchronous physical USB receive first-stage D endpoints.
set_false_path -from [get_ports {USB_MIDI_DP USB_MIDI_DM}] -to [get_pins {u_lab/u_system/u_control_input/u_usb_midi/phy/*_ms_s0/D}]
# Preserve the existing SPI bundled-data budget, never disable general setup.
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {esp_spi_sclk}]
set_max_delay -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {esp_spi_sclk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {esp_spi_sclk}]
set_false_path -to [get_pins {u_lab/u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/req_meta_s0/D u_lab/u_system/u_control_input/u_transport/g_midi.u_raw_mailbox/ack_meta_s0/D}]
# AE350 command/route: request and ACK only; response and payload stay timed.
set_false_path -to [get_pins {u_lab/u_command_cdc/req_meta_s0/D u_lab/u_command_cdc/ack_meta_s0/D u_lab/u_route_cdc/req_meta_s0/D u_lab/u_route_cdc/ack_meta_s0/D}]
set_max_delay -from [get_clocks {apb_clk}] -to [get_pins {u_lab/u_command_cdc/dst_data_o_*_s0/D u_lab/u_route_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_pins {u_lab/u_command_cdc/dst_data_o_*_s0/D u_lab/u_route_cdc/dst_data_o_*_s0/D}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {apb_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {apb_clk}]
set_max_delay -from [get_clocks {apb_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_clocks {control_clk}]
set_false_path -to [get_pins {u_lab/u_feedback_cdc/req_meta_s0/D u_lab/u_feedback_cdc/ack_meta_s0/D u_lab/u_backend_status_cdc/req_meta_s0/D u_lab/u_backend_status_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_ingress_cdc/byte_req_meta_s0/D u_lab/u_ingress_cdc/byte_ack_meta_s0/D u_lab/u_ingress_cdc/flush_req_meta_s0/D u_lab/u_ingress_cdc/flush_ack_meta_s0/D u_lab/u_ingress_cdc/done_req_meta_s0/D u_lab/u_ingress_cdc/done_ack_meta_s0/D}]
# Native command/idle lanes and their readiness first stages. Generated
# blocks use dot-qualified instance names; preserve meta->sync/data timing.
set_false_path -to [get_pins {u_lab/u_system/u_synth_manager/g_ae350_backend.*/req_meta_s0/D u_lab/u_system/u_synth_manager/g_ae350_backend.*/ack_meta_s0/D u_lab/u_system/u_synth_manager/g_ae350_backend.ready_meta*_s0/D}]
# Existing control->subtractive coherent state still carries output routing.
# ABI2 removes MIDI event CDC; the surviving audio-state handshake is nested
# under u_lab/u_control_state/u_payload_cdc. Its return-status payload is optimized out.
set_false_path -to [get_pins {u_lab/u_system/u_synth_manager/g_control_domain.u_audio_control/u_control_state/u_payload_cdc/req_meta_s0/D u_lab/u_system/u_synth_manager/g_control_domain.u_audio_control/u_control_state/u_payload_cdc/ack_meta_s0/D}]
# Existing control/audio bundled transfers retain their 20ns setup budget.
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {subtractive_clk}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_clocks {control_clk}]
set_max_delay -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_clocks {fm_native_clk}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_clocks {control_clk}]

# @block audio.request_sub when=ENABLE_SUBTRACTIVE or ENABLE_ADDITIVE or ENABLE_FLUTE
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/req_meta_s0/D u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {subtractive_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {subtractive_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/req_meta_s0/D u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_sub_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_sub

# @block audio.request_ks when=ENABLE_KS
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/req_meta_s0/D u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {control_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {control_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/req_meta_s0/D u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_ks_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_ks

# @block audio.request_fm when=ENABLE_FM
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/req_meta_s0/D u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_request/dst_reset_sync_s0/CLEAR}]
set_max_delay -from [get_clocks {fm_native_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {fm_native_clk}] -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_data_o_*_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/req_meta_s0/D u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/src_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/src_reset_sync_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_reset_meta_s0/CLEAR u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_response/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.u_*_reset/release_pipe_*_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_audio_output/u_fm_channel/g_enabled.response_error_meta_s0/D}]
# @end audio.request_fm

# @block audio.pcm_status_snapshots when=ALWAYS
# ENGINE_GATES: ACTIVE [ENABLE_REVERB=1]
# 11.4 PCM1快照held数据与首级/资格；统一PCM管线常驻。
set_max_delay -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/dst_data_o_*_s0/D u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {pcm_audio_clk}] -to [get_pins {u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/dst_data_o_*_s0/D u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_data_o_*_s0/D}]
# 三组计数器快照均由audio24进入control48，使用显式20ns载荷预算。
set_false_path -to [get_pins {u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/req_meta_s0/D u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/src_reset_meta_s0/CLEAR u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/src_reset_sync_s0/CLEAR u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/dst_reset_meta_s0/CLEAR u_lab/u_system/u_pcm_status/u_*_snapshot_cdc/dst_reset_sync_s0/CLEAR}]
set_false_path -to [get_pins {u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/req_meta_s0/D u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/src_reset_meta_s0/CLEAR u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/src_reset_sync_s0/CLEAR u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_reset_meta_s0/CLEAR u_lab/u_system/u_pcm_status/g_subtractive_status.u_snapshot_cdc/dst_reset_sync_s0/CLEAR}]
report_timing -setup -mod_ins {u_lab/u_system/u_pcm_status} -max_paths 100 -max_common_paths 1
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
set_false_path -to [get_pins {u_lab/u_system/u_reverb_control/read_req_meta_s0/D u_lab/u_system/u_reverb_control/cs_meta_s0/D}]
set_false_path -to [get_pins {u_lab/u_system/u_reverb_control/cfg_reset_meta_s0/CLEAR u_lab/u_system/u_reverb_control/cfg_reset_sync_s0/CLEAR u_lab/u_system/u_reverb_control/synth_reset_meta_s0/CLEAR u_lab/u_system/u_reverb_control/synth_reset_sync_s0/CLEAR}]
report_timing -setup -mod_ins {u_lab/u_system/u_reverb_control} -max_paths 1000 -max_common_paths 1
report_timing -hold -mod_ins {u_lab/u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -recovery -mod_ins {u_lab/u_system/u_reverb_control} -max_paths 100 -max_common_paths 1
report_timing -removal -mod_ins {u_lab/u_system/u_reverb_control} -max_paths 100 -max_common_paths 1

# --------------------------------------------------------------------------------------------
# RVR1 synth_clk_i最终仍接subtractive_clk；ACK/读回时钟范围保持。
# @end effect.reverb_control

# ABI2 template FIFO: Gray pointers have a bounded physical datapath.
# Keep first-stage setup bounded (not a blanket false path); only hold is
# asynchronous. The fastest pointer source is the 50MHz APB POP clock.
# Gowin uses _s1 for CE flops here; only the suffix may vary. Gray bit6 equals
# binary bit6 and shares that physical flop. Include it without selecting any
# meta/sync source Q or second-stage destination D.
set_max_delay -from [get_pins {u_lab/u_template_ingress/wgray_?_s*/Q u_lab/u_template_ingress/wbin_6_s*/Q}] -to [get_pins {u_lab/u_template_ingress/wgray_meta_?_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_lab/u_template_ingress/wgray_?_s*/Q u_lab/u_template_ingress/wbin_6_s*/Q}] -to [get_pins {u_lab/u_template_ingress/wgray_meta_?_s*/D}]
set_max_delay -from [get_pins {u_lab/u_template_ingress/rgray_?_s*/Q u_lab/u_template_ingress/rbin_6_s*/Q}] -to [get_pins {u_lab/u_template_ingress/rgray_meta_?_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_lab/u_template_ingress/rgray_?_s*/Q u_lab/u_template_ingress/rbin_6_s*/Q}] -to [get_pins {u_lab/u_template_ingress/rgray_meta_?_s*/D}]
set_false_path -to [get_pins {u_lab/u_template_ingress/overflow_meta_s0/D u_lab/u_template_status_cdc/req_meta_s0/D u_lab/u_template_status_cdc/ack_meta_s0/D}]
# CPU ACK words are held through the return handshake, including stopped SCLK.
set_max_delay -from [get_clocks {apb_clk}] -to [get_pins {u_lab/u_template_status_cdc/dst_data_o_*_s0/D}] 20.000
set_false_path -hold -from [get_clocks {apb_clk}] -to [get_pins {u_lab/u_template_status_cdc/dst_data_o_*_s0/D}]


# AE350 T0..T3: one active48MHz capture + heldCPU result, no FPGA table.
# Only asynchronous request/ack and loss/alive first-stage D are excepted.
set_false_path -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/ack_meta*_s*/D}]
set_false_path -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/req_meta*_s*/D}]
set_false_path -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/loss_meta*_s*/D}]
set_false_path -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/alive_meta*_s*/D}]
set_false_path -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/result_loss_meta_s0/D}]
set_max_delay -from [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/held_data*_s*/Q}] -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/dst_data_o*_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/held_data*_s*/Q}] -to [get_pins {u_lab/u_system/g_cpu_timing.u_capture/*/dst_data_o*_s*/D}]
# CPU submits UART bytes without a wait; this single held byte crosses to sys50.
set_false_path -to [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/ack_meta_s0/D}]
set_false_path -to [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/req_meta_s0/D}]
set_max_delay -from [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/held_data*_s*/Q}] -to [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/dst_data_o*_s*/D}] 20.000
set_false_path -hold -from [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/held_data*_s*/Q}] -to [get_pins {u_lab/g_cpu_timing_uart.u_apb/u_uart_cdc/dst_data_o*_s*/D}]
# Reset recovery/removal, meta->sync and ordinary data remain timed.

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
report_timing -recovery -max_paths 25 -max_common_paths 1
report_timing -removal -max_paths 25 -max_common_paths 1
report_min_pulse_width -nworst 20 -detail

# The prepared register view consumes a held SPI address after req synchronization.
set_false_path -to [get_pins {u_lab/u_system/u_synth_manager/g_prepared_register_view.read_meta_s0/D}]
