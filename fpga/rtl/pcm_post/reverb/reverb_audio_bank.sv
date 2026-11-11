`default_nettype none
// Independent native-rate effects; PCM never crosses between audio clocks.
// wet/damp are unsigned Q1.15 levels in subtractive_clk_i (0..32768 / 0..28672).
// Present a coherent pair in that domain. The FM copy receives an atomic
// held-bus snapshot; sustained settings converge without a per-frame command.
// Reset inputs follow the existing audio filters' hard-mute/fault resets.
module reverb_audio_bank #(
    parameter bit ENABLE = 1'b1
) (
    input wire subtractive_clk_i, synth_rst_n_i, synth_tick_i,
    input wire synth_mute_i, synth_bypass_i,
    input wire signed [23:0] synth_left_i, synth_right_i,
    input wire fm_clk_i, fm_rst_n_i, fm_tick_i, fm_mute_i,
    input wire signed [23:0] fm_left_i, fm_right_i,
    input wire [15:0] wet_q15_i, damp_q15_i,
    output wire signed [23:0] synth_left_o, synth_right_o,
    output wire signed [23:0] fm_left_o, fm_right_o,
    output wire synth_ready_o, fm_ready_o,
    output wire synth_late_o, fm_late_o,
    output wire synth_clipped_o, fm_clipped_o
);
    generate if (ENABLE) begin : enabled
        logic [31:0] fm_params;
        wire [31:0] fm_param_update;
        wire fm_param_valid;
        wire signed [23:0] synth_wet_left, synth_wet_right;
        wire signed [23:0] fm_wet_left, fm_wet_right;
        held_bus_cdc #(.WIDTH(32)) u_params (
            .src_clk_i(subtractive_clk_i), .dst_clk_i(fm_clk_i),
            .rst_n_i(synth_rst_n_i && fm_rst_n_i),
            .src_data_i({wet_q15_i,damp_q15_i}), .src_valid_i(1'b1), .src_ready_o(),
            .dst_data_o(fm_param_update), .dst_valid_o(fm_param_valid), .dst_ready_i(1'b1)
        );
        // Resetting the other audio domain flushes the transport, while an
        // active FM effect keeps its last accepted coherent setting.
        always_ff @(posedge fm_clk_i or negedge fm_rst_n_i) begin
            if (!fm_rst_n_i) fm_params <= 32'd0;
            else if (fm_param_valid) fm_params <= fm_param_update;
        end
        // Additive's direct PCM is published at tick+2; accept it at +3.
        // FM publishes at tick, and retains +1 to fit its 128-clock frame.
        reverb_audio_stage #(.RUNTIME_PARAMS(1'b1), .INPUT_DELAY_CYCLES(3)) u_synth (
            .clk(subtractive_clk_i), .rst_n(synth_rst_n_i && !synth_bypass_i),
            .sample_tick(synth_tick_i), .wet_q15(wet_q15_i), .damp_q15(damp_q15_i),
            .pcm_l_in(synth_left_i), .pcm_r_in(synth_right_i),
            .pcm_l_out(synth_wet_left), .pcm_r_out(synth_wet_right),
            .out_valid(), .ready(synth_ready_o), .late(synth_late_o), .clipped(synth_clipped_o)
        );
        reverb_audio_stage #(.RUNTIME_PARAMS(1'b1)) u_fm (
            .clk(fm_clk_i), .rst_n(fm_rst_n_i), .sample_tick(fm_tick_i),
            .wet_q15(fm_params[31:16]), .damp_q15(fm_params[15:0]),
            .pcm_l_in(fm_left_i), .pcm_r_in(fm_right_i),
            .pcm_l_out(fm_wet_left), .pcm_r_out(fm_wet_right),
            .out_valid(), .ready(fm_ready_o), .late(fm_late_o), .clipped(fm_clipped_o)
        );
        // Preserve immediate hard silence and the unfiltered diagnostic tone.
        // Bypass also clears the synth effect, so its tone cannot leak later.
        assign synth_left_o = synth_mute_i ? 24'sd0 : synth_bypass_i ? synth_left_i : synth_wet_left;
        assign synth_right_o = synth_mute_i ? 24'sd0 : synth_bypass_i ? synth_right_i : synth_wet_right;
        assign fm_left_o = fm_mute_i ? 24'sd0 : fm_wet_left;
        assign fm_right_o = fm_mute_i ? 24'sd0 : fm_wet_right;
    end else begin : disabled
        // A compile-time opt-out preserves the complete pre-effect PCM path.
        assign synth_left_o=synth_left_i;
        assign synth_right_o=synth_right_i;
        assign fm_left_o=fm_left_i;
        assign fm_right_o=fm_right_i;
        assign synth_ready_o=synth_rst_n_i;
        assign fm_ready_o=fm_rst_n_i;
        assign synth_late_o=1'b0;
        assign fm_late_o=1'b0;
        assign synth_clipped_o=1'b0;
        assign fm_clipped_o=1'b0;
    end endgenerate
endmodule
`default_nettype wire
