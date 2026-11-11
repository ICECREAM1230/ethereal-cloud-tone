// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Complete additive engine. All persistent partial/voice/control state is  owned here; 
//the only connection to another synthesis engine is DDS service.
`default_nettype none
module additive_engine #(
    parameter bit ENABLE_LOCAL_SELECT=0
) (
    input wire clk,sclk,rst_n,sample_tick,enable,
    input wire [9:0] slot,
    input wire perf_write_enable,
    input wire [15:0] perf_write_addr,
    input wire [31:0] perf_write_data,
    input midi_pkg::midi_event_t midi_event,
    input wire midi_event_valid,
    output wire midi_event_ready,
    input wire [3:0] midi_flush,
    output wire selected,bypass_tilt,
    output wire [31:0] status,
    output wire add_valid,
    output wire [23:0] add_phase,add_step,
    input wire add_ready,add_out_valid,
    input wire [23:0] add_new_phase,
    input wire signed [17:0] add_sample,
    output wire signed [23:0] pcm_left,pcm_right,
    output wire pcm_valid,
    input wire local_select_valid_i,local_select_bind_i,
    input wire [31:0] local_select_id_i,
    input wire local_switch_busy_i,
    output wire local_select_ready_o,local_select_done_o,
    output wire [31:0] local_select_done_id_o,local_select_result_o
);
    wire [12:0] centroid;
    wire [15:0] slope,comb_depth,master_gain,master_pan;
    wire [4:0] comb_period;
    wire [6:0] bend_range;
    wire [63:0] attack_inc,decay_inc,sustain_level,release_inc,groups;
    wire [767:0] ratios;
    wire [511:0] weights,gains,voice_envelopes;
    wire [191:0] voice_steps;
    wire [127:0] voice_pan;
    wire [63:0] voice_generation;
    wire [7:0] voice_active;
    wire spectrum_busy,spectrum_valid;
    logic spectrum_tick;
    // The COMMIT mailbox publishes active parameters on sample_tick.
    // Start spectrum one edge later so this frame uses that same generation.
    always_ff @(posedge clk or negedge rst_n)
        if(!rst_n)spectrum_tick<=1'b0;
        else spectrum_tick<=sample_tick;
    wire active=enable && selected;
    additive_control_bridge #(.ENABLE_LOCAL_SELECT(ENABLE_LOCAL_SELECT)) u_control(.clk,.sclk,.rst_n,.sample_tick,
        .perf_write_enable,.perf_write_addr,.perf_write_data,.selected,.bypass_tilt,.status,
        .centroid,.slope,.comb_period,.comb_depth,.master_gain,.master_pan,.bend_range,
        .attack_inc,.decay_inc,.sustain_level,.release_inc,.ratios,.weights,.groups,
        .local_select_valid_i,.local_select_bind_i,.local_select_id_i,.local_switch_busy_i,
        .local_select_ready_o,.local_select_done_o,.local_select_done_id_o,.local_select_result_o);
    additive_voice_control u_voices(.clk,.rst_n,.enable(active),.slot,
        .midi_event,.midi_event_valid,.midi_event_ready,.midi_flush,
        .attack_inc,.decay_inc,.sustain_level,.release_inc,.bend_range,
        .voice_steps,.voice_envelopes,.voice_pan,.voice_generation,.voice_active);
    additive_spectrum u_spectrum(.clk,.rst_n,.sample_tick(spectrum_tick),.centroid,.slope,
        .comb_period,.comb_depth,.weights,.gains,.busy(spectrum_busy),.valid(spectrum_valid));
    additive_renderer u_renderer(.clk,.rst_n,.enable(active),.sample_tick,.slot,
        .voice_steps,.voice_envelopes,.voice_pan,.voice_generation,.voice_active,
        .ratios,.groups,.gains,.master_gain,.master_pan,
        .add_valid,.add_phase,.add_step,.add_ready,.add_out_valid,.add_new_phase,.add_sample,
        .pcm_left,.pcm_right,.pcm_valid);
`ifndef SYNTHESIS
    always @(posedge clk)if(rst_n && active && slot==10'd319)
        assert(!spectrum_busy)else $fatal(1,"spectrum did not finish before additive window");
`endif
endmodule
`default_nettype wire
