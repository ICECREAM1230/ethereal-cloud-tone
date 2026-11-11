`default_nettype none
module synth_system #(
    // 主工程由活动顶层传入参数；此处默认值仅用于直接实例化本模块。
    parameter bit ENABLE_MIDI_NOTE20_TEST = 1'b0, // AE350前20条USB NoteOn四时刻/空闲UART；无PCM上传控制
    parameter bit ENABLE_MIDI_PCM_LATENCY_TEST = 1'b0, // 可裁剪的独立延迟实验旁路
    parameter bit ENABLE_AE350_CONTROL = 1'b0, // D盘实验：AE350为唯一MIDI/voice owner
    parameter bit ENABLE_MIDI_STATUS_LED = 1'b1, // MIDI 状态灯：1=启用，0=关闭
    parameter bit ENABLE_MIDI_DEBUG_UART = 1'b0, // MIDI 调试串口：默认关闭，“全模块开启”时也保持关闭；仅专项诊断时手动开启，会显著增加 BRAM 占用
    parameter bit SHARE_ADDITIVE_DDS = 1'b0, // 加法 DDS 共享：1=共享，0=独立；加法合成器启用时生效
    parameter bit ENABLE_REVERB = 1'b1, // 混响 DSP：1=启用，0=旁路；SPI 混响控制接口仍保留
    parameter logic [15:0] REVERB_WET_Q15 = 16'd8192, // 混响湿声比例默认值（Q15）；8192 对应 25%，可由 SPI 更新
    parameter logic [15:0] REVERB_DAMP_Q15 = 16'd8192, // 混响阻尼默认值（Q15）；8192 对应 0.25，可由 SPI 更新
    parameter bit SHARED_REVERB=1'b1, // 混响架构：1=共用一套混响，0=两套独立混响；ENABLE_REVERB=1 时生效
    parameter bit FM_DIAG_LED_ACTIVE_LOW=1'b1, // FM 诊断灯极性：1=低电平点亮，0=高电平点亮
    parameter bit ENABLE_SUBTRACTIVE=1'b1, // 减法合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_KS_TIMBRES=1'b0,
    parameter bit ENABLE_KS_LIMITER=1'b1, // KS动态峰值限幅编译开关
    parameter bit ENABLE_KS=1'b1, // KS 物理建模合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_FM=1'b1, // FM 合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_ADDITIVE=1'b0, // 加法合成器：1=启用，0=不参与综合及模式切换
    parameter bit ENABLE_FLUTE=1'b0, // 独立笛物理模型，编译前开关
    parameter bit ENABLE_OUTPUT_TILT=1'b1, // 输出低通滤波：1=启用，0=旁路；同时作用于普通 PCM 与 FM PCM
    parameter integer OUTPUT_TILT_SHIFT=3, // 低通截止参数；3 保持默认音色，2/4 用于更亮/更暗 A/B
    parameter bit ENABLE_SENSOR_I2C=1'b0,
    parameter integer SENSOR_I2C_HZ=100_000,
    parameter bit ENABLE_FFT=1'b1, // FFT显示：0移除FFT分析/绘图，KEY1不改变音频
    parameter bit ENABLE_HDMI=1'b0 // HDMI 示波显示：1=启用，0=移除显示、视频 PLL 和显示缓存

) (

    input wire latency_cpu_clk_i,latency_cpu_rst_ni,
    input wire latency_cpu_submit_i,latency_cpu_ack_i,
    input wire latency_note_submit_i,
    input wire [1:0] latency_cpu_engine_i,latency_cpu_ack_engine_i,
    input wire latency_cpu_commit_i,
    input wire [1:0] latency_cpu_commit_engine_i,
    input wire [68:0] latency_cpu_submit_identity_i,latency_cpu_commit_identity_i,latency_cpu_ack_identity_i,
    output wire [511:0] latency_result_o,
    output wire latency_result_valid_o,latency_session_no,
    input wire [4:0] note20_read_index_i,
    output wire [1023:0] note20_read_data_o,
    output wire note20_read_valid_o,note20_done_o,note20_loss_o,note20_session_no,
    output wire [4:0] note20_allocated_o,note20_frozen_o,
    output wire [575:0] cpu_timing_result_o,
    output wire cpu_timing_valid_o,cpu_timing_loss_o,
    input wire cpu_timing_ready_i,
    input wire ae350_owner_rst_ni,
    output wire ae350_control_clk_o,ae350_control_rst_no,
    output wire ae350_byte_valid_o,
    input wire ae350_byte_ready_i,
    output wire [1:0] ae350_byte_source_o,
    output wire [7:0] ae350_byte_data_o,
    output wire [63:0] ae350_byte_tick_o,
    output wire ae350_flush_valid_o,
    input wire ae350_flush_ready_i,
    output wire [3:0] ae350_flush_mask_o,
    output wire [63:0] ae350_flush_tick_o,
    input wire ae350_flush_done_valid_i,
    input wire [3:0] ae350_flush_done_mask_i,
    input wire [31:0] ae350_flush_done_sequence_i,
    input wire ae350_cmd_valid_i,
    output wire ae350_cmd_ready_o,
    input wire [159:0] ae350_cmd_data_i,
    input wire [31:0] ae350_cmd_sequence_i,
    output wire [31:0] ae350_cmd_result_o,
    output wire ae350_fb_valid_o,
    input wire ae350_fb_ready_i,
    output wire [1:0] ae350_fb_engine_o,
    output wire [4:0] ae350_fb_voice_o,
    output wire [31:0] ae350_fb_generation_o,
    output wire [2:0] ae350_ready_o,
    output wire [31:0] ae350_fault_o,
    input wire [1:0] ae350_route_i,
    input wire [255:0] ae350_template_status_i,
    input wire [15:0] ae350_template_free_i,
    input wire ae350_template_overflow_i,
    output wire ae350_template_write_o,
    output wire [15:0] ae350_template_addr_o,
    output wire [31:0] ae350_template_data_o,
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
    // Only J8/PMOD1 is used by ESP32. FM diagnostics are opt-in.
    input  wire [3:0] ESP_PMOD1_PASSIVE,
    output wire [7:0] FM_DIAG_LED,
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
`ifdef SYNTH_SENSOR_I2C_PORTS
    ,inout wire scl_io,sda_io,
    output wire fdc_addr_o,fdc_sd_o,
    input wire fdc_int_ni,
    inout wire tof1_scl_io,tof1_sda_io,tof1_xshut_io,
    input wire tof1_int_ni,
    inout wire tof2_scl_io,tof2_sda_io,tof2_xshut_io,
    input wire tof2_int_ni
`endif
    // output wire [7:0] LEDs  // Retired: these pads now belong to PMOD0.

);
    localparam bit ENABLE_MIDI=1'b1;
    localparam bit AUTONOMOUS_USB_MIDI=1'b1;
    localparam bit ENABLE_MIDI_DEBUG=ENABLE_MIDI_DEBUG_UART;
    localparam integer CONTROL_CLK_HZ=48000000;
    localparam integer MIDI_DEBUG_CLK_HZ=CONTROL_CLK_HZ;
    wire control_clk,control_rst_n,subtractive_clk,subtractive_rst_n,fm_native_clk,fm_rst_n,audio_clk,audio_rst_n;
    wire ks_native_clk,ks_clock_locked;
    // USB48_PLL is free-running from board50MHz; its clock/LOCK outputs do
    // not depend on the USB session or the controller's reset release.
    clock_reset u_clock_reset(.sys_clk,.sys_rst_n,
        .control_source_clk_i(ks_native_clk),.control_source_locked_i(ks_clock_locked),
        .control_clk,.control_rst_n,.subtractive_clk,.subtractive_rst_n,.fm_native_clk,.fm_rst_n,.audio_clk,.audio_rst_n);
    wire rst=~control_rst_n,rst_n=control_rst_n;
    assign ae350_control_clk_o=control_clk;
    assign ae350_control_rst_no=control_rst_n;
    wire owner_reset_n;
    generate if(ENABLE_AE350_CONTROL)begin:g_owner_reset_release
        // The CPU/session reset can change on PCLK or a PLL reference edge.
        // Assert cancellation immediately; release the ingress, RAW mailbox
        // and control owner only on their own control-clock edges.
        jt51_reset_sync u_owner_control(.clk_i(control_clk),
            .rst_n_i(ae350_owner_rst_ni && control_rst_n),.rst_n_o(owner_reset_n));
    end else begin:g_legacy_owner_reset
        assign owner_reset_n=rst_n;
    end endgenerate
    wire legacy_usb_local_flush;

    wire control_frame_tick;
    wire sclk=ESP_SPI_SCLK,cs=ESP_SPI_CS_N,mosi=ESP_SPI_MOSI;
    wire miso;assign ESP_SPI_MISO=miso;
    wire [31:0] usb_packet_data;
    wire usb_packet_valid,usb_packet_ready,usb_session_flush,usb_configured,usb_error,usb_local_flush,usb_flush_busy;
    wire midi_debug_uart_tx;assign MIDI_DEBUG_TX=midi_debug_uart_tx;
    wire [5:0] core_led;
    wire legacy_chord_play_i,local_engine_cycle_i,dac_switch_busy_i,dac_fm_active_i,dac_frame_tick_i,dac_frame_fm_i;
    wire pcm_valid,fm_path_rst_n_o,fm_route_o,fm_pcm_valid_o;
    wire pcm_sub_request_valid,pcm_sub_request_ready,pcm_sub_response_valid;
    wire pcm_ks_request_valid,pcm_ks_request_ready,pcm_ks_response_valid;
    wire pcm_fm_request_valid,pcm_fm_request_ready,pcm_fm_response_valid;
    wire [63:0] pcm_sub_request_tag,pcm_sub_response_tag,pcm_ks_request_tag,pcm_ks_response_tag,pcm_fm_request_tag,pcm_fm_response_tag;
    wire signed [23:0] pcm_sub_l,pcm_sub_r,pcm_ks_l,pcm_ks_r,pcm_fm_l,pcm_fm_r;
    wire [2:0] pcm_source,pcm_serial_source;
    wire pcm_mute,pcm_test_tone,pcm_output_frame_tick;
    wire [31:0] pcm_revision,pcm_serial_revision;
    wire [63:0] pcm_serial_tag;
    wire pcm_serial_fm=pcm_serial_source==3'd2;
    wire reverb_synth_rst_n_o,reverb_fm_rst_n_o,reverb_synth_mute_o,reverb_fm_mute_o,reverb_synth_bypass_o;

    wire cfg_write_i,cfg_read_req_toggle_i;
    wire [15:0] cfg_addr_i,cfg_read_addr_i;
    wire [31:0] cfg_data_i,cfg_read_data_o;
    assign ae350_template_addr_o=cfg_addr_i;
    assign ae350_template_data_o=cfg_data_i;
    wire [31:0] engine_cfg_read_data,reverb_cfg_read_data,pcm_diag_read_data;
    wire pcm_diag_read_selected;
    wire [127:0] pcm_synth_stats,pcm_fm_stats,pcm_audio_stats;
    wire reverb_read_selected;
    wire reverb_write_selected=cfg_addr_i[15:3]==13'h0e4;
    wire [15:0] reverb_wet_q15,reverb_damp_q15;
    assign cfg_read_data_o=reverb_read_selected ? reverb_cfg_read_data :
        pcm_diag_read_selected ? pcm_diag_read_data : engine_cfg_read_data;
    // SNS1/MOD1 keep their existing addresses; imported PCM1 uses 0780..078F.
    pcm_pipeline_status #(.SPLIT_SYNTH_STATUS(1'b1),.BASE_ADDR(16'h0780)) u_pcm_status(
        .sys_clk_i(control_clk),.subtractive_clk_i(audio_clk),.fm_clk_i(audio_clk),
        .audio_clk_i(audio_clk),.rst_n_i(rst_n),
        .synth_stats_i(pcm_synth_stats),.fm_stats_i(pcm_fm_stats),.audio_stats_i(pcm_audio_stats),
        .route_fm_i(pcm_serial_fm),.audio_reset_n_i(audio_rst_n),
        .read_req_toggle_i(cfg_read_req_toggle_i),.read_addr_i(cfg_read_addr_i),
        .read_selected_o(pcm_diag_read_selected),.read_data_o(pcm_diag_read_data));
    // Control lifetime follows the global control reset, independently of
    // effect-tail clears, engine selection, local Panic and FM clock faults.
    reverb_control #(.DEFAULT_WET_Q15(REVERB_WET_Q15),.DEFAULT_DAMP_Q15(REVERB_DAMP_Q15)) u_reverb_control (
        .cfg_clk_i(sclk),.synth_clk_i(subtractive_clk),.rst_n_i(rst_n),.cfg_cs_n_i(cs),
        .write_enable_i(cfg_write_i),.write_addr_i(cfg_addr_i),.write_data_i(cfg_data_i),
        .read_req_toggle_i(cfg_read_req_toggle_i),.read_addr_i(cfg_read_addr_i),
        .read_selected_o(reverb_read_selected),.read_data_o(reverb_cfg_read_data),
        .wet_q15_o(reverb_wet_q15),.damp_q15_o(reverb_damp_q15));
    wire [1:0] raw_source;
    wire [7:0] raw_data;
    wire raw_valid,raw_input_enabled,source_raw_ready,source_packet_ready;
    wire latency_raw_accept,latency_usb_accept,latency_usb_rst_n;
    wire [1:0] latency_raw_source;
    wire [7:0] latency_raw_data;
    wire [31:0] latency_usb_packet;
    wire latency_serial_load,latency_wire_end;
    wire signed [23:0] latency_serial_left,latency_serial_right;
    wire [1:0] latency_serial_engine;
    wire [63:0] latency_serial_tag;
    wire [7:0] latency_serial_quality;
    wire [3:0] mailbox_flush,decoder_flush,midi_source_reset;
    wire [31:0] mailbox_read,event_count;
    midi_pkg::midi_event_t midi_event;
    midi_pkg::midi_sysex_t unused_midi_sysex;
    wire midi_event_valid,midi_event_ready,midi_event_pending,unused_sysex_valid;
    wire [31:0] unused_fault_count,unused_padding_count,unused_sysex_count;
    wire local_switch_mute;
    wire [7:0] fm_diag_status;
    wire fm_diag_requested;
    wire signed [23:0] selected_left,selected_right,additive_left,additive_right,fm_raw_left,fm_raw_right;
    wire additive_selected,additive_bypass_tilt,selected_fault,output_filter_rst_n;
    wire cell_tick,sample_tick48,cell_tick48;
    // Extra modulation is exported by the input layer; engine/voice adapters
    // remain separate from MIDI byte parsing and source arbitration.
    wire modulation_valid,modulation_active,modulation_source;
    wire signed [17:0] modulation_pitch_delta;
    wire [16:0] modulation_gain;
    wire sensor_controls_valid,sensor_active,sensor_group;
    wire [1:0] sensor_source_id;
    wire [13:0] sensor_pitch;
    wire [16:0] sensor_gain;
    control_input_frontend #(.ENABLE_MIDI_STATUS_LED(ENABLE_MIDI_STATUS_LED),
        .ENABLE_SENSOR_I2C(ENABLE_SENSOR_I2C),.SENSOR_I2C_HZ(SENSOR_I2C_HZ),
        .AE350_CONTROL(ENABLE_AE350_CONTROL),
        .ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST || ENABLE_MIDI_NOTE20_TEST)) u_control_input(
        .sysclk(control_clk),
        .rst_n(owner_reset_n),
        .ae350_flush_done_valid_i(ae350_flush_done_valid_i),
        .ae350_flush_done_mask_i(ae350_flush_done_mask_i),
        .ae350_template_status_i,.ae350_template_free_i,.ae350_template_overflow_i,.ae350_template_write_o,
        .sclk(sclk),
        .cs(cs),
        .mosi(mosi),
        .miso(miso),
        .latency_raw_accept_o(latency_raw_accept),.latency_raw_source_o(latency_raw_source),
        .latency_raw_data_o(latency_raw_data),.latency_usb_accept_o(latency_usb_accept),
        .latency_usb_packet_o(latency_usb_packet),.latency_usb_rst_no(latency_usb_rst_n),
        .cfg_write_o(cfg_write_i),
        .cfg_addr_o(cfg_addr_i),
        .cfg_data_o(cfg_data_i),
        .cfg_read_req_toggle_o(cfg_read_req_toggle_i),
        .cfg_read_addr_o(cfg_read_addr_i),
        .cfg_read_data_i(cfg_read_data_o),
        .usb_packet_valid(usb_packet_valid),
        .usb_configured(usb_configured),
        .usb_error(usb_error),
        .usb_flush_busy(usb_flush_busy),
        .usb_packet_ready(usb_packet_ready),
        .source_packet_ready(source_packet_ready),
        .source_raw_ready(source_raw_ready),
        .local_switch_mute(local_switch_mute),
        .raw_source(raw_source),
        .raw_data(raw_data),
        .raw_valid(raw_valid),
        .raw_input_enabled(raw_input_enabled),
        .mailbox_flush(mailbox_flush),
        .mailbox_read(mailbox_read),
        .event_count(event_count),
        .sys_clk(sys_clk),
        .sys_rst_n(sys_rst_n),
        .usb_local_flush(usb_local_flush),
        .usb_dp(USB_MIDI_DP),
        .usb_dm(USB_MIDI_DM),
        .usb_packet_data(usb_packet_data),
        .usb_session_flush(usb_session_flush),
        .status_led(MIDI_STATUS_LED),
        .usb_clk_o(ks_native_clk),.usb_pll_locked_o(ks_clock_locked),
        .modulation_valid_o(modulation_valid),.modulation_active_o(modulation_active),.modulation_source_o(modulation_source),
        .modulation_pitch_delta_o(modulation_pitch_delta),.modulation_gain_o(modulation_gain),
        .sensor_controls_valid_o(sensor_controls_valid),.sensor_active_o(sensor_active),.sensor_group_o(sensor_group),
        .sensor_source_id_o(sensor_source_id),.sensor_pitch_o(sensor_pitch),.sensor_gain_o(sensor_gain)
`ifdef SYNTH_SENSOR_I2C_PORTS
        ,.scl_io,.sda_io,.fdc_addr_o,.fdc_sd_o,.fdc_int_ni,
        .tof1_scl_io,.tof1_sda_io,.tof1_xshut_io,.tof1_int_ni,
        .tof2_scl_io,.tof2_sda_io,.tof2_xshut_io,.tof2_int_ni
`endif
    );
    generate if(ENABLE_AE350_CONTROL)begin:g_ae350_input
        wire [3:0] blocked;
        wire [31:0] accepted,packet_errors,padding;
        ae350_ingress_frontend u_ingress(
            .clk_i(control_clk),.rst_ni(owner_reset_n),
            .usb_packet_data_i(usb_packet_data),.usb_packet_valid_i(usb_packet_valid),
            .usb_packet_ready_o(source_packet_ready),.usb_session_flush_i(usb_session_flush),
            .usb_flush_busy_i(usb_flush_busy),.usb_local_flush_o(usb_local_flush),
            .raw_source_i(raw_source),.raw_data_i(raw_data),.raw_valid_i(raw_valid),
            .raw_ready_o(source_raw_ready),.raw_enabled_i(raw_input_enabled),.mailbox_flush_i(mailbox_flush),
            .flush_done_valid_i(ae350_flush_done_valid_i),.flush_done_mask_i(ae350_flush_done_mask_i),
            .flush_done_sequence_i(ae350_flush_done_sequence_i),
            .byte_valid_o(ae350_byte_valid_o),.byte_ready_i(ae350_byte_ready_i),
            .byte_source_o(ae350_byte_source_o),.byte_data_o(ae350_byte_data_o),.byte_tick_o(ae350_byte_tick_o),
            .flush_valid_o(ae350_flush_valid_o),.flush_ready_i(ae350_flush_ready_i),
            .flush_mask_o(ae350_flush_mask_o),.flush_tick_o(ae350_flush_tick_o),
            .blocked_sources_o(blocked),.accepted_count_o(accepted),
            .packet_error_count_o(packet_errors),.padding_count_o(padding));
        assign midi_event='0;assign midi_event_valid=0;assign midi_event_pending=0;assign midi_source_reset=0;
        assign midi_debug_uart_tx=1;
    end else if(ENABLE_MIDI)begin:g_decode
        assign usb_local_flush=legacy_usb_local_flush;
        assign ae350_byte_valid_o=0;assign ae350_byte_source_o=0;assign ae350_byte_data_o=0;assign ae350_byte_tick_o=0;
        assign ae350_flush_valid_o=0;assign ae350_flush_mask_o=0;assign ae350_flush_tick_o=0;

        midi_decoder #(.REGISTERED_CONTROLS(1'b1),.INDEPENDENT_USB(AUTONOMOUS_USB_MIDI),.RAW_SOURCE3_ENABLE(1'b0),
            .ENABLE_DIAGNOSTICS(ENABLE_MIDI_DEBUG),.DIAG_CLK_HZ(MIDI_DEBUG_CLK_HZ)) u_midi_decoder (
            .clk(control_clk),.rst(!rst_n),.enabled(raw_input_enabled),
            .packet_data(usb_packet_data),.packet_valid(usb_packet_valid && !local_switch_mute),.packet_ready(source_packet_ready),
            .session_flush(usb_session_flush),.raw_data,.raw_source,
            .raw_valid(raw_valid && !local_switch_mute),.raw_ready(source_raw_ready),
            .flush(decoder_flush),.source_reset(midi_source_reset),
            .event_data(midi_event),.event_valid(midi_event_valid),.event_pending(midi_event_pending),.event_ready(midi_event_ready),
            .sysex_data(unused_midi_sysex),.sysex_valid(unused_sysex_valid),.sysex_ready(1'b1),
            .fault_count(unused_fault_count),.padding_count(unused_padding_count),
            .event_count(),.sysex_count(unused_sysex_count),
            .diagnostic_uart_tx(midi_debug_uart_tx)
        );
    end else begin:g_no_decode
        assign usb_local_flush=legacy_usb_local_flush;
        assign ae350_byte_valid_o=0;assign ae350_byte_source_o=0;assign ae350_byte_data_o=0;assign ae350_byte_tick_o=0;
        assign ae350_flush_valid_o=0;assign ae350_flush_mask_o=0;assign ae350_flush_tick_o=0;
        assign midi_event='0;assign midi_event_valid=0;assign midi_event_pending=0;assign midi_source_reset=0;
        assign source_raw_ready=0;assign source_packet_ready=1;assign midi_debug_uart_tx=1;
    end endgenerate
    synthesizer_manager #(
        .ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_CONTROL),
        .PCM_REQUEST_DRIVEN(1'b1),.CONTROL_CLOCK_SPLIT(1'b1),.CONTROL_CLK_HZ(CONTROL_CLK_HZ),.CONTROL_DIAG_BASE(16'h0790),
        .ENABLE_FM(ENABLE_FM),.ENABLE_SUBTRACTIVE(ENABLE_SUBTRACTIVE),.ENABLE_KS(ENABLE_KS),.ENABLE_KS_LIMITER(ENABLE_KS_LIMITER),.ENABLE_KS_TIMBRES(ENABLE_KS_TIMBRES),
        .ENABLE_MIDI(1'b1),.ENABLE_MIDI_DEBUG(ENABLE_MIDI_DEBUG_UART),
        .MIDI_DEBUG_CLK_HZ(MIDI_DEBUG_CLK_HZ),.FM_SHARED_CLOCK(1'b0),.FM_NATIVE96(1'b1),.FM_ISLAND_CLOCK(1'b1),.KS_ISLAND_CLOCK(1'b1),
        .LOCAL_CHORD_CONTROL(1'b1),.AUTONOMOUS_USB_MIDI(1'b1),.ENABLE_LOCAL_ENGINE_CONTROL(1'b1),
        .ENABLE_ADDITIVE(ENABLE_ADDITIVE),.ENABLE_FLUTE(ENABLE_FLUTE),.SHARE_ADDITIVE_DDS(SHARE_ADDITIVE_DDS)
    ) u_synth_manager(
        .owner_rst_ni(owner_reset_n),.ae350_route_i(ae350_route_i),
        .ae350_cmd_valid_i(ae350_cmd_valid_i),.ae350_cmd_ready_o(ae350_cmd_ready_o),
        .ae350_cmd_data_i(ae350_cmd_data_i),.ae350_cmd_sequence_i(ae350_cmd_sequence_i),
        .ae350_cmd_result_o(ae350_cmd_result_o),.ae350_fb_valid_o(ae350_fb_valid_o),
        .ae350_fb_ready_i(ae350_fb_ready_i),.ae350_fb_engine_o(ae350_fb_engine_o),
        .ae350_fb_voice_o(ae350_fb_voice_o),.ae350_fb_generation_o(ae350_fb_generation_o),
        .ae350_ready_o(ae350_ready_o),.ae350_fault_o(ae350_fault_o),
        .subtractive_clk(subtractive_clk),
        .control_clk_i(control_clk),.control_rst_n_i(control_rst_n),.subtractive_rst_n_i(subtractive_rst_n),
        .rst(rst),
        .legacy_chord_play_i(legacy_chord_play_i),
        .local_engine_cycle_i(local_engine_cycle_i),
        .dac_switch_busy_i(dac_switch_busy_i),
        .dac_fm_active_i(dac_fm_active_i),
        .dac_frame_tick_i(dac_frame_tick_i),
        .dac_frame_fm_i(dac_frame_fm_i),
        .pcm_sub_request_valid_i(pcm_sub_request_valid),.pcm_sub_request_ready_o(pcm_sub_request_ready),
        .pcm_sub_request_tag_i(pcm_sub_request_tag),.pcm_sub_response_valid_o(pcm_sub_response_valid),
        .pcm_sub_response_tag_o(pcm_sub_response_tag),.pcm_sub_left_o(pcm_sub_l),.pcm_sub_right_o(pcm_sub_r),
        .pcm_ks_request_valid_i(pcm_ks_request_valid),.pcm_ks_request_ready_o(pcm_ks_request_ready),
        .pcm_ks_request_tag_i(pcm_ks_request_tag),.pcm_ks_response_valid_o(pcm_ks_response_valid),
        .pcm_ks_response_tag_o(pcm_ks_response_tag),.pcm_ks_left_o(pcm_ks_l),.pcm_ks_right_o(pcm_ks_r),
        .pcm_fm_request_valid_i(pcm_fm_request_valid),.pcm_fm_request_ready_o(pcm_fm_request_ready),
        .pcm_fm_request_tag_i(pcm_fm_request_tag),.pcm_fm_response_valid_o(pcm_fm_response_valid),
        .pcm_fm_response_tag_o(pcm_fm_response_tag),.pcm_fm_left_o(pcm_fm_l),.pcm_fm_right_o(pcm_fm_r),
        .pcm_source_o(pcm_source),.pcm_mute_o(pcm_mute),.pcm_test_tone_o(pcm_test_tone),.pcm_revision_o(pcm_revision),
        .pcm_output_clk_i(audio_clk),.pcm_output_rst_n_i(audio_rst_n),
        .pcm_output_frame_tick_i(pcm_output_frame_tick),.pcm_output_tap_valid_i(scope_pcm_valid),
        .pcm_output_source_i(pcm_serial_source),.pcm_output_revision_i(pcm_serial_revision),
        .fm_native_clk(fm_native_clk),
        .fm_rst_n(fm_rst_n),
        .ks_native_clk(ks_native_clk),.ks_clock_locked_i(ks_clock_locked),
        .cfg_clk_i(sclk),
        .cfg_write_i(cfg_write_i && !reverb_write_selected),
        .cfg_addr_i(cfg_addr_i),
        .cfg_data_i(cfg_data_i),
        .cfg_read_req_toggle_i(cfg_read_req_toggle_i),
        .cfg_read_addr_i(cfg_read_addr_i),
        .cfg_read_data_o(engine_cfg_read_data),
        .usb_session_started_i(usb_configured || usb_packet_valid),
        .usb_flush_busy(usb_flush_busy),
        .raw_input_enabled(raw_input_enabled),
        .mailbox_flush(mailbox_flush),
        .midi_source_reset(midi_source_reset),
        .mailbox_read(mailbox_read),
        .midi_event(midi_event),
        .midi_event_valid(midi_event_valid),
        .midi_event_pending(midi_event_pending),
        .midi_event_ready(midi_event_ready),
        .usb_local_flush(legacy_usb_local_flush),
        .input_mute_o(local_switch_mute),
        .decoder_flush_o(decoder_flush),
        .event_count(event_count),
        .led(core_led),
        .pcm_valid(pcm_valid),.control_frame_tick_o(control_frame_tick),
        .fm_path_rst_n_o(fm_path_rst_n_o),
        .fm_route_o(fm_route_o),
        .fm_pcm_valid_o(fm_pcm_valid_o),
        .fm_diag_status_o(fm_diag_status),.fm_diag_requested_o(fm_diag_requested),
        .selected_left_o(selected_left),
        .selected_right_o(selected_right),
        .additive_left_o(additive_left),
        .additive_right_o(additive_right),
        .fm_pcm_left_o(fm_raw_left),
        .fm_pcm_right_o(fm_raw_right),
        .additive_selected_o(additive_selected),
        .additive_bypass_tilt_o(additive_bypass_tilt),
        .selected_fault_o(selected_fault),
        .output_filter_rst_n_o(output_filter_rst_n),
        .reverb_synth_rst_n_o(reverb_synth_rst_n_o),
        .reverb_fm_rst_n_o(reverb_fm_rst_n_o),
        .reverb_synth_mute_o(reverb_synth_mute_o),
        .reverb_fm_mute_o(reverb_fm_mute_o),
        .reverb_synth_bypass_o(reverb_synth_bypass_o),
        .cell_tick_o(cell_tick),
        .sample_tick48_o(sample_tick48),
        .cell_tick48_o(cell_tick48));
    // BEGIN_DISPLAY_KEY_CONTROL
    wire display_toggle;
    reg fft_selected_control;
    // KEY1 is now a display control. Preserve the prior power-up chord policy.
    assign legacy_chord_play_i = 1'b1;
    preset_chord_keys #(.DEBOUNCE_CYCLES(CONTROL_CLK_HZ/100)) u_chord_keys(
        .clk_i(control_clk),.rst_n_i(control_rst_n),.keys_n_i(ENABLE_AE350_CONTROL ? 2'b11 : Key_in),.frame_tick_i(control_frame_tick),
        .play_o(),.display_toggle_o(display_toggle),.engine_cycle_o(local_engine_cycle_i));
    always_ff @(posedge control_clk or negedge control_rst_n) begin
        if (!control_rst_n) fft_selected_control <= 1'b0;
        else if (display_toggle) fft_selected_control <= ~fft_selected_control;
    end
    // END_DISPLAY_KEY_CONTROL
    wire signed [23:0] scope_pcm_left,scope_pcm_right;
    wire scope_pcm_valid,scope_pcm_fm;
    assign scope_pcm_fm=pcm_serial_fm;
    pcm_request_audio #(
        .ENABLE_MIDI_PCM_LATENCY_TEST(ENABLE_MIDI_PCM_LATENCY_TEST || ENABLE_MIDI_NOTE20_TEST),
        .ENABLE_SUBTRACTIVE(ENABLE_SUBTRACTIVE || ENABLE_ADDITIVE || ENABLE_FLUTE),
        .ENABLE_KS(ENABLE_KS),.ENABLE_FM(ENABLE_FM),.ENABLE_REVERB(ENABLE_REVERB),
        .ENABLE_OUTPUT_TILT(ENABLE_OUTPUT_TILT),.OUTPUT_TILT_SHIFT(OUTPUT_TILT_SHIFT)
    ) u_audio_output (
        .global_rst_n_i(sys_rst_n && control_rst_n),.audio_clk_i(audio_clk),.audio_rst_n_i(audio_rst_n),
        .sub_clk_i(subtractive_clk),.sub_rst_n_i(subtractive_rst_n),
        .ks_clk_i(ks_native_clk),.ks_rst_n_i(control_rst_n && ks_clock_locked),
        .fm_clk_i(fm_native_clk),.fm_rst_n_i(fm_path_rst_n_o),
        .source_i(pcm_source),.mute_i(pcm_mute),.test_tone_i(pcm_test_tone),
        .bypass_tilt_i(pcm_source==3'd3 && additive_bypass_tilt),
        .wet_q15_i(reverb_wet_q15),.damp_q15_i(reverb_damp_q15),.revision_i(pcm_revision),
        .sub_request_valid_o(pcm_sub_request_valid),.sub_request_ready_i(pcm_sub_request_ready),
        .sub_request_tag_o(pcm_sub_request_tag),.sub_response_valid_i(pcm_sub_response_valid),
        .sub_response_tag_i(pcm_sub_response_tag),.sub_left_i(pcm_sub_l),.sub_right_i(pcm_sub_r),
        .ks_request_valid_o(pcm_ks_request_valid),.ks_request_ready_i(pcm_ks_request_ready),
        .ks_request_tag_o(pcm_ks_request_tag),.ks_response_valid_i(pcm_ks_response_valid),
        .ks_response_tag_i(pcm_ks_response_tag),.ks_left_i(pcm_ks_l),.ks_right_i(pcm_ks_r),
        .fm_request_valid_o(pcm_fm_request_valid),.fm_request_ready_i(pcm_fm_request_ready),
        .fm_request_tag_o(pcm_fm_request_tag),.fm_response_valid_i(pcm_fm_response_valid),
        .fm_response_tag_i(pcm_fm_response_tag),.fm_left_i(pcm_fm_l),.fm_right_i(pcm_fm_r),
        .hp_bck_o(HP_BCK),.hp_ws_o(HP_WS),.hp_data_o(HP_DIN),
        .pcm_bck_o(PCM_BCK),.pcm_ws_o(PCM_WS),.pcm_data_o(PCM_DIN),
        .tap_left_o(scope_pcm_left),.tap_right_o(scope_pcm_right),.tap_valid_o(scope_pcm_valid),
        .tap_source_o(pcm_serial_source),.tap_revision_o(pcm_serial_revision),.tap_tag_o(pcm_serial_tag),
        .latency_load_o(latency_serial_load),.latency_end_o(latency_wire_end),
        .latency_left_o(latency_serial_left),.latency_right_o(latency_serial_right),.latency_engine_o(latency_serial_engine),
        .latency_tag_o(latency_serial_tag),.latency_quality_o(latency_serial_quality),
        .frame_tick_o(pcm_output_frame_tick),.fm_active_o(dac_fm_active_i),.busy_o(dac_switch_busy_i),
        .synth_stats_o(pcm_synth_stats),.fm_stats_o(pcm_fm_stats),.audio_stats_o(pcm_audio_stats));
    // All probes are observational. The default branch has constant outputs
    // and no counter, MIDI parser, event mailbox or UART latency payload logic.
    generate if(ENABLE_MIDI_PCM_LATENCY_TEST)begin:g_midi_pcm_latency
        // Only diagnostic state uses these local release qualifiers. No
        // business reset or ready signal is driven from the measurement.
        wire sys_reset_n,raw_reset_n,usb_reset_n,cpu_reset_n;
        wire sub_reset_n,ks_reset_n,fm_reset_n,audio_reset_n;
        jt51_reset_sync u_sys_reset(.clk_i(sys_clk),.rst_n_i(sys_rst_n && owner_reset_n),.rst_n_o(sys_reset_n));
        jt51_reset_sync u_raw_reset(.clk_i(sclk),.rst_n_i(owner_reset_n),.rst_n_o(raw_reset_n));
        jt51_reset_sync u_usb_reset(.clk_i(ks_native_clk),.rst_n_i(latency_usb_rst_n && owner_reset_n),.rst_n_o(usb_reset_n));
        jt51_reset_sync u_cpu_reset(.clk_i(latency_cpu_clk_i),.rst_n_i(latency_cpu_rst_ni && owner_reset_n),.rst_n_o(cpu_reset_n));
        jt51_reset_sync u_sub_reset(.clk_i(subtractive_clk),.rst_n_i(subtractive_rst_n && owner_reset_n),.rst_n_o(sub_reset_n));
        jt51_reset_sync u_ks_reset(.clk_i(ks_native_clk),.rst_n_i(control_rst_n && ks_clock_locked && owner_reset_n),.rst_n_o(ks_reset_n));
        jt51_reset_sync u_fm_reset(.clk_i(fm_native_clk),.rst_n_i(fm_path_rst_n_o && owner_reset_n),.rst_n_o(fm_reset_n));
        jt51_reset_sync u_audio_reset(.clk_i(audio_clk),.rst_n_i(audio_rst_n && owner_reset_n),.rst_n_o(audio_reset_n));
        assign latency_session_no=sys_reset_n;
        midi_pcm_latency_probe u_probe(
            .clk_i(sys_clk),.rst_ni(sys_reset_n),
            .raw_clk_i(sclk),.raw_rst_ni(raw_reset_n),.raw_accept_i(latency_raw_accept),
            .raw_source_i(latency_raw_source),.raw_data_i(latency_raw_data),
            .usb_clk_i(ks_native_clk),.usb_rst_ni(usb_reset_n),
            .usb_accept_i(latency_usb_accept),.usb_packet_i(latency_usb_packet),
            .control_clk_i(control_clk),.control_rst_ni(owner_reset_n),
            .flush_i(ae350_flush_valid_o ? ae350_flush_mask_o[2:0] : 3'd0),.selected_engine_i(ae350_route_i),
            .cpu_clk_i(latency_cpu_clk_i),.cpu_rst_ni(cpu_reset_n),
            .cpu_submit_i(latency_cpu_submit_i),.cpu_engine_i(latency_cpu_engine_i),
            .cpu_ack_i(latency_cpu_ack_i),.cpu_ack_engine_i(latency_cpu_ack_engine_i),
            .native_clk_i({fm_native_clk,ks_native_clk,subtractive_clk}),
            .native_rst_ni({fm_reset_n,ks_reset_n,sub_reset_n}),
            .dsp_valid_i({pcm_fm_response_valid,pcm_ks_response_valid,pcm_sub_response_valid}),
            .dsp_left_i({pcm_fm_l,pcm_ks_l,pcm_sub_l}),.dsp_right_i({pcm_fm_r,pcm_ks_r,pcm_sub_r}),
            .audio_clk_i(audio_clk),.audio_rst_ni(audio_reset_n),.serial_load_i(latency_serial_load),
            .serial_left_i(latency_serial_left),.serial_right_i(latency_serial_right),
            .serial_engine_i(latency_serial_engine),.wire_end_i(latency_wire_end),
            .result_o(latency_result_o),.result_valid_o(latency_result_valid_o),.armed_o());
    end else begin:g_no_midi_pcm_latency
        assign latency_result_o=512'd0;assign latency_result_valid_o=1'b0;assign latency_session_no=1'b1;
    end endgenerate
    // The CPU owns the twenty records and all interval/serial formatting.
    // Hardware retains only one active four-point trial and a held result.
    generate if(ENABLE_MIDI_NOTE20_TEST)begin:g_cpu_timing
        wire capture_reset_n,audio_reset_n,cpu_reset_n;
        jt51_reset_sync u_capture_reset(.clk_i(control_clk),.rst_n_i(sys_rst_n && owner_reset_n),.rst_n_o(capture_reset_n));
        jt51_reset_sync u_audio_reset(.clk_i(audio_clk),.rst_n_i(audio_rst_n && owner_reset_n),.rst_n_o(audio_reset_n));
        jt51_reset_sync u_cpu_reset(.clk_i(latency_cpu_clk_i),.rst_n_i(latency_cpu_rst_ni && owner_reset_n),.rst_n_o(cpu_reset_n));
        ae350_latency_capture u_capture(
            .clk_i(control_clk),.rst_ni(capture_reset_n),
            .usb_accept_i(latency_usb_accept),.usb_packet_i(latency_usb_packet),
            .selected_engine_i(ae350_route_i),
            .flush_i(ae350_flush_valid_o ? ae350_flush_mask_o[2:0] : 3'd0),
            .other_midi_i(ae350_byte_valid_o && ae350_byte_ready_i &&
                ae350_byte_source_o!=2'd0 && ae350_byte_data_o<8'hf8),
            .cpu_clk_i(latency_cpu_clk_i),.cpu_rst_ni(cpu_reset_n),
            .cpu_submit_i(latency_note_submit_i),.cpu_engine_i(latency_cpu_engine_i),
            .cpu_submit_identity_i(latency_cpu_submit_identity_i),
            .cpu_ack_i(latency_cpu_ack_i),.cpu_ack_engine_i(latency_cpu_ack_engine_i),
            .cpu_ack_identity_i(latency_cpu_ack_identity_i),
            .audio_clk_i(audio_clk),.audio_rst_ni(audio_reset_n),.serial_load_i(latency_serial_load),
            .serial_left_i(latency_serial_left),.serial_right_i(latency_serial_right),
            .serial_engine_i(latency_serial_engine),.serial_quality_i(latency_serial_quality),
            .result_o(cpu_timing_result_o),.result_valid_o(cpu_timing_valid_o),
            .result_ready_i(cpu_timing_ready_i),.loss_o(cpu_timing_loss_o));
    end else begin:g_no_cpu_timing
        assign cpu_timing_result_o=576'd0;assign cpu_timing_valid_o=1'b0;assign cpu_timing_loss_o=1'b0;
    end endgenerate
    // Historical table interface is deliberately retired in the CPU flow.
    assign note20_session_no=1'b1;assign note20_read_data_o=1024'd0;
    assign note20_read_valid_o=1'b0;assign note20_done_o=1'b0;assign note20_loss_o=1'b0;
    assign note20_allocated_o=5'd0;assign note20_frozen_o=5'd0;
    // The optional display tap never feeds the audio transmitter.
    wire [47:0] display_pcm;
    wire display_pcm_valid;
    generate if(ENABLE_HDMI)begin:g_display_pcm
        held_bus_cdc #(.WIDTH(48)) u_snapshot(
            .src_clk_i(audio_clk),.dst_clk_i(subtractive_clk),.rst_n_i(audio_rst_n && subtractive_rst_n),
            .src_data_i({scope_pcm_left,scope_pcm_right}),.src_valid_i(scope_pcm_valid),.src_ready_o(),
            .dst_data_o(display_pcm),.dst_valid_o(display_pcm_valid),.dst_ready_i(1'b1));
    end else begin:g_no_display_pcm
        assign display_pcm=0;assign display_pcm_valid=0;
    end endgenerate
    generate if (ENABLE_HDMI) begin : g_hdmi
        scope_hdmi #(.ENABLE_FFT(ENABLE_FFT),.SAMPLE_RATE_NUM(91015625),.SAMPLE_RATE_DEN(948)) u_display (
            .sys_clk_i(sys_clk), .sys_rst_n_i(sys_rst_n),
            .core_clk_i(subtractive_clk), .audio_rst_n_i(subtractive_rst_n),
            .pcm_left_i($signed(display_pcm[47:24])), .pcm_right_i($signed(display_pcm[23:0])),
            .pcm_valid_i(display_pcm_valid), .fft_selected_i(fft_selected_control),
            .tmds_clk_p_o(tmds_clk_p), .tmds_clk_n_o(tmds_clk_n),
            .tmds_data_p_o(tmds_data_p), .tmds_data_n_o(tmds_data_n)
        );
    end else begin : g_no_hdmi
        // HDMI 关闭时仅保留静态差分缓冲，与 CST 的 LVCMOS33D 配对设置一致。
        // DAC 帧反馈仍由音频输出模块提供；显示缓存及视频 PLL 均不生成。
        ELVDS_OBUF u_idle_clock_buffer (
            .I(1'b0), .O(tmds_clk_p), .OB(tmds_clk_n)
        );
        for (genvar lane = 0; lane < 3; lane = lane + 1) begin : g_idle_data
            ELVDS_OBUF u_idle_data_buffer (
                .I(1'b0), .O(tmds_data_p[lane]), .OB(tmds_data_n[lane])
            );
        end
    end endgenerate

    assign dac_frame_tick_i=scope_pcm_valid;assign dac_frame_fm_i=scope_pcm_fm;
`ifdef SYNTH_FM_DIAG_LEDS
    wire parsed_fm_note = fm_diag_requested && !local_switch_mute &&
        midi_event_valid && midi_event.status[7:4] == 4'h9 &&
        midi_event.data2 != 0 && !decoder_flush[midi_event.source[1:0]];
    // Parsed notes live in control_clk; final serial taps now live in audio_clk.
    // Hold a per-selection milestone and transfer the complete observation.
    // This bridge only feeds diagnostics and never gates musical control.
    logic parsed_note_seen_control;
    always_ff @(posedge control_clk or negedge control_rst_n)
        if(!control_rst_n)parsed_note_seen_control<=0;
        else if(!fm_diag_requested)parsed_note_seen_control<=0;
        else if(parsed_fm_note)parsed_note_seen_control<=1;
    wire [2:0] diagnostic_control;
    held_bus_cdc #(.WIDTH(3)) u_fm_diag_control_cdc(
        .src_clk_i(control_clk),.dst_clk_i(audio_clk),
        .rst_n_i(sys_rst_n && control_rst_n && audio_rst_n),
        .src_data_i({fm_diag_requested,local_switch_mute,parsed_note_seen_control}),
        .src_valid_i(1'b1),.src_ready_o(),.dst_data_o(diagnostic_control),
        .dst_valid_o(),.dst_ready_i(1'b1));
    // Optional observation only. Normal audio never returns to a source clock.
    wire [47:0] diagnostic_post_pcm;
    wire diagnostic_post_valid;
    held_bus_cdc #(.WIDTH(48)) u_fm_diag_post_cdc(
        .src_clk_i(audio_clk),.dst_clk_i(fm_native_clk),
        .rst_n_i(sys_rst_n && audio_rst_n && fm_path_rst_n_o),
        .src_data_i({scope_pcm_left,scope_pcm_right}),
        .src_valid_i(scope_pcm_valid && scope_pcm_fm),.src_ready_o(),
        .dst_data_o(diagnostic_post_pcm),.dst_valid_o(diagnostic_post_valid),.dst_ready_i(1'b1));
    fm_diagnostic_leds #(.ACTIVE_LOW(FM_DIAG_LED_ACTIVE_LOW)) u_fm_diagnostic_leds (
        .ref_clk_i(sys_clk),.sys_clk_i(audio_clk),.fm_clk_i(fm_native_clk),
        .rst_n_i(sys_rst_n),.core_rst_n_i(audio_rst_n),
        .requested_i(diagnostic_control[2]),.parsed_note_i(diagnostic_control[0]),
        .local_mute_i(diagnostic_control[1]),
        .serial_nonzero_i(scope_pcm_valid && scope_pcm_fm &&
            ((scope_pcm_left[23:8] != 0) || (scope_pcm_right[23:8] != 0))),
        .fm_status_i(fm_diag_status),.post_valid_i(diagnostic_post_valid),
        .post_left_i($signed(diagnostic_post_pcm[47:24])),
        .post_right_i($signed(diagnostic_post_pcm[23:0])),.led_o(FM_DIAG_LED)
    );
`else
    // 保留内部接口；默认移除诊断寄存器、CDC 和灯逻辑，输出为不点亮电平。
    assign FM_DIAG_LED={8{FM_DIAG_LED_ACTIVE_LOW}};
`endif
    assign PA_EN=1'b0;

endmodule
`default_nettype wire
