// Copyright (c) 2026 ICECREAM1230
// Central control shares USB/KS48MHz in the production split-domain path.
// The original73.734MHz clock is explicitly subtractive_clk; drum, native
// runtime/CSP/render, PCM alignment and frame publication stay on that clock.
// Legacy module benches select CONTROL_CLOCK_SPLIT=0 for their one-clock path.
// Derived from Ethereal Cloud Tone Flex Synthesizer top.sv.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module synthesizer_manager #(

    // Board top explicitly enables FM. Direct legacy unit benches keep their
    // original DDS/KS boundary without requiring a second clock.
    // 生产路径：control_clk_i 为48MHz，subtractive_clk 保持原73.734MHz音频时基。
    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,
    parameter bit ENABLE_AE350_FDC = 1'b0,
    parameter integer FDC_WATCHDOG_CYCLES = 36_867_000,
    parameter bit CONTROL_CLOCK_SPLIT = 1'b0,
    parameter bit PCM_REQUEST_DRIVEN = 1'b0,
    // Historical module benches default to50MHz; board top explicitly sets48MHz.
    parameter integer CONTROL_CLK_HZ = 50000000,
    // CTL1兼容bench默认0750；主生产覆盖0790，避开SNS1/MOD1/PCM1。
    parameter logic [15:0] CONTROL_DIAG_BASE = 16'h0750,
    parameter bit ENABLE_FM = 1'b0,
    parameter bit ENABLE_MIDI = 1'b0,
    parameter bit ENABLE_MIDI_DEBUG = 1'b0,
    parameter integer MIDI_DEBUG_CLK_HZ = CONTROL_CLOCK_SPLIT ? CONTROL_CLK_HZ : 73734177,
    // Dedicated FM clock is the production path. Shared mode is retained
    // only for historical module comparisons, never selected by board top.
    parameter bit FM_SHARED_CLOCK = 1'b0,
    parameter bit FM_NATIVE96 = 1'b0,
    parameter bit FM_ISLAND_CLOCK = 1'b0,
    parameter bit KS_ISLAND_CLOCK = 1'b0,
    parameter bit LOCAL_CHORD_CONTROL = 1'b0,
    parameter bit AUTONOMOUS_USB_MIDI = 1'b0,
    parameter bit ENABLE_ADDITIVE = 1'b0,
    parameter bit ENABLE_FLUTE = 1'b0,
    parameter bit ENABLE_LOCAL_ENGINE_CONTROL = 1'b0,
    parameter integer LOCAL_ENGINE_TIMEOUT_CYCLES = CONTROL_CLOCK_SPLIT ? CONTROL_CLK_HZ/10 : 7373418,
    parameter bit SHARE_ADDITIVE_DDS = 1'b1,
    // 保留主工程的模块开关；禁用时不生成对应引擎，模式 mask 同步屏蔽。
    parameter bit ENABLE_KS_TIMBRES=1'b0,
    parameter bit ENABLE_KS_LIMITER = 1'b1,
    parameter bit ENABLE_KS = 1'b1,
    parameter bit ENABLE_SUBTRACTIVE = 1'b1

) (
    input wire owner_rst_ni,
    input wire ae350_cmd_valid_i,output wire ae350_cmd_ready_o,
    input wire [159:0] ae350_cmd_data_i,input wire [31:0] ae350_cmd_sequence_i,
    output wire [31:0] ae350_cmd_result_o,
    output wire ae350_fb_valid_o,input wire ae350_fb_ready_i,
    output wire [1:0] ae350_fb_engine_o,output wire [4:0] ae350_fb_voice_o,
    output wire [31:0] ae350_fb_generation_o,
    output wire [2:0] ae350_ready_o,output wire [31:0] ae350_fault_o,
    input wire [1:0] ae350_route_i,
    input wire subtractive_clk,rst,legacy_chord_play_i,local_engine_cycle_i,dac_switch_busy_i,dac_fm_active_i,
    input wire dac_frame_tick_i,dac_frame_fm_i,fm_native_clk,fm_rst_n,
    input wire ks_native_clk,ks_clock_locked_i,
    input wire control_clk_i,control_rst_n_i,subtractive_rst_n_i,
    input wire cfg_clk_i,cfg_write_i,
    input wire [15:0] cfg_addr_i,
    input wire [31:0] cfg_data_i,
    input wire cfg_read_req_toggle_i,
    input wire [15:0] cfg_read_addr_i,
    output wire [31:0] cfg_read_data_o,
    input wire usb_session_started_i,usb_flush_busy,raw_input_enabled,
    input wire [3:0] mailbox_flush,midi_source_reset,
    input wire [31:0] mailbox_read,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid,midi_event_pending,
    output wire midi_event_ready,usb_local_flush,input_mute_o,
    output wire [3:0] decoder_flush_o,
    output wire [31:0] event_count,
    output wire [5:0] led,
    output wire control_frame_tick_o,
    output wire pcm_valid,fm_path_rst_n_o,fm_route_o,fm_pcm_valid_o,
    output wire signed [23:0] selected_left_o,selected_right_o,additive_left_o,additive_right_o,fm_pcm_left_o,fm_pcm_right_o,
    output wire additive_selected_o,additive_bypass_tilt_o,selected_fault_o,output_filter_rst_n_o,
    output wire reverb_synth_rst_n_o,reverb_fm_rst_n_o,reverb_synth_mute_o,reverb_fm_mute_o,reverb_synth_bypass_o,
    output wire cell_tick_o,sample_tick48_o,cell_tick48_o,
    output wire [7:0] fm_diag_status_o,
    output wire fm_diag_requested_o,
    // Unified audio scheduler: engine-facing ports use each native clock.
    input wire pcm_sub_request_valid_i, input wire [63:0] pcm_sub_request_tag_i,
    output wire pcm_sub_request_ready_o, pcm_sub_response_valid_o,
    output wire [63:0] pcm_sub_response_tag_o,
    output wire signed [23:0] pcm_sub_left_o, pcm_sub_right_o,
    input wire pcm_ks_request_valid_i, input wire [63:0] pcm_ks_request_tag_i,
    output wire pcm_ks_request_ready_o, pcm_ks_response_valid_o,
    output wire [63:0] pcm_ks_response_tag_o,
    output wire signed [23:0] pcm_ks_left_o, pcm_ks_right_o,
    input wire pcm_fm_request_valid_i, input wire [63:0] pcm_fm_request_tag_i,
    output wire pcm_fm_request_ready_o, pcm_fm_response_valid_o,
    output wire [63:0] pcm_fm_response_tag_o,
    output wire signed [23:0] pcm_fm_left_o, pcm_fm_right_o,
    output wire [2:0] pcm_source_o,
    output wire pcm_mute_o, pcm_test_tone_o,
    output wire [31:0] pcm_revision_o,
    input wire pcm_output_clk_i, pcm_output_rst_n_i,
    input wire pcm_output_frame_tick_i, pcm_output_tap_valid_i,
    input wire [2:0] pcm_output_source_i,
    input wire [31:0] pcm_output_revision_i
);


    wire control_clk=CONTROL_CLOCK_SPLIT ? control_clk_i : subtractive_clk;
    wire rst_n=CONTROL_CLOCK_SPLIT ? control_rst_n_i : ~rst;
    wire audio_rst_n=CONTROL_CLOCK_SPLIT ? subtractive_rst_n_i : ~rst;
    (* ASYNC_REG="TRUE" *) logic [1:0] audio_alive_sync;
    logic audio_ever_ready,audio_fault_previous;
    wire audio_domain_fault=CONTROL_CLOCK_SPLIT && audio_ever_ready && !audio_alive_sync[1];
    wire audio_fault_edge=audio_domain_fault && !audio_fault_previous;
    always_ff @(posedge control_clk or negedge rst_n)begin
        if(!rst_n)begin audio_alive_sync<=0;audio_ever_ready<=0;audio_fault_previous<=0;end
        else begin
            audio_alive_sync<={audio_alive_sync[0],audio_rst_n};
            if(audio_alive_sync[1])audio_ever_ready<=1;
            audio_fault_previous<=audio_domain_fault;
        end
    end
    wire ae_owner_reset_n,ae_control_link_reset_n,ae_sub_link_reset_n;
    wire ae_owner_request_n=rst_n && (!ENABLE_AE350_VOICE_OWNER || owner_rst_ni);
    wire ae_link_reset_n=ae_owner_reset_n && audio_rst_n && (!ENABLE_FM || fm_rst_n) && (!ENABLE_KS || !KS_ISLAND_CLOCK || ks_clock_locked_i);
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_ae350_reset_release
        jt51_reset_sync u_owner_control(.clk_i(control_clk),.rst_n_i(ae_owner_request_n),.rst_n_o(ae_owner_reset_n));
        jt51_reset_sync u_link_control(.clk_i(control_clk),.rst_n_i(ae_link_reset_n),.rst_n_o(ae_control_link_reset_n));
        jt51_reset_sync u_link_sub(.clk_i(subtractive_clk),.rst_n_i(ae_link_reset_n),.rst_n_o(ae_sub_link_reset_n));
    end else begin:g_legacy_reset_release
        assign ae_owner_reset_n=rst_n;assign ae_control_link_reset_n=rst_n;assign ae_sub_link_reset_n=audio_rst_n;
    end endgenerate
    reg[1:0] ae_previous_route;reg[31:0] ae_route_revision;
    always @(posedge control_clk or negedge ae_owner_reset_n)begin
        if(!ae_owner_reset_n)begin ae_previous_route<=0;ae_route_revision<=1;end
        else if(ae_previous_route!=ae350_route_i)begin ae_previous_route<=ae350_route_i;ae_route_revision<=ae_route_revision+1'b1;end
    end
    wire ae_fm_fault;
    wire[2:0] ae_native_cmd_valid,ae_native_cmd_ready,ae_native_ready,ae_native_fb_valid,ae_native_fb_ready;
    wire[479:0] ae_native_cmd_data;wire[95:0] ae_native_cmd_sequence,ae_native_fb_generation;
    wire[5:0] ae_native_cmd_result;
    wire[14:0] ae_native_fb_voice;
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_ae350_backend
        wire[2:0] lane_valid,lane_ready,lane_fb_valid,lane_fb_ready;
        wire[159:0] lane_data;wire[31:0] lane_sequence;wire[14:0] lane_fb_voice;wire[95:0] lane_fb_generation;
        wire[5:0] lane_result;
        wire[31:0] router_fault;
        reg link_ever_ready;reg[31:0] link_fault;
        (* ASYNC_REG="TRUE" *)reg fm_fault_meta,fm_fault_sync;
        always @(posedge control_clk or negedge ae_owner_reset_n)begin
            if(!ae_owner_reset_n)begin link_ever_ready<=0;link_fault<=0;fm_fault_meta<=0;fm_fault_sync<=0;end
            else begin
                fm_fault_meta<=ae_fm_fault;fm_fault_sync<=fm_fault_meta;
                if((ready_sync & {ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE})=={ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE})link_ever_ready<=1;
                // Observe the existing control-clock reset qualifier, not
                // raw PLL/session levels in the wide sticky fault enables.
                // Its asynchronous assertion also retains a short loss until
                // control clocks return and the fault can be recorded.
                if(link_ever_ready && !ae_control_link_reset_n)link_fault<=32'h00000100;
                if(fm_fault_sync)link_fault<=32'h00000200;
            end
        end
        (* ASYNC_REG="TRUE" *)reg[2:0] ready_meta,ready_sync;
        always @(posedge control_clk or negedge ae_link_reset_n)
            if(!ae_link_reset_n)begin ready_meta<=0;ready_sync<=0;end
            else begin ready_meta<=ae_native_ready;ready_sync<=ready_meta;end
        ae350_voice_backend u_admission(.clk_i(control_clk),.rst_ni(ae_control_link_reset_n),
            .cmd_valid_i(ae350_cmd_valid_i),.cmd_ready_o(ae350_cmd_ready_o),.cmd_data_i(ae350_cmd_data_i),.cmd_sequence_i(ae350_cmd_sequence_i),.cmd_result_o(ae350_cmd_result_o),
            .native_ready_i(ready_sync & {3{link_fault==0}}),.ready_o(ae350_ready_o),.fault_o(router_fault),
            .lane_valid_o(lane_valid),.lane_ready_i(lane_ready),.lane_result_i(lane_result),.lane_data_o(lane_data),.lane_sequence_o(lane_sequence),
            .lane_fb_valid_i(lane_fb_valid),.lane_fb_ready_o(lane_fb_ready),.lane_fb_voice_i(lane_fb_voice),.lane_fb_generation_i(lane_fb_generation),
            .fb_valid_o(ae350_fb_valid_o),.fb_ready_i(ae350_fb_ready_i),.fb_engine_o(ae350_fb_engine_o),.fb_voice_o(ae350_fb_voice_o),.fb_generation_o(ae350_fb_generation_o));
        assign ae350_fault_o=router_fault | link_fault | engine_fault | (audio_domain_fault ? 32'h100 : 32'd0);
        for(genvar e=0;e<3;e=e+1)begin:g_lane
            wire native_clock=e==0 ? subtractive_clk : e==1 ? (KS_ISLAND_CLOCK ? ks_native_clk : control_clk) : (FM_ISLAND_CLOCK ? fm_native_clk : control_clk);
            lab_transaction_cdc #(.WIDTH(192),.RESPONSE_WIDTH(2)) u_command(.src_clk_i(control_clk),.dst_clk_i(native_clock),.rst_ni(ae_link_reset_n),
                .src_data_i({lane_sequence,lane_data}),.src_valid_i(lane_valid[e]),.src_ready_o(lane_ready[e]),.src_response_o(lane_result[e*2+:2]),
                .dst_data_o({ae_native_cmd_sequence[e*32+:32],ae_native_cmd_data[e*160+:160]}),.dst_valid_o(ae_native_cmd_valid[e]),.dst_ready_i(ae_native_cmd_ready[e]),.dst_response_i(ae_native_cmd_result[e*2+:2]));
            held_bus_cdc #(.WIDTH(37)) u_feedback(.src_clk_i(native_clock),.dst_clk_i(control_clk),.rst_n_i(ae_link_reset_n),
                .src_data_i({ae_native_fb_generation[e*32+:32],ae_native_fb_voice[e*5+:5]}),.src_valid_i(ae_native_fb_valid[e]),.src_ready_o(ae_native_fb_ready[e]),
                .dst_data_o({lane_fb_generation[e*32+:32],lane_fb_voice[e*5+:5]}),.dst_valid_o(lane_fb_valid[e]),.dst_ready_i(lane_fb_ready[e]));
        end
    end else begin:g_no_ae350_backend
        assign ae350_cmd_ready_o=0;assign ae350_cmd_result_o=0;assign ae350_fb_valid_o=0;assign ae350_fb_engine_o=0;assign ae350_fb_voice_o=0;assign ae350_fb_generation_o=0;assign ae350_ready_o=0;assign ae350_fault_o=0;
        assign ae_native_cmd_valid=0;assign ae_native_cmd_data=0;assign ae_native_cmd_sequence=0;assign ae_native_fb_ready=0;
    end endgenerate
    wire audio_control_busy,audio_control_valid;
    wire [31:0] audio_control_id,audio_state_sent,audio_state_applied,audio_frame_count;
    wire [31:0] output_frame_revision;
    wire [2:0] output_frame_route;
    wire [31:0] legacy_output_frame_revision, legacy_audio_frame_count;
    wire [2:0] legacy_output_frame_route;
    wire legacy_control_dac_busy,legacy_control_dac_fm,legacy_control_dac_tick,legacy_control_dac_frame_fm;
    wire legacy_control_frame_tick;
    wire [31:0] audio_source_revision;
    generate if(PCM_REQUEST_DRIVEN)begin:g_output_status
        pcm_output_status_bridge u_status(
            .audio_clk_i(pcm_output_clk_i),.audio_rst_n_i(pcm_output_rst_n_i),
            .control_clk_i(control_clk),.control_rst_n_i(rst_n),
            .frame_tick_i(pcm_output_frame_tick_i),.tap_valid_i(pcm_output_tap_valid_i),
            .busy_i(dac_switch_busy_i),.fm_active_i(dac_fm_active_i),
            .tap_source_i(pcm_output_source_i),.tap_revision_i(pcm_output_revision_i),
            .sample_tick_o(control_frame_tick_o),.played_tick_o(control_dac_tick),
            .busy_o(control_dac_busy),.fm_active_o(control_dac_fm),
            .played_fm_o(control_dac_frame_fm),.played_source_o(output_frame_route),
            .played_revision_o(output_frame_revision),.played_count_o(audio_frame_count));
    end else begin:g_legacy_output_status
        assign control_frame_tick_o=legacy_control_frame_tick;
        assign control_dac_tick=legacy_control_dac_tick;
        assign control_dac_busy=legacy_control_dac_busy;assign control_dac_fm=legacy_control_dac_fm;
        assign control_dac_frame_fm=legacy_control_dac_frame_fm;
        assign output_frame_route=legacy_output_frame_route;
        assign output_frame_revision=legacy_output_frame_revision;
        assign audio_frame_count=legacy_audio_frame_count;
    end endgenerate
    wire control_dac_busy,control_dac_fm,control_dac_tick,control_dac_frame_fm;
    wire [31:0] dds_template_status_control;
    wire [15:0] control_pass_count_control;
    wire bank_active_control,dmem_ack_control,test_tone_control;
    wire [2:0] audio_engine;
    wire audio_owner,audio_mute,audio_dds_render,audio_dds_csp,audio_legacy_chord;
    wire audio_fm_selected,audio_fm_mute,audio_fm_loss;
    wire dds_events_quiesced,fm_events_quiesced;
    wire midi_ingress_pending;
    wire dds_event_control_ready,fm_event_control_ready;
    wire dds_cdc_delivered,dds_cdc_drained;
    wire dds_native_enable,dds_native_valid,dds_native_capture_pending,dds_native_ready;
    wire [3:0] dds_native_flush;
    midi_pkg::midi_event_t dds_native_event;
    wire dds_transport_ready;
    wire control_audio_online=!CONTROL_CLOCK_SPLIT ||
        (audio_alive_sync[1] && audio_control_valid && !audio_control_busy);
    wire config_reset_n;
    generate if(CONTROL_CLOCK_SPLIT)begin:g_config_reset
        jt51_reset_sync u_reset(.clk_i(control_clk),.rst_n_i(rst_n && audio_rst_n),.rst_n_o(config_reset_n));
    end else begin:g_legacy_config_reset
        assign config_reset_n=rst_n;
    end endgenerate

    wire sclk=cfg_clk_i;
    wire [3:0] midi_backend_reset;

    // Transport and musical state have one fabric-clock owner. Legacy unit
    // benches leave ENABLE_MIDI=0 and retain their original control path.
    wire midi_write_enable,perf_write_enable;
    wire [7:0] midi_write_addr,midi_read_addr;
    wire [15:0] perf_write_addr;
    wire [31:0] midi_write_data,midi_read_data,perf_write_data;
    wire [31:0] control_diagnostic_data;
    // MIDI读回覆盖07xx；相对地址同时用于窗口选择和冻结数组索引。
    wire [15:0] control_diagnostic_offset={8'h07,midi_read_addr}-CONTROL_DIAG_BASE;
    wire control_diagnostic_selected=CONTROL_CLOCK_SPLIT && control_diagnostic_offset<16'd16;
    wire [31:0] dds_template_status,ks_template_status;
    wire additive_selected,additive_bypass_tilt;
    wire [31:0] additive_status;
    wire signed [23:0] additive_left,additive_right;
    wire additive_pcm_valid;
    wire performance_enabled;
    wire local_switch_busy,local_engine_owner,local_switch_mute,local_switch_flush;
    // Same-edge physical branches: runtime/ingress, renderer, CSP, KS, FM, ADD.
    wire [6:0] local_switch_mute_branches;
    wire [2:0] effective_engine;
    wire [4:0] effective_selection;
    wire [3:0] local_select_valid,local_select_bind,local_select_ready,local_select_done;
    wire [31:0] local_select_id,engine_status,engine_requested_revision,engine_completed_revision,engine_fault;
    wire [127:0] local_select_done_id,local_select_result;
    wire fm_event_link_reset;
    wire flute_selected,flute_midi_ready,flute_midi_enable,flute_midi_valid;
    wire flute_events_quiesced,flute_midi_delivered,flute_midi_drained;
    wire flute_stage_drained;
    wire [31:0] flute_register_data;
    wire signed [23:0] flute_pcm;
    wire flute_pcm_valid;
    midi_pkg::midi_event_t flute_midi_event;
    wire local_musical_permission=performance_enabled || local_engine_owner;
    wire effective_test_tone=test_tone_control && !local_engine_owner;
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_ae350_route
        assign local_select_valid=0;assign local_select_bind=0;assign local_select_id=0;
        assign local_switch_busy=0;assign local_engine_owner=1;assign local_switch_mute=0;assign local_switch_flush=0;
        assign local_switch_mute_branches=0;
        assign engine_status=0;assign engine_requested_revision=ae_route_revision;assign engine_completed_revision=ae_route_revision;assign engine_fault=0;
        assign effective_engine={1'b0,ae350_route_i};
        assign effective_selection=(5'b00001<<ae350_route_i) & {ENABLE_FLUTE,ENABLE_ADDITIVE,ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE};
    end else if(ENABLE_LOCAL_ENGINE_CONTROL)begin:g_local_engine
        logic remote_req_spi,remote_seen;
        (* ASYNC_REG="TRUE" *) logic [1:0] remote_sync,fm_alive_sync,ks_alive_sync;
        always_ff @(posedge sclk or negedge rst_n)
            if(!rst_n)remote_req_spi<=0;
            else if(midi_write_enable && midi_write_addr==8'h10 && midi_write_data==32'h52454d54)
                remote_req_spi<=~remote_req_spi;
        always_ff @(posedge control_clk or negedge rst_n)begin
            if(!rst_n)begin remote_sync<=0;remote_seen<=0;fm_alive_sync<=0;ks_alive_sync<=0;end
            else begin
                remote_sync<={remote_sync[0],remote_req_spi};remote_seen<=remote_sync[1];
                fm_alive_sync<={fm_alive_sync[0],fm_rst_n};
                ks_alive_sync<={ks_alive_sync[0],ks_clock_locked_i};
            end
        end
        engine_switch_control #(.ENGINE_COUNT(5),.ENGINE_MASK({ENABLE_FLUTE,ENABLE_ADDITIVE,ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE}),
            .TIMEOUT_CYCLES(LOCAL_ENGINE_TIMEOUT_CYCLES),.KS_CLOCK_GUARD(KS_ISLAND_CLOCK && ENABLE_KS),.MUTE_BRANCH_COUNT(7),.WAIT_CANCEL_ACK(CONTROL_CLOCK_SPLIT)) u_switch (
            .clk_i(control_clk),.rst_n_i(rst_n),.cycle_i(local_engine_cycle_i),
            .remote_release_i(remote_sync[1]!=remote_seen),
            .selected_i({flute_selected,additive_selected,fm_binding,ks_mode}),
            .fm_clock_alive_i(!ENABLE_FM || fm_alive_sync[1]),
            .ks_clock_alive_i(!ENABLE_KS || !KS_ISLAND_CLOCK || ks_alive_sync[1]),.usb_flush_busy_i(usb_flush_busy),
            .dac_busy_i(control_dac_busy || (CONTROL_CLOCK_SPLIT && !audio_control_valid)),.dac_fm_active_i(control_dac_fm),
            .dac_frame_tick_i(control_dac_tick),.dac_frame_fm_i(control_dac_frame_fm),
            .cancel_ready_i(!midi_ingress_pending && dds_events_quiesced && (fm_events_quiesced || fm_event_link_reset) && flute_events_quiesced && !audio_control_busy),
            .fm_reset_cancelled_i(fm_event_link_reset),
            .output_state_applied_i(!audio_control_busy && output_frame_revision==engine_requested_revision && output_frame_route==effective_engine),
            .domain_fault_i(audio_domain_fault),
            .select_valid_o(local_select_valid),.select_bind_o(local_select_bind),.select_id_o(local_select_id),
            .select_ready_i(local_select_ready),.select_done_i(local_select_done),
            .select_done_id_i(local_select_done_id),.select_result_i(local_select_result),
            .effective_engine_o(effective_engine),.effective_selection_o(effective_selection),.busy_o(local_switch_busy),.owner_o(local_engine_owner),
            .mute_o(local_switch_mute),.mute_branches_o(local_switch_mute_branches),.flush_o(local_switch_flush),.status_o(engine_status),
            .requested_revision_o(engine_requested_revision),.completed_revision_o(engine_completed_revision),
            .fault_o(engine_fault));
    end else begin:g_no_local_engine
        assign local_select_valid=0;assign local_select_bind=0;assign local_select_id=0;
        assign local_switch_busy=0;assign local_engine_owner=0;assign local_switch_mute=0;assign local_switch_flush=0;
        assign local_switch_mute_branches=0;
        assign engine_status=0;assign engine_requested_revision=0;assign engine_completed_revision=0;assign engine_fault=0;
        assign effective_engine=flute_selected?3'd4:additive_selected?3'd3:fm_binding?3'd2:ks_mode?3'd1:3'd0;
        assign effective_selection=(5'b00001<<effective_engine) &
                                   {ENABLE_FLUTE,ENABLE_ADDITIVE,ENABLE_FM,ENABLE_KS,ENABLE_SUBTRACTIVE};
    end endgenerate
    logic local_performance_active;
    // USB has its own boot permission. Once a keyboard begins a session,
    // unplugging it leaves the performance engine silent/ready instead of
    // unexpectedly restarting the legacy demonstration chord.
    always_ff @(posedge control_clk or negedge rst_n)
        if(!rst_n)local_performance_active<=1'b0;
        else if(ENABLE_MIDI && AUTONOMOUS_USB_MIDI && usb_session_started_i)
            local_performance_active<=1'b1;
    assign performance_enabled=ENABLE_AE350_VOICE_OWNER || raw_input_enabled || local_performance_active;
    midi_pkg::midi_event_t dds_midi_event,ks_midi_event,fm_midi_event;
    midi_pkg::midi_event_t add_midi_event;
    midi_pkg::midi_sysex_t unused_midi_sysex;
    wire dds_midi_ready,ks_midi_ready,fm_midi_ready;
    wire dds_midi_enable,ks_midi_enable,fm_midi_enable;
    wire dds_render_midi_enable,dds_csp_midi_enable;
    wire dds_midi_valid,ks_midi_valid,fm_midi_valid;
    wire add_midi_ready,add_midi_enable,add_midi_valid;
    wire dds_midi_capture_pending;
    wire ks_midi_drop,fm_midi_drop;
    wire fm_cdc_delivered, fm_cdc_drained;
    wire fm_mode_cancel;
    midi_pkg::midi_event_t fm_return_event;
    logic [3:0] midi_consumer_flush;
    wire [3:0] midi_submit_fence=midi_backend_reset | midi_consumer_flush;
    generate if(ENABLE_MIDI) begin : g_midi
        // Drain/discard complete USB packets and RAW bytes during a local
        // transition. Backpressuring them would replay a held NoteOn after
        // unmute. The initial mode fence clears already parsed events.
        // Disabled RAW parsers stay reset, but musical consumers receive
        // only the actual cancellation transaction. Holding 1110 at those
        // consumers would continuously stall the unrelated USB allocator.
        assign midi_backend_reset=AUTONOMOUS_USB_MIDI ?
            (midi_source_reset & {{3{raw_input_enabled}},1'b1}) | mailbox_flush : midi_source_reset;
        wire [31:0] unused_fault_count,unused_padding_count;
        wire unused_sysex_valid;
        reg [2:0] previous_engine;
        wire [2:0] current_engine=effective_engine;
        // Register the comparison too: its common no-flush qualification has
        // high fanout into all three owners. The ingress slots keep an old
        // hub event from reaching a newly selected owner before this pulse
        // cancels it. Immediate engine disable still cancels the old owner.
        (* syn_preserve = 1 *) reg mode_changed_q;
        wire mode_changed_now=current_engine!=previous_engine;
        wire mode_boundary=mode_changed_q || local_switch_flush || audio_fault_edge;
        assign fm_mode_cancel=mode_boundary;
        // Only admission uses the immediate comparison. In particular a
        // disabled/template-invalid new engine must not drain/count an old
        // hub event in the edge before the registered cancellation arrives.
        midi_pkg::midi_event_t ingress_event;
        wire ingress_valid,ingress_ready;
        wire [2:0] ingress_route;
        if(CONTROL_CLOCK_SPLIT)begin:g_registered_ingress
            // Native/control readiness is consumed locally. The decoder sees
            // only this receiver-owned ready FF, never a combinational trip
            // through FM admission or an audio configuration comparison.
            midi_event_ingress #(.ROUTE_WIDTH(3)) u_boundary(
                .clk_i(control_clk),.rst_n_i(rst_n),
                .cancel_all_i(mode_boundary || mode_changed_now),
                .block_i(local_switch_mute || audio_domain_fault),
                .source_flush_i(midi_submit_fence | mailbox_flush),
                .route_i(effective_engine),.in_event_i(midi_event),
                .in_valid_i(midi_event_valid),.in_ready_o(midi_event_ready),
                .out_event_o(ingress_event),.out_route_o(ingress_route),
                .out_valid_o(ingress_valid),.pending_o(midi_ingress_pending),
                .out_ready_i(ingress_ready),.drained_o());
        end else begin:g_legacy_ingress
            assign ingress_event=midi_event;
            assign ingress_route=effective_engine;
            assign ingress_valid=midi_event_valid;
            assign midi_ingress_pending=1'b0;
            assign midi_event_ready=ingress_ready;
        end
        wire raw_source_flushed=midi_source_reset[ingress_event.source[1:0]];
        wire stage_event_valid=ingress_valid && ingress_route==effective_engine &&
            !mode_changed_now && !raw_source_flushed && !local_switch_mute && !audio_domain_fault &&
            control_audio_online;
        always @(posedge control_clk or negedge rst_n)
            if(!rst_n)begin previous_engine<=0;mode_changed_q<=1'b0;midi_consumer_flush<=0;end
            else begin
                previous_engine<=current_engine;
                mode_changed_q<=mode_changed_now;
                // Complete cancellation mask travels across the same local
                // boundary as event validity. Raw cancellation remains only
                // at admission, accounting and final backend submission.
                midi_consumer_flush<=midi_backend_reset | {4{local_switch_flush}};
            end
        assign usb_local_flush=mailbox_flush[0] || mode_boundary;
        assign decoder_flush_o=mailbox_flush | {4{mode_boundary}};
        
        assign midi_read_data=midi_read_addr[7:4]==4'hb ? flute_register_data :
                              control_diagnostic_selected ? control_diagnostic_data :
                              midi_read_addr==8'h07 ? dds_template_status_control :
                              midi_read_addr==8'h08 ? ks_template_status :
                              midi_read_addr==8'h09 ? additive_status :
                              ENABLE_LOCAL_ENGINE_CONTROL && midi_read_addr==8'h0b ? 32'h454e4731 :
                              ENABLE_LOCAL_ENGINE_CONTROL && midi_read_addr==8'h0c ? engine_status :
                              ENABLE_LOCAL_ENGINE_CONTROL && midi_read_addr==8'h0d ? engine_requested_revision :
                              ENABLE_LOCAL_ENGINE_CONTROL && midi_read_addr==8'h0e ? engine_completed_revision :
                              ENABLE_LOCAL_ENGINE_CONTROL && midi_read_addr==8'h0f ? engine_fault : mailbox_read;
        
        // A local event register at each engine cuts the shared payload/ready
        // route. No lookahead: an occupied slot cannot capture its successor
        // on the same edge it is consumed. Registered source cancellation is
        // aligned with owner state; raw cancellation still fences submission.
        wire dds_input_ready,ks_input_ready,fm_input_ready,dds_slot_empty;
        wire add_input_ready,flute_input_ready;
        wire [4:0] delivered,drained,counted_delivery;
        wire dds_stage_delivered,dds_stage_drained;
        midi_event_stage #(.REGISTERED_CONTROLS(1'b1)) dds_stage (
            .clk(control_clk),.rst_n,.enable(dds_midi_enable),.cancel_all(mode_boundary),
            .drop_incoming(1'b0),.source_flush(midi_consumer_flush),
            .in_event(ingress_event),.in_valid(stage_event_valid),.in_ready(dds_input_ready),.slot_empty(dds_slot_empty),
            .out_event(dds_midi_event),.out_valid(dds_midi_valid),.out_ready(dds_midi_ready),
            .delivered(dds_stage_delivered),.drained(dds_stage_drained)
        );
        assign delivered[0]=CONTROL_CLOCK_SPLIT ? dds_cdc_delivered : dds_stage_delivered;
        assign drained[0]=CONTROL_CLOCK_SPLIT ? dds_cdc_drained : dds_stage_drained;
        midi_event_stage #(.REGISTERED_CONTROLS(1'b1)) ks_stage (
            .clk(control_clk),.rst_n,.enable(ks_midi_enable),.cancel_all(mode_boundary),
            .drop_incoming(ks_midi_drop),.source_flush(midi_consumer_flush),
            .in_event(ingress_event),.in_valid(stage_event_valid),.in_ready(ks_input_ready),.slot_empty(),
            .out_event(ks_midi_event),.out_valid(ks_midi_valid),.out_ready(ks_midi_ready),
            .delivered(delivered[1]),.drained(drained[1])
        );
        wire fm_stage_delivered, fm_stage_drained;
        midi_event_stage #(.REGISTERED_CONTROLS(1'b1)) fm_stage (
            .clk(control_clk),.rst_n,.enable(fm_midi_enable),.cancel_all(mode_boundary),
            // Native CDC classifies drop after applying enable/epoch. A source
            // snapshot can still describe the preceding disabled generation.
            .drop_incoming(CONTROL_CLOCK_SPLIT ? 1'b0 : fm_midi_drop),.source_flush(midi_consumer_flush),
            .in_event(ingress_event),.in_valid(stage_event_valid),.in_ready(fm_input_ready),.slot_empty(),
            .out_event(fm_midi_event),.out_valid(fm_midi_valid),.out_ready(fm_midi_ready),
            .delivered(fm_stage_delivered),.drained(fm_stage_drained)
        );
        assign delivered[2] = FM_ISLAND_CLOCK ? fm_cdc_delivered : fm_stage_delivered;
        assign drained[2] = FM_ISLAND_CLOCK ? fm_cdc_drained : fm_stage_drained;
        if(ENABLE_ADDITIVE)begin:g_additive_stage
            midi_event_stage #(.REGISTERED_CONTROLS(1'b1)) add_stage (
                .clk(control_clk),.rst_n,.enable(add_midi_enable),.cancel_all(mode_boundary),
                .drop_incoming(1'b0),.source_flush(midi_consumer_flush),
                .in_event(ingress_event),.in_valid(stage_event_valid),.in_ready(add_input_ready),.slot_empty(),
                .out_event(add_midi_event),.out_valid(add_midi_valid),.out_ready(add_midi_ready),
                .delivered(delivered[3]),.drained(drained[3])
            );
        end else begin:g_no_additive_stage
            assign add_input_ready=1'b0;
            assign add_midi_event='0;assign add_midi_valid=1'b0;
            assign delivered[3]=1'b0;assign drained[3]=1'b0;
        end
        if(ENABLE_FLUTE)begin:g_flute_stage
            wire stage_delivered,stage_drained;
            midi_event_stage #(.REGISTERED_CONTROLS(1'b1)) flute_stage (
                .clk(control_clk),.rst_n,.enable(flute_midi_enable),.cancel_all(mode_boundary),
                .drop_incoming(1'b0),.source_flush(midi_consumer_flush),
                .in_event(ingress_event),.in_valid(stage_event_valid),.in_ready(flute_input_ready),.slot_empty(),
                .out_event(flute_midi_event),.out_valid(flute_midi_valid),.out_ready(flute_midi_ready),
                .delivered(stage_delivered),.drained(stage_drained));
            assign delivered[4]=flute_midi_delivered;
            assign drained[4]=flute_midi_drained;
            assign flute_stage_drained=stage_drained;
        end else begin:g_no_flute_stage
            assign flute_input_ready=0;assign flute_midi_event='0;assign flute_midi_valid=0;
            assign delivered[4]=0;assign drained[4]=0;
            assign flute_stage_drained=0;
        end
        // These event slots are complete local buffers. In the production
        // path they may capture before their CDC is ready, then hold until
        // its existing epoch/control/application handshake permits transfer.
        assign ingress_ready=rst_n && !mode_changed_now && !raw_source_flushed && !local_switch_mute && !audio_domain_fault &&
            control_audio_online &&
            (|(effective_selection & {flute_input_ready,add_input_ready,fm_input_ready,ks_input_ready,dds_input_ready}));
        // An empty-slot guard from the hub's holding FF, independent of raw
        // reset/source cancellation and owner ready. It may conservatively
        // pause during canceled admission; only actual slot handshakes count.
        assign dds_midi_capture_pending=(midi_event_pending || midi_ingress_pending) && dds_midi_enable && dds_slot_empty;
        // Mailbox status counts engine acceptance (or its original clear/drain
        // behavior), never an intermediate slot capture or a canceled slot.
        reg [31:0] accepted_event_count;
        assign counted_delivery[0]=CONTROL_CLOCK_SPLIT ? dds_cdc_delivered : delivered[0] && !midi_source_reset[dds_midi_event.source[1:0]];
        assign counted_delivery[1]=delivered[1] && !midi_source_reset[ks_midi_event.source[1:0]];
        // A returned CDC ACK classifies an earlier destination edge. A new
        // source flush must not erase that already completed acceptance.
        assign counted_delivery[2]=FM_ISLAND_CLOCK ? fm_cdc_delivered :
            delivered[2] && !midi_source_reset[fm_midi_event.source[1:0]];
        assign counted_delivery[3]=delivered[3] && !midi_source_reset[add_midi_event.source[1:0]];
        assign counted_delivery[4]=delivered[4]; // native CDC ACK already classifies its source generation
        // Capture final acceptance flags before reducing them to an increment.
        // Statistics publish two clocks after the real delivery or drain edge;
        // the real engine ACK, cancellation and their timing stay unchanged.
        // The local capture removes the shared qualification + popcount chain
        // from one clock period and preserves simultaneous late FM ACKs.
        reg [12:0] acceptance_sample /* synthesis syn_dont_touch = 1 */;
        reg [3:0] acceptance_increment;
        always @(posedge control_clk or negedge rst_n)
            if(!rst_n)begin accepted_event_count<=0;acceptance_sample<=0;acceptance_increment<=0;end
            else begin
                // A late FM acknowledgement can coincide with admission to
                // another engine, or a local flush drain. Count both events.
                acceptance_sample<={counted_delivery,drained,flute_stage_drained,(FM_ISLAND_CLOCK && fm_stage_drained),(CONTROL_CLOCK_SPLIT && dds_stage_drained)};
                acceptance_increment<=4'($countones(acceptance_sample));
                if(acceptance_increment!=0)accepted_event_count<=accepted_event_count+32'(acceptance_increment);
            end
        assign event_count=accepted_event_count;
`ifndef SYNTHESIS
        wire [9:0] acceptance_mask={counted_delivery,drained};
        always @(posedge control_clk) if(rst_n && !FM_ISLAND_CLOCK && !CONTROL_CLOCK_SPLIT)
            assert($onehot0(acceptance_mask)) else $fatal(1,"multiple MIDI engine acceptances delivered=%b drained=%b",delivered,drained);
`endif
    end else begin : g_legacy_control
        assign fm_mode_cancel=1'b0;
        assign usb_local_flush=0;
        assign midi_read_data=0;
        assign unused_midi_sysex='0;
        assign midi_event_ready=0;
        assign midi_ingress_pending=0;
        assign dds_midi_event='0;
        assign ks_midi_event='0;
        assign fm_midi_event='0;
        assign add_midi_event='0;
        assign flute_midi_event='0;
        assign dds_midi_valid=0;
        assign ks_midi_valid=0;
        assign fm_midi_valid=0;
        assign add_midi_valid=0;
        assign flute_midi_valid=0;
        assign flute_stage_drained=0;
        assign dds_midi_capture_pending=0;
        assign midi_backend_reset=0;
        assign midi_consumer_flush=0;
        assign decoder_flush_o=0;
        assign event_count=0;
    end endgenerate

    // CTL1从CONTROL_DIAG_BASE起占16字；读基址冻结控制/传输/输出证据。
    // 该窗口只作观察，不驱动音频或音乐状态。
    generate if(CONTROL_CLOCK_SPLIT)begin:g_control_diagnostics
        logic [31:0] delivered_sub,drained_sub,delivered_fm,drained_fm,generation;
        logic [31:0] frozen[0:15];
        (* ASYNC_REG="TRUE" *)logic request_meta;
        logic request_sync,request_previous;
        always_ff @(posedge control_clk or negedge rst_n)begin
            if(!rst_n)begin
                delivered_sub<=0;drained_sub<=0;delivered_fm<=0;drained_fm<=0;generation<=0;
                request_meta<=0;request_sync<=0;request_previous<=0;
                for(integer k=0;k<16;k=k+1)frozen[k]<=0;
            end else begin
                request_meta<=cfg_read_req_toggle_i;request_sync<=request_meta;request_previous<=request_sync;
                if(dds_cdc_delivered)delivered_sub<=delivered_sub+32'd1;
                if(dds_cdc_drained)drained_sub<=drained_sub+32'd1;
                if(fm_cdc_delivered)delivered_fm<=delivered_fm+32'd1;
                if(fm_cdc_drained)drained_fm<=drained_fm+32'd1;
                if(request_sync!=request_previous && cfg_read_addr_i==CONTROL_DIAG_BASE)begin
                    generation<=generation+32'd1;
                    frozen[0]<=32'h43544c31;frozen[1]<=32'(CONTROL_CLK_HZ);frozen[2]<=generation+32'd1;
                    frozen[3]<={24'd0,local_switch_mute,(effective_engine==2),audio_alive_sync[1],audio_domain_fault,
                        fm_events_quiesced,dds_events_quiesced,audio_control_valid,audio_control_busy};
                    frozen[4]<=audio_state_sent;frozen[5]<=audio_state_applied;frozen[6]<=audio_control_id;
                    frozen[7]<=audio_frame_count;frozen[8]<=delivered_sub;frozen[9]<=drained_sub;
                    frozen[10]<=delivered_fm;frozen[11]<=drained_fm;frozen[12]<=event_count;
                    frozen[13]<=engine_requested_revision;frozen[14]<=engine_completed_revision;frozen[15]<=engine_fault;
                end
            end
        end
        assign control_diagnostic_data=control_diagnostic_offset[3:0]==0 ? 32'h43544c31 : frozen[control_diagnostic_offset[3:0]];
    end else begin:g_no_control_diagnostics
        assign control_diagnostic_data=0;
    end endgenerate
    assign dds_midi_enable=ENABLE_SUBTRACTIVE && performance_enabled && !audio_domain_fault && !local_switch_mute_branches[0] && (ENABLE_AE350_VOICE_OWNER || effective_selection[0]);
    assign dds_render_midi_enable=ENABLE_SUBTRACTIVE && performance_enabled && !audio_domain_fault && !local_switch_mute_branches[1] && (ENABLE_AE350_VOICE_OWNER || effective_selection[0]);
    assign dds_csp_midi_enable=ENABLE_SUBTRACTIVE && performance_enabled && !audio_domain_fault && !local_switch_mute_branches[2] && (ENABLE_AE350_VOICE_OWNER || effective_selection[0]);
    assign ks_midi_enable=ENABLE_KS && local_musical_permission && !audio_domain_fault && !local_switch_mute_branches[3] && effective_selection[1];
    assign fm_midi_enable=local_musical_permission && !audio_domain_fault && !local_switch_mute_branches[4] && effective_selection[2];
    assign add_midi_enable=local_musical_permission && !audio_domain_fault && !local_switch_mute_branches[5] && effective_selection[3];
    assign flute_midi_enable=ENABLE_FLUTE && local_musical_permission && !audio_domain_fault && !local_switch_mute_branches[6] && effective_selection[4];

    generate if(CONTROL_CLOCK_SPLIT)begin:g_control_domain
        wire [47:0] control_state={5'd0,effective_engine[2],engine_requested_revision,local_engine_owner,effective_engine[1:0],
            (local_switch_mute || audio_domain_fault),dds_render_midi_enable,dds_csp_midi_enable,
            legacy_chord_play_i,fm_selected,fm_mute,fm_loss_mute};
        wire [47:0] audio_state;
        control_audio_bridge u_audio_control(
            .control_clk_i(control_clk),.control_rst_n_i(rst_n),
            .subtractive_clk_i(subtractive_clk),.subtractive_rst_n_i(audio_rst_n),
            .state_i(control_state),.state_o(audio_state),.state_busy_o(audio_control_busy),
            .state_applied_valid_o(audio_control_valid),.state_applied_id_o(audio_control_id),
            .state_sent_o(audio_state_sent),.state_applied_o(audio_state_applied),
            .sample_tick_i(sample_tick),.dac_frame_tick_i(PCM_REQUEST_DRIVEN ? 1'b0 : dac_frame_tick_i),.dac_frame_fm_i(dac_frame_fm_i),
            .dac_busy_i(dac_switch_busy_i),.dac_fm_active_i(dac_fm_active_i),
            .template_status_i(dds_template_status),.control_pass_count_i(control_pass_count),
            .bank_active_i(bank_active_status),.dmem_ack_i(dmem_wr_ack),.test_tone_i(test_tone_en),
            .template_status_o(dds_template_status_control),.control_pass_count_o(control_pass_count_control),
            .bank_active_o(bank_active_control),.dmem_ack_o(dmem_ack_control),.test_tone_o(test_tone_control),
            .dac_busy_o(legacy_control_dac_busy),.dac_fm_active_o(legacy_control_dac_fm),
            .sample_tick_o(legacy_control_frame_tick),.dac_frame_tick_o(legacy_control_dac_tick),
            .dac_frame_fm_o(legacy_control_dac_frame_fm),.dac_frame_route_o(legacy_output_frame_route),
            .dac_frame_revision_o(legacy_output_frame_revision),.frame_count_o(legacy_audio_frame_count));
        assign audio_source_revision=audio_state[41:10];
        assign audio_engine={audio_state[42],audio_state[8:7]};
        assign {audio_owner,audio_mute,audio_dds_render,audio_dds_csp,audio_legacy_chord,
            audio_fm_selected,audio_fm_mute,audio_fm_loss}={audio_state[9],audio_state[6:0]};
        if(ENABLE_AE350_VOICE_OWNER)begin:g_prepared_no_midi_events
            // Prepared execution has no second MIDI event owner. Audio flags
            // still arrive through u_audio_control at the original boundary.
            assign dds_native_enable=1'b1;assign dds_native_event='0;
            assign dds_native_valid=1'b0;assign dds_native_flush=4'd0;
            assign dds_native_capture_pending=1'b0;
            assign dds_transport_ready=1'b1;assign dds_midi_ready=1'b1;
            assign dds_cdc_delivered=1'b0;assign dds_cdc_drained=1'b0;
            assign dds_events_quiesced=1'b1;assign dds_event_control_ready=1'b1;
        end else begin:g_legacy_midi_events
        wire unused_pending;
        midi_event_cdc u_subtractive_events(
            .src_clk_i(control_clk),.dst_clk_i(subtractive_clk),.rst_n_i(rst_n && audio_rst_n),
            .src_enable_i(dds_midi_enable),.src_cancel_i(fm_mode_cancel),.src_flush_i(midi_submit_fence),
            .src_event_i(dds_midi_event),.src_valid_i(dds_midi_valid && !audio_control_busy),
            .src_ready_o(dds_transport_ready),.src_delivered_o(dds_cdc_delivered),.src_drained_o(dds_cdc_drained),
            .src_return_event_o(),.src_quiesced_o(dds_events_quiesced),.src_control_ready_o(dds_event_control_ready),
            .dst_enable_o(dds_native_enable),.dst_event_o(dds_native_event),.dst_valid_o(dds_native_valid),
            .dst_ready_i(dds_native_ready),.dst_drop_i(!audio_dds_render),
            .dst_flush_o(dds_native_flush),.dst_pending_o(unused_pending),
            .dst_capture_pending_o(dds_native_capture_pending));
        assign dds_midi_ready=dds_transport_ready && !audio_control_busy;
        end
    end else begin:g_same_control_clock
        assign audio_control_busy=0;assign audio_control_valid=1;assign audio_control_id=0;
        assign audio_state_sent=0;assign audio_state_applied=0;assign legacy_audio_frame_count=0;
        assign legacy_output_frame_revision=engine_requested_revision;assign legacy_output_frame_route=effective_engine;
        assign audio_source_revision=engine_requested_revision;
        assign legacy_control_dac_busy=dac_switch_busy_i;assign legacy_control_dac_fm=dac_fm_active_i;
        assign legacy_control_dac_tick=dac_frame_tick_i;assign legacy_control_dac_frame_fm=dac_frame_fm_i;
        assign legacy_control_frame_tick=sample_tick;
        assign dds_template_status_control=dds_template_status;assign control_pass_count_control=control_pass_count;
        assign bank_active_control=bank_active_status;assign dmem_ack_control=dmem_wr_ack;assign test_tone_control=test_tone_en;
        assign {audio_owner,audio_engine,audio_mute,audio_dds_render,audio_dds_csp,audio_legacy_chord,
            audio_fm_selected,audio_fm_mute,audio_fm_loss}={local_engine_owner,effective_engine,local_switch_mute,
            dds_render_midi_enable,dds_csp_midi_enable,legacy_chord_play_i,fm_selected,fm_mute,fm_loss_mute};
        assign dds_native_enable=dds_midi_enable;assign dds_native_event=dds_midi_event;
        assign dds_native_valid=dds_midi_valid;assign dds_midi_ready=dds_native_ready;
        assign dds_native_capture_pending=dds_midi_capture_pending;assign dds_native_flush=midi_consumer_flush;
        assign dds_events_quiesced=1;assign dds_cdc_delivered=0;assign dds_cdc_drained=0;
        assign dds_event_control_ready=1;
        assign dds_transport_ready=0;
    end endgenerate

    //----------------------------------------------------------------
    // Drum — the sole timebase
    //----------------------------------------------------------------
    logic       sample_tick, lane_enter, cell_tick;
    logic       sample_tick48, cell_tick48;
    logic [9:0] slot;

    wire legacy_sample_tick,legacy_lane_enter,legacy_cell_tick,legacy_sample_tick48,legacy_cell_tick48;
    wire [9:0] legacy_slot, request_slot;
    wire request_sample_tick,request_lane_enter;
    drum u_drum (
        .clk           (subtractive_clk),
        .rst_n         (audio_rst_n && !PCM_REQUEST_DRIVEN),
        .sample_tick   (legacy_sample_tick),
        .lane_enter    (legacy_lane_enter),
        .cell_tick     (legacy_cell_tick),
        .sample_tick48 (legacy_sample_tick48),
        .cell_tick48   (legacy_cell_tick48),
        .slot          (legacy_slot)
    );

    generate if(PCM_REQUEST_DRIVEN)begin:g_requested_schedule
        if(ENABLE_SUBTRACTIVE)begin:g_sub_schedule
            assign slot=request_slot;assign sample_tick=request_sample_tick;assign lane_enter=request_lane_enter;
        end else begin:g_ordinary_schedule
            pcm_request_drum u_schedule(.clk_i(subtractive_clk),.rst_n_i(audio_rst_n),
                .request_valid_i(pcm_sub_request_valid_i),.request_tag_i(pcm_sub_request_tag_i),
                .request_ready_o(pcm_sub_request_ready_o),.frame_tag_o(),
                .slot_o(slot),.sample_tick_o(sample_tick),.lane_enter_o(lane_enter));
        end
        logic half48,cell_half48;
        always_ff @(posedge subtractive_clk or negedge audio_rst_n)
            if(!audio_rst_n)half48<=0;else if(sample_tick)half48<=~half48;
        assign sample_tick48=sample_tick && !half48;
        assign cell_tick=(slot % synth_pkg::CELL_DIV)==0;
        always_ff @(posedge subtractive_clk or negedge audio_rst_n)
            if(!audio_rst_n)cell_half48<=0;else if(cell_tick)cell_half48<=~cell_half48;
        assign cell_tick48=cell_tick && !cell_half48;
    end else begin:g_legacy_schedule
        assign slot=legacy_slot;assign sample_tick=legacy_sample_tick;assign lane_enter=legacy_lane_enter;
        assign cell_tick=legacy_cell_tick;assign sample_tick48=legacy_sample_tick48;assign cell_tick48=legacy_cell_tick48;
    end endgenerate

    //----------------------------------------------------------------
    // 256-element pipeline
    //----------------------------------------------------------------
    logic signed [23:0] sample_left, sample_right;   // Q0.24

    logic        elem_write_enable;
    logic [2:0]  elem_write_word;
    logic [7:0]  elem_write_index;
    logic [31:0] elem_write_data;
    logic        swap_req;
    logic [9:0]  dmem_wr_addr;
    logic [17:0] dmem_wr_data;
    logic        dmem_wr_toggle;
    wire         dmem_wr_ack, bank_active_status;
    wire [15:0]  control_pass_count;
    logic        imem_write_enable;
    logic [9:0]  imem_write_addr;   // {entry[7:0], word[1:0]} — 10 bits
                            // (#100); a too-narrow wire here silently
                            // truncated entries before — size from the pool
    logic [31:0] imem_write_data;

    wire ks_write_enable;
    wire [7:0] ks_write_addr, ks_read_addr;
    wire [31:0] ks_write_data, ks_read_data;
    wire ks_mode,ks_pcm_valid;
    wire signed [23:0] ks_pcm_left,ks_pcm_right;
    wire [15:0] ks_active_mask;
    generate if(ENABLE_KS)begin:g_karplus_strong
    karplus_strong_engine #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN),.ENABLE_KS_TIMBRES(ENABLE_KS_TIMBRES),.ENABLE_KS_LIMITER(ENABLE_KS_LIMITER),.ENABLE_MIDI(ENABLE_MIDI),.ENABLE_LOCAL_ENGINE_CONTROL(ENABLE_LOCAL_ENGINE_CONTROL),
        .AUTONOMOUS_USB_MIDI(AUTONOMOUS_USB_MIDI),.KS_ISLAND_CLOCK(KS_ISLAND_CLOCK),.SPLIT_AUDIO_CLOCK(CONTROL_CLOCK_SPLIT),
        .CANCEL_ON_CONTROL_FAULT(CONTROL_CLOCK_SPLIT && ENABLE_LOCAL_ENGINE_CONTROL)) u_engine(.ae350_cmd_valid_i(ae_native_cmd_valid[1]),.ae350_cmd_ready_o(ae_native_cmd_ready[1]),.ae350_cmd_result_o(ae_native_cmd_result[2+:2]),
        .ae350_cmd_data_i(ae_native_cmd_data[160+:160]),.ae350_cmd_sequence_i(ae_native_cmd_sequence[32+:32]),
        .ae350_fb_valid_o(ae_native_fb_valid[1]),.ae350_fb_ready_i(ae_native_fb_ready[1]),
        .ae350_fb_voice_o(ae_native_fb_voice[5+:5]),.ae350_fb_generation_o(ae_native_fb_generation[32+:32]),.ae350_ready_o(ae_native_ready[1]),
        .pcm_request_valid_i(pcm_ks_request_valid_i),.pcm_request_tag_i(pcm_ks_request_tag_i),
        .pcm_request_ready_o(pcm_ks_request_ready_o),.pcm_response_valid_o(pcm_ks_response_valid_o),
        .pcm_response_tag_o(pcm_ks_response_tag_o),.pcm_response_left_o(pcm_ks_left_o),.pcm_response_right_o(pcm_ks_right_o),
        .sysclk(control_clk),
        .ks_native_clk(ks_native_clk),.ks_clock_locked_i(ks_clock_locked_i),
        .audio_clk_i(subtractive_clk),.audio_rst_n_i(audio_rst_n),
        .transaction_fault_i(engine_fault),
        .rst_n(ENABLE_AE350_VOICE_OWNER ? ae_link_reset_n : rst_n),
        .sclk(sclk),
        .slot(slot),
        .local_select_valid(local_select_valid[0]),
        .local_select_bind(local_select_bind[0]),
        .local_select_id(local_select_id),
        .local_switch_mute(local_switch_mute_branches[3]),
        .local_select_ready(local_select_ready[0]),
        .local_select_done(local_select_done[0]),
        .local_select_done_id(local_select_done_id[0+:32]),
        .local_select_result(local_select_result[0+:32]),
        .ks_midi_enable(ks_midi_enable),
        .ks_midi_valid(ks_midi_valid),
        // Off/clear can already be in our new input slot after the decoder
        // retires its copy. Cover that edge in KS's existing On barrier.
        .midi_event_pending(midi_event_pending || midi_ingress_pending),
        .ks_midi_event(ks_midi_event),
        .ks_midi_drop(ks_midi_drop),
        .ks_midi_ready(ks_midi_ready),
        .midi_consumer_flush(midi_consumer_flush),
        .midi_submit_fence(midi_submit_fence),
        .perf_write_enable(perf_write_enable),
        .perf_write_addr(perf_write_addr),
        .perf_write_data(perf_write_data),
        .ks_template_status(ks_template_status),
        .ks_write_enable(ks_write_enable),
        .ks_write_addr(ks_write_addr),
        .ks_read_addr(ks_read_addr),
        .ks_write_data(ks_write_data),
        .ks_read_data(ks_read_data),
        .ks_mode(ks_mode),
        .ks_pcm_valid(ks_pcm_valid),
        .ks_pcm_left(ks_pcm_left),
        .ks_pcm_right(ks_pcm_right),
        .ks_active_mask(ks_active_mask));
    end else begin:g_no_karplus_strong
        assign ae_native_cmd_ready[1]=0;assign ae_native_cmd_result[2+:2]=0;assign ae_native_ready[1]=0;assign ae_native_fb_valid[1]=0;assign ae_native_fb_voice[5+:5]=0;assign ae_native_fb_generation[32+:32]=0;
        assign pcm_ks_request_ready_o=1;assign pcm_ks_response_valid_o=0;assign pcm_ks_response_tag_o=0;
        assign pcm_ks_left_o=0;assign pcm_ks_right_o=0;
        assign ks_template_status=0;assign ks_read_data=0;
        assign ks_mode=0;assign ks_pcm_valid=0;assign ks_pcm_left=0;assign ks_pcm_right=0;assign ks_active_mask=0;
        assign ks_midi_ready=1'b1;assign ks_midi_drop=1'b1;
        assign local_select_ready[0]=0;assign local_select_done[0]=0;
        assign local_select_done_id[0+:32]=0;assign local_select_result[0+:32]=0;
    end endgenerate
    // KS frame f completes within f. Buffer it at f+1 so output_tilt consumes
    // f at f+2, matching the element limiter's existing publication schedule.
    // DDS is a direct bypass: its sample values and latency remain unchanged.
    logic signed [23:0] ks_pending_left, ks_pending_right;
    logic signed [23:0] ks_aligned_left, ks_aligned_right;
    logic ks_frame_seen;
    always_ff @(posedge subtractive_clk or negedge audio_rst_n) begin
        if (!audio_rst_n) begin
            ks_pending_left <= '0; ks_pending_right <= '0;
            ks_aligned_left <= '0; ks_aligned_right <= '0;
            ks_frame_seen <= 1'b0;
        end else if (sample_tick) begin
            ks_aligned_left <= ks_pcm_valid ? ks_pcm_left
                              : ks_frame_seen ? ks_pending_left : 24'sd0;
            ks_aligned_right <= ks_pcm_valid ? ks_pcm_right
                               : ks_frame_seen ? ks_pending_right : 24'sd0;
            ks_frame_seen <= 1'b0;
        end else if (ks_pcm_valid) begin
            ks_pending_left <= ks_pcm_left;
            ks_pending_right <= ks_pcm_right;
            ks_frame_seen <= 1'b1;
        end
    end
    wire fm_write_enable;
    wire [7:0] fm_write_addr, fm_read_addr;
    wire [31:0] fm_write_data, fm_read_data;
    wire fm_selected, fm_mute, fm_binding;
    wire fm_loss_mute;
    assign fm_diag_requested_o = effective_engine == 2 ||
        (ENABLE_LOCAL_ENGINE_CONTROL && engine_status[1:0] == 2);
    wire signed [23:0] fm_aligned_left, fm_aligned_right;
    generate if (ENABLE_FM) begin : g_fm
        fm_engine #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN),.ENABLE_MIDI(ENABLE_MIDI),.ENABLE_LOCAL_ENGINE_CONTROL(ENABLE_LOCAL_ENGINE_CONTROL),
            .FM_SHARED_CLOCK(FM_SHARED_CLOCK),.FM_NATIVE96(FM_NATIVE96),.FM_ISLAND_CLOCK(FM_ISLAND_CLOCK),
            .CONTROL_RESET_RELEASE(CONTROL_CLOCK_SPLIT)) u_engine(.ae350_cmd_valid_i(ae_native_cmd_valid[2]),.ae350_cmd_ready_o(ae_native_cmd_ready[2]),.ae350_cmd_result_o(ae_native_cmd_result[4+:2]),
        .ae350_cmd_data_i(ae_native_cmd_data[320+:160]),.ae350_cmd_sequence_i(ae_native_cmd_sequence[64+:32]),
        .ae350_fb_valid_o(ae_native_fb_valid[2]),.ae350_fb_ready_i(ae_native_fb_ready[2]),
        .ae350_fb_voice_o(ae_native_fb_voice[10+:5]),.ae350_fb_generation_o(ae_native_fb_generation[64+:32]),.ae350_ready_o(ae_native_ready[2]),.ae350_fault_o(ae_fm_fault),
        .sysclk(control_clk),
        .rst_n(ENABLE_AE350_VOICE_OWNER ? ae_link_reset_n : rst_n),
        .sclk(sclk),
        .pcm_request_valid_i(pcm_fm_request_valid_i),.pcm_request_tag_i(pcm_fm_request_tag_i),
        .pcm_request_ready_o(pcm_fm_request_ready_o),.pcm_response_valid_o(pcm_fm_response_valid_o),
        .pcm_response_tag_o(pcm_fm_response_tag_o),.pcm_response_left_o(pcm_fm_left_o),.pcm_response_right_o(pcm_fm_right_o),
        .fm_native_clk(fm_native_clk),
        .fm_rst_n(fm_rst_n),
        .sample_tick(sample_tick),
        .engine_fault(engine_fault),
        .fm_event_link_reset_o(fm_event_link_reset),
        .local_switch_mute(local_switch_mute_branches[4]),
        .local_select_valid(local_select_valid[1]),
        .local_select_bind(local_select_bind[1]),
        .local_select_id(local_select_id),
        .local_select_ready(local_select_ready[1]),
        .local_select_done(local_select_done[1]),
        .local_select_done_id(local_select_done_id[32+:32]),
        .local_select_result(local_select_result[32+:32]),
        .fm_midi_enable(fm_midi_enable),
        .fm_mode_cancel(fm_mode_cancel),
        .fm_midi_valid(fm_midi_valid),
        .midi_event_pending(midi_event_pending),
        .effective_test_tone(effective_test_tone),
        .effective_engine(effective_engine),
        .fm_midi_event(fm_midi_event),
        .midi_submit_fence(midi_submit_fence),
        .midi_consumer_flush(midi_consumer_flush),
        .fm_midi_ready(fm_midi_ready),
        .fm_cdc_delivered(fm_cdc_delivered),
        .fm_cdc_drained(fm_cdc_drained),
        .fm_events_quiesced_o(fm_events_quiesced),
        .fm_event_admission_ready_o(fm_event_control_ready),
        .fm_return_event(fm_return_event),
        .perf_write_enable(perf_write_enable),
        .perf_write_addr(perf_write_addr),
        .perf_write_data(perf_write_data),
        .fm_write_enable(fm_write_enable),
        .fm_write_addr(fm_write_addr),
        .fm_read_addr(fm_read_addr),
        .fm_write_data(fm_write_data),
        .fm_read_data(fm_read_data),
        .fm_selected(fm_selected),
        .fm_mute(fm_mute),
        .fm_binding(fm_binding),
        .fm_loss_mute(fm_loss_mute),
        .fm_midi_drop(fm_midi_drop),
        .fm_aligned_left(fm_aligned_left),
        .fm_aligned_right(fm_aligned_right),
        .fm_pcm_left_o(fm_pcm_left_o),
        .fm_pcm_right_o(fm_pcm_right_o),
        .fm_route_o(),
        .fm_path_rst_n_o(fm_path_rst_n_o),
        .fm_pcm_valid_o(fm_pcm_valid_o),
        .fm_diag_o(fm_diag_status_o),
        .reverb_fm_rst_n_o(reverb_fm_rst_n_o),
        .reverb_fm_mute_o(reverb_fm_mute_o));
    end else begin : g_no_fm
        assign ae_fm_fault=0;assign ae_native_cmd_ready[2]=0;assign ae_native_cmd_result[4+:2]=0;assign ae_native_ready[2]=0;assign ae_native_fb_valid[2]=0;assign ae_native_fb_voice[10+:5]=0;assign ae_native_fb_generation[64+:32]=0;
        assign fm_diag_status_o=8'd0;
        assign pcm_fm_request_ready_o=1;assign pcm_fm_response_valid_o=0;assign pcm_fm_response_tag_o=0;
        assign pcm_fm_left_o=0;assign pcm_fm_right_o=0;
        assign fm_pcm_valid_o=1'b0;
        assign reverb_fm_rst_n_o=1'b0;
        assign reverb_fm_mute_o=1'b1;
        assign fm_path_rst_n_o=rst_n && fm_rst_n;
        assign fm_midi_ready = 1'b1;
        assign fm_midi_drop = 1'b1;
        assign fm_read_data = 32'd0;
        assign fm_selected = 1'b0;
        assign fm_binding = 1'b0;
        assign fm_event_link_reset = 1'b1;
        assign fm_mute = 1'b1;
        assign fm_loss_mute = 1'b0;
        assign fm_aligned_left = 24'sd0;
        assign fm_aligned_right = 24'sd0;
        assign fm_pcm_left_o=24'sd0;
        assign fm_pcm_right_o=24'sd0;
        assign fm_events_quiesced=1'b1;
        assign fm_event_control_ready=1'b1;
        assign fm_cdc_delivered=1'b0;
        assign fm_cdc_drained=1'b0;
        assign fm_return_event='0;
        assign local_select_ready[1]=0;assign local_select_done[1]=0;
        assign local_select_done_id[32+:32]=0;assign local_select_result[32+:32]=0;
    end endgenerate
    wire signed [23:0] selected_left = audio_engine==4 ? flute_pcm : audio_engine==3 ? additive_left : audio_engine==2 ? fm_aligned_left :
                                      audio_engine==1 ? ks_aligned_left : sample_left;
    wire signed [23:0] selected_right = audio_engine==4 ? flute_pcm : audio_engine==3 ? additive_right : audio_engine==2 ? fm_aligned_right :
                                       audio_engine==1 ? ks_aligned_right : sample_right;

    generate if(ENABLE_FLUTE)begin:g_flute
        flute_engine u_engine (
            .control_clk_i(control_clk),.rst_n_i(rst_n),.audio_clk_i(subtractive_clk),.audio_rst_n_i(audio_rst_n),
            .sample_tick48_i(sample_tick48),.enable_i(flute_midi_enable),
            .audio_enable_i(audio_engine==4 && !audio_mute && audio_rst_n),
            .midi_event_i(flute_midi_event),.midi_valid_i(flute_midi_valid),.midi_ready_o(flute_midi_ready),
            .flush_i(midi_submit_fence),.cancel_i(fm_mode_cancel),
            .local_switch_busy_i(local_switch_busy),
            .local_select_valid_i(local_select_valid[3]),.local_select_bind_i(local_select_bind[3]),
            .local_select_id_i(local_select_id),.local_select_ready_o(local_select_ready[3]),
            .local_select_done_o(local_select_done[3]),.local_select_done_id_o(local_select_done_id[96+:32]),
            .local_select_result_o(local_select_result[96+:32]),.selected_o(flute_selected),
            .reg_clk_i(sclk),.reg_write_i(midi_write_enable),.reg_addr_i(midi_write_addr),.reg_data_i(midi_write_data),
            .reg_read_addr_i(midi_read_addr),.reg_read_data_o(flute_register_data),
            .pcm_o(flute_pcm),.pcm_valid_o(flute_pcm_valid),.quiesced_o(flute_events_quiesced),
            .midi_delivered_o(flute_midi_delivered),.midi_drained_o(flute_midi_drained));
    end else begin:g_no_flute
        assign flute_selected=0;assign flute_register_data=0;assign flute_pcm=0;assign flute_pcm_valid=0;
        assign flute_midi_ready=0;assign flute_events_quiesced=1;assign flute_midi_delivered=0;assign flute_midi_drained=0;
        assign local_select_ready[3]=0;assign local_select_done[3]=0;
        assign local_select_done_id[96+:32]=0;assign local_select_result[96+:32]=0;
    end endgenerate
    wire sub_dds_valid,sub_dds_response;
    wire [13:0] sub_dds_pitch;
    wire [23:0] sub_dds_phase,sub_dds_duty,sub_dds_new_phase;
    wire [1:0] sub_dds_wave;
    wire signed [17:0] sub_dds_sample;
    generate if(ENABLE_ADDITIVE)begin:g_additive
        wire add_valid,add_ready,add_out_valid;
        wire [23:0] add_phase,add_step,add_new_phase;
        wire signed [17:0] add_sample;
        additive_engine #(.ENABLE_LOCAL_SELECT(ENABLE_LOCAL_ENGINE_CONTROL)) u_engine(
            .local_select_valid_i(local_select_valid[2]),.local_select_bind_i(local_select_bind[2]),
            .local_select_id_i(local_select_id),.local_switch_busy_i(local_switch_mute_branches[5]),
            .local_select_ready_o(local_select_ready[2]),.local_select_done_o(local_select_done[2]),
            .local_select_done_id_o(local_select_done_id[64+:32]),.local_select_result_o(local_select_result[64+:32]),
            .clk(subtractive_clk),.sclk,.rst_n(audio_rst_n),.sample_tick,.slot,.enable(add_midi_enable),
            .perf_write_enable,.perf_write_addr,.perf_write_data,
            .midi_event(add_midi_event),.midi_event_valid(add_midi_valid),.midi_event_ready(add_midi_ready),
            .midi_flush(midi_submit_fence),.selected(additive_selected),.bypass_tilt(additive_bypass_tilt),.status(additive_status),
            .add_valid,.add_phase,.add_step,.add_ready,.add_out_valid,.add_new_phase,.add_sample,
            .pcm_left(additive_left),.pcm_right(additive_right),.pcm_valid(additive_pcm_valid));
        shared_dds_service #(.SHARE_DDS(SHARE_ADDITIVE_DDS)) u_shared_dds (
            .clk(subtractive_clk),.rst_n(audio_rst_n),.sub_valid(sub_dds_valid),.sub_pitch(sub_dds_pitch),
            .sub_phase(sub_dds_phase),.sub_duty(sub_dds_duty),.sub_wave(sub_dds_wave),
            .sub_out_valid(sub_dds_response),.sub_new_phase(sub_dds_new_phase),.sub_sample(sub_dds_sample),
            .add_valid,.add_phase,.add_step,.add_ready,.add_out_valid,.add_new_phase,.add_sample);
    end else begin:g_no_additive
        assign additive_selected=0;assign additive_bypass_tilt=0;assign additive_status=0;
        assign local_select_ready[2]=0;assign local_select_done[2]=0;
        assign local_select_done_id[64+:32]=0;assign local_select_result[64+:32]=0;
        assign additive_left=0;assign additive_right=0;assign additive_pcm_valid=0;
        assign add_midi_ready=0;assign sub_dds_response=0;assign sub_dds_new_phase=0;assign sub_dds_sample=0;
    end endgenerate
    wire sub_result_valid;
    wire [63:0] sub_result_tag;
    wire signed [23:0] sub_result_left,sub_result_right;
    generate if(ENABLE_SUBTRACTIVE)begin:g_subtractive
    subtractive_engine #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),
        .ENABLE_AE350_FDC(ENABLE_AE350_FDC),.FDC_WATCHDOG_CYCLES(FDC_WATCHDOG_CYCLES),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN),.SPLIT_MIDI_ENABLE(1'b1),.ENABLE_MIDI(ENABLE_MIDI),.REGISTERED_EVENT_INPUT(ENABLE_MIDI),.EXTERNAL_DDS(ENABLE_ADDITIVE),
        .LOCAL_CHORD_CONTROL(LOCAL_CHORD_CONTROL),.LOCAL_MIDI_BOOT(AUTONOMOUS_USB_MIDI)) u_engine (.ae350_cmd_valid_i(ae_native_cmd_valid[0]),.ae350_cmd_ready_o(ae_native_cmd_ready[0]),.ae350_cmd_result_o(ae_native_cmd_result[0+:2]),
        .ae350_cmd_data_i(ae_native_cmd_data[0+:160]),.ae350_cmd_sequence_i(ae_native_cmd_sequence[0+:32]),
        .ae350_fb_valid_o(ae_native_fb_valid[0]),.ae350_fb_ready_i(ae_native_fb_ready[0]),
        .ae350_fb_voice_o(ae_native_fb_voice[0+:5]),.ae350_fb_generation_o(ae_native_fb_generation[0+:32]),.ae350_ready_o(ae_native_ready[0]),
        .legacy_chord_play_i(audio_legacy_chord),
        .midi_enable(dds_native_enable),.midi_render_enable(audio_dds_render),
        .midi_csp_enable(audio_dds_csp),.midi_event(dds_native_event),.midi_event_valid(dds_native_valid),
        .midi_capture_pending(dds_native_capture_pending),
        .midi_event_ready(dds_native_ready),.midi_flush(dds_native_flush),
        .perf_write_enable,.perf_write_addr,.perf_write_data,.midi_template_status(dds_template_status),
        .pcm_request_valid_i(pcm_sub_request_valid_i),.pcm_request_tag_i(pcm_sub_request_tag_i),
        .pcm_request_ready_o(pcm_sub_request_ready_o),.pcm_response_valid_o(sub_result_valid),
        .pcm_response_tag_o(sub_result_tag),.pcm_response_left_o(sub_result_left),.pcm_response_right_o(sub_result_right),
        .pcm_slot_o(request_slot),.pcm_sample_tick_o(request_sample_tick),.pcm_lane_enter_o(request_lane_enter),
        .clk         (subtractive_clk),
        .rst_n       (ae_sub_link_reset_n),
        .slot        (slot),
        .lane_enter  (lane_enter),
        .sample_tick (sample_tick),
        .sclk        (sclk),
        .elem_write_enable       (elem_write_enable),
        .elem_write_word     (elem_write_word),
        .elem_write_index     (elem_write_index),
        .elem_write_data    (elem_write_data),
        .dmem_wr_addr     (dmem_wr_addr),
        .dmem_wr_data     (dmem_wr_data),
        .dmem_wr_toggle      (dmem_wr_toggle),
        .dmem_wr_ack         (dmem_wr_ack),
        .bank_active_status  (bank_active_status),
        .control_pass_count  (control_pass_count),
        .imem_write_enable       (imem_write_enable),
        .imem_write_addr     (imem_write_addr),
        .imem_write_data     (imem_write_data),
        .swap_req    (swap_req),
        .mix_left    (sample_left),
        .mix_right   (sample_right),
        .test_tone_en (test_tone_en),
        .dds_req_valid(sub_dds_valid),.dds_req_pitch(sub_dds_pitch),.dds_req_phase(sub_dds_phase),
        .dds_req_duty(sub_dds_duty),.dds_req_wave(sub_dds_wave),.dds_resp_valid(sub_dds_response),
        .dds_resp_phase(sub_dds_new_phase),.dds_resp_sample(sub_dds_sample)
    );
    end else begin:g_no_subtractive
        assign ae_native_cmd_ready[0]=0;assign ae_native_cmd_result[0+:2]=0;assign ae_native_ready[0]=0;assign ae_native_fb_valid[0]=0;assign ae_native_fb_voice[0+:5]=0;assign ae_native_fb_generation[0+:32]=0;
        assign sub_result_valid=0;assign sub_result_tag=0;assign sub_result_left=0;assign sub_result_right=0;
        if(!PCM_REQUEST_DRIVEN)assign pcm_sub_request_ready_o=1;
        assign request_slot=0;assign request_sample_tick=0;assign request_lane_enter=0;
        assign sample_left=0;assign sample_right=0;assign test_tone_en=0;
        // ready属于原生端；控制端ready仍由事件桥/同域旁路单独驱动。
        assign dds_native_ready=1'b1;assign dds_template_status=0;
        // ACK means both dmem generations committed. An absent engine must
        // never manufacture that acknowledgement; other SPI windows remain usable.
        assign dmem_wr_ack=0;assign bank_active_status=0;assign control_pass_count=0;
        assign sub_dds_valid=0;assign sub_dds_pitch=0;assign sub_dds_phase=0;
        assign sub_dds_duty=0;assign sub_dds_wave=0;
    end endgenerate

    //----------------------------------------------------------------
    // Test tone (audio-chain purity check, issue #81): 1500 Hz sine,
    // 64-sample period at 96 kHz (phase step 2^18 = 262144 EXACTLY),
    // ~0 dBFS (sine LUT peak << 8 = 8388352 of 8388607, −0.0003 dB).
    // Midband per Thor — coupling caps in the analog chain attenuate
    // low tones; 1500 Hz measures the chain flat. Enabled by
    // bus-address-1023 writes (firmware maps CC 119); replaces the mix
    // at BOTH outputs. At 48 kHz capture the tone lands exactly on
    // bin 32 of a 1024-point FFT — coherent, no window, harmonics on
    // exact bins (64, 96, ...). Thor's purity criterion: any harmonic
    // above 1/4096 of the fundamental (−72.2 dBc) rings alarms.
    //----------------------------------------------------------------
    logic test_tone_en;
    wire selected_fault=ENABLE_AE350_VOICE_OWNER ? (audio_engine==2 && (audio_fm_loss || audio_fm_mute)) :
        audio_engine!=3 && audio_engine!=4 && (audio_fm_loss || (audio_fm_selected && audio_fm_mute));
    wire output_filter_rst_n = audio_rst_n && !selected_fault && !audio_mute;
    wire audio_test_tone=test_tone_en && !audio_owner;
    assign fm_route_o=FM_ISLAND_CLOCK && audio_engine==2 && !audio_test_tone;
    assign reverb_synth_rst_n_o=output_filter_rst_n && !audio_mute;
    assign reverb_synth_mute_o=selected_fault || audio_mute;
    assign reverb_synth_bypass_o=audio_test_tone;
    assign pcm_valid=sample_tick;
    assign pcm_source_o=audio_engine;
    assign pcm_mute_o=audio_mute || selected_fault;
    assign pcm_test_tone_o=audio_test_tone;
    assign pcm_revision_o=audio_source_revision;
    // A requested frame owns its return branch until publication. Route
    // changes must not select both SUB(+10) and ordinary(+12) for one tag.
    logic [63:0] ordinary_render_tag,ordinary_publish_tag;
    logic [2:0] ordinary_render_source,ordinary_publish_source;
    logic ordinary_have_frame,ordinary_publish_pending;
    logic [3:0] ordinary_publish_delay;
    logic ordinary_result_valid;
    logic signed [23:0] ordinary_result_left,ordinary_result_right;
    wire ordinary_return_owner=!ENABLE_SUBTRACTIVE ||
        ordinary_publish_source==3 || ordinary_publish_source==4;
    wire ordinary_source_matches=audio_engine==ordinary_publish_source;
    always_ff @(posedge subtractive_clk or negedge audio_rst_n)begin
        if(!audio_rst_n)begin
            ordinary_render_tag<=0;ordinary_publish_tag<=0;ordinary_have_frame<=0;
            ordinary_render_source<=0;ordinary_publish_source<=0;
            ordinary_publish_pending<=0;ordinary_publish_delay<=0;ordinary_result_valid<=0;
            ordinary_result_left<=0;ordinary_result_right<=0;
        end else begin
            ordinary_result_valid<=0;
            if(pcm_sub_request_valid_i && pcm_sub_request_ready_o)begin
                ordinary_render_tag<=pcm_sub_request_tag_i;
                ordinary_render_source<=audio_engine;
            end
            if(sample_tick)begin
                ordinary_have_frame<=1;
                if(ordinary_have_frame)begin
                    ordinary_publish_tag<=ordinary_render_tag;
                    ordinary_publish_source<=ordinary_render_source;
                    ordinary_publish_pending<=!ENABLE_SUBTRACTIVE ||
                        ordinary_render_source==3 || ordinary_render_source==4;
                    ordinary_publish_delay<=12;
                end
            end else if(ordinary_publish_pending)begin
                if(ordinary_publish_delay==1)begin
                    ordinary_result_left<=ordinary_source_matches ?
                        (ordinary_publish_source==3 ? additive_left :
                         ordinary_publish_source==4 ? flute_pcm : 24'sd0) : 24'sd0;
                    ordinary_result_right<=ordinary_source_matches ?
                        (ordinary_publish_source==3 ? additive_right :
                         ordinary_publish_source==4 ? flute_pcm : 24'sd0) : 24'sd0;
                    ordinary_result_valid<=1;ordinary_publish_pending<=0;
                end else ordinary_publish_delay<=ordinary_publish_delay-1'b1;
            end
        end
    end
    // BEGIN_PCM_RETURN_PIPELINE
    generate if(PCM_REQUEST_DRIVEN)begin:g_pcm_return_stage
        // Qualify a complete packet from one publication edge. Register the
        // byte comparisons before their reduction so a 64-bit tag compare
        // never directly drives the transport's wide packet write enable.
        // The request/frame timeline is unchanged; native return latency is
        // one clock longer, with an input interval of one clock.
        logic [111:0] return_packet;
        logic return_valid,return_ordinary;
        (* syn_preserve = 1 *)logic [7:0] return_tag_equal;
        always_ff @(posedge subtractive_clk or negedge audio_rst_n)begin
            if(!audio_rst_n)begin
                return_packet<=0;return_valid<=0;return_ordinary<=0;return_tag_equal<=0;
            end else begin
                return_valid<=ordinary_return_owner ? ordinary_result_valid : sub_result_valid;
                return_ordinary<=ordinary_return_owner;
                // Capture selection qualification with the payload. A route
                // change after this edge cannot relabel an owned old frame.
                return_packet<={ordinary_return_owner ? ordinary_publish_tag : sub_result_tag,
                    !ordinary_source_matches ? 24'sd0 : ordinary_return_owner ?
                        ordinary_result_left : ordinary_publish_source==0 ? sub_result_left : 24'sd0,
                    !ordinary_source_matches ? 24'sd0 : ordinary_return_owner ?
                        ordinary_result_right : ordinary_publish_source==0 ? sub_result_right : 24'sd0};
                for(integer byte_index=0;byte_index<8;byte_index=byte_index+1)
                    return_tag_equal[byte_index]<=sub_result_tag[byte_index*8+:8]==
                        ordinary_publish_tag[byte_index*8+:8];
            end
        end
        assign pcm_sub_response_valid_o=return_valid && (return_ordinary || &return_tag_equal);
        assign {pcm_sub_response_tag_o,pcm_sub_left_o,pcm_sub_right_o}=return_packet;
    end else begin:g_legacy_pcm_return
        assign pcm_sub_response_valid_o=1'b0;
        assign pcm_sub_response_tag_o=ordinary_return_owner ? ordinary_publish_tag : sub_result_tag;
        // Preserve the owned identity even when selection changed. Such a frame
        // completes with tagged silence, never PCM from the newly selected engine.
        assign pcm_sub_left_o=!ordinary_source_matches ? 24'sd0 : ordinary_return_owner ?
            ordinary_result_left : ordinary_publish_source==0 ? sub_result_left : 24'sd0;
        assign pcm_sub_right_o=!ordinary_source_matches ? 24'sd0 : ordinary_return_owner ?
            ordinary_result_right : ordinary_publish_source==0 ? sub_result_right : 24'sd0;
    end endgenerate
    // END_PCM_RETURN_PIPELINE
    assign input_mute_o=local_switch_mute || audio_domain_fault;
    assign selected_left_o=selected_left;assign selected_right_o=selected_right;
    assign additive_left_o=additive_left;assign additive_right_o=additive_right;
    assign additive_selected_o=additive_selected;assign additive_bypass_tilt_o=additive_bypass_tilt;
    assign selected_fault_o=selected_fault;assign output_filter_rst_n_o=output_filter_rst_n;
    assign cell_tick_o=cell_tick;assign sample_tick48_o=sample_tick48;assign cell_tick48_o=cell_tick48;
    //----------------------------------------------------------------
    // SPI control plane — the docs/memory_map.md wire protocol:
    // 16-bit word addresses, 32-bit data, 11-bit backed global window.
    // (The byte-protocol bring-up slave spi_slave_regs.sv remains in
    // the tree as reference; this is its successor.)
    //----------------------------------------------------------------
    generate if(ENABLE_AE350_VOICE_OWNER)begin:g_prepared_register_view
        // The SPI CPU mode already rejects the old element/voice/owner writes.
        // Keep MID1's registered read handshake, without a duplicate backed
        // register RAM, high-level write decoder or shadow parameter compiler.
        (* ASYNC_REG="TRUE" *)reg read_meta,read_sync;
        reg read_previous,read_pending;reg[31:0] read_data;
        always @(posedge control_clk or negedge config_reset_n)begin
            if(!config_reset_n)begin read_meta<=0;read_sync<=0;read_previous<=0;read_pending<=0;read_data<=0;end
            else begin
                read_meta<=cfg_read_req_toggle_i;read_sync<=read_meta;
                read_previous<=read_sync;read_pending<=read_sync!=read_previous;
                if(read_pending)read_data<=cfg_read_addr_i[15:8]==8'h07 ? midi_read_data : 32'd0;
            end
        end
        assign cfg_read_data_o=read_data;
        assign midi_read_addr=cfg_read_addr_i[7:0];
        assign elem_write_enable=0;assign elem_write_word=0;assign elem_write_index=0;assign elem_write_data=0;
        assign dmem_wr_addr=0;assign dmem_wr_data=0;assign dmem_wr_toggle=0;
        assign imem_write_enable=0;assign imem_write_addr=0;assign imem_write_data=0;assign swap_req=0;
        assign ks_write_enable=0;assign ks_write_addr=0;assign ks_write_data=0;assign ks_read_addr=0;
        assign fm_write_enable=0;assign fm_write_addr=0;assign fm_write_data=0;assign fm_read_addr=0;
        assign midi_write_enable=0;assign midi_write_addr=0;assign midi_write_data=0;
        assign perf_write_enable=0;assign perf_write_addr=0;assign perf_write_data=0;
    end else begin:g_legacy_control_registers
    synth_control_registers u_control_regs (
        .cfg_clk_i,.cfg_write_i,.cfg_addr_i,.cfg_data_i,
        .cfg_read_req_toggle_i,.cfg_read_addr_i,.cfg_read_data_o,
        .sysclk   (control_clk),
        .rst_n    (config_reset_n),
        .elem_write_enable    (elem_write_enable),
        .elem_write_word  (elem_write_word),
        .elem_write_index  (elem_write_index),
        .elem_write_data (elem_write_data),
        .dmem_wr_addr  (dmem_wr_addr),
        .dmem_wr_data  (dmem_wr_data),
        .dmem_wr_toggle   (dmem_wr_toggle),
        .dmem_wr_ack      (dmem_ack_control),
        .bank_active_status (bank_active_control),
        .control_pass_count (control_pass_count_control),
        .imem_write_enable    (imem_write_enable),
        .imem_write_addr  (imem_write_addr),
        .imem_write_data  (imem_write_data),
        .swap_req (swap_req),
        .ks_write_enable(ks_write_enable), .ks_write_addr(ks_write_addr),
        .ks_write_data(ks_write_data), .ks_read_addr(ks_read_addr),
        .ks_read_data(ks_read_data),
        .fm_write_enable(fm_write_enable), .fm_write_addr(fm_write_addr),
        .fm_write_data(fm_write_data), .fm_read_addr(fm_read_addr),
        .fm_read_data(fm_read_data),
        .midi_write_enable,.midi_write_addr,.midi_write_data,.midi_read_addr,.midi_read_data,
        .perf_write_enable,.perf_write_addr,.perf_write_data
    );
    end endgenerate

    //----------------------------------------------------------------
    // led[0]: ~1.4 Hz liveness blink derived from sample ticks —
    // proves clock + drum with one flop chain. Others off.
    //----------------------------------------------------------------
    logic [16:0] beat;
    always_ff @(posedge subtractive_clk or negedge audio_rst_n)
        if (!audio_rst_n)      beat <= '0;
        else if (sample_tick) beat <= beat + 1'b1;

    assign led = {4'b1111, ~rst, ~beat[16]};

`ifndef SYNTHESIS
    initial if(CONTROL_CLOCK_SPLIT && ENABLE_ADDITIVE)
        $fatal(1,"control50 additive requires an explicit native command adapter");
`endif


endmodule
`default_nettype wire
