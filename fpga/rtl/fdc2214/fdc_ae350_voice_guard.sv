// SPDX-License-Identifier: MIT
`default_nettype none
// Optional audio-domain endpoint for an already applied native FDC voice job.
// Its owner supplies the actual native voice/generation and keeps that identity
// with each rendered element. This is not a second allocator or MIDI parser.
// All inputs must be synchronous to clk_i; bundled control needs CDC upstream.
module fdc_ae350_voice_guard #(
    parameter bit ENABLE = 1'b0,
    parameter integer SAMPLE_W = 18,
    parameter integer TAG_W = 8,
    parameter integer WATCHDOG_CYCLES = 36_867_000
) (
    input wire clk_i, rst_ni,
    input wire applied_valid_i,
    input wire [31:0] applied_epoch_i, applied_sequence_i,
    input wire [4:0] applied_voice_i,
    input wire [31:0] applied_generation_i,
    input wire [16:0] applied_gain_i,
    input wire applied_active_i,
    input wire revoke_i,
    input wire [4:0] revoke_voice_i,
    input wire [31:0] revoke_generation_i,
    input wire in_valid_i, in_sensor_owned_i,
    input wire [4:0] in_voice_i,
    input wire [31:0] in_generation_i,
    input wire [TAG_W-1:0] in_tag_i,
    input wire signed [SAMPLE_W-1:0] in_left_i, in_right_i,
    output wire out_valid_o,
    output wire [TAG_W-1:0] out_tag_o,
    output wire signed [SAMPLE_W-1:0] out_left_o, out_right_o,
    output wire alive_o, timed_out_o
);
    generate if (ENABLE) begin : g_enabled
        localparam integer CW = WATCHDOG_CYCLES > 1 ? $clog2(WATCHDOG_CYCLES+1) : 1;
        reg [CW-1:0] remaining;
        reg [31:0] epoch, sequence_value, generation;
        reg [4:0] voice;
        reg [16:0] gain;
        reg have_identity, active, timed_out;
        reg [TAG_W-1:0] tag_product, tag_result;
        wire new_epoch = applied_epoch_i > epoch;
        wire newer = applied_epoch_i != 0 && applied_sequence_i != 0 &&
            (new_epoch || (applied_epoch_i == epoch && applied_sequence_i > sequence_value));
        wire accept_control = applied_valid_i && newer &&
            applied_generation_i != 0 && applied_gain_i <= 17'd65536;
        wire revoked = revoke_i && have_identity &&
            revoke_voice_i == voice && revoke_generation_i == generation;
        wire alive = have_identity && remaining != 0 && !revoked;
        wire same_owner = have_identity && in_voice_i == voice && in_generation_i == generation;
        // A sensor-labelled element without a live matching identity is silent.
        // A different (MIDI) owner bypasses this endpoint, including same-slot
        // reuse. Old FDC watchdog expiration cannot silence the replacement.
        wire [16:0] sample_gain = alive && active && same_owner ? gain : 17'd0;
        assign alive_o = alive;
        assign timed_out_o = timed_out;
        initial begin
            if (WATCHDOG_CYCLES < 1 || TAG_W < 1)
                $fatal(1,"FDC voice guard requires positive watchdog and tag width");
        end
        always @(posedge clk_i) begin
            if (!rst_ni) begin
                remaining <= 0; epoch <= 0; sequence_value <= 0;
                generation <= 0; voice <= 0; gain <= 0;
                have_identity <= 0; active <= 0; timed_out <= 0;
            end else if (revoked) begin
                remaining <= 0; active <= 0; have_identity <= 0;
                // Retain sequence history: replaying the revoked job cannot arm.
            end else if (accept_control) begin
                epoch <= applied_epoch_i; sequence_value <= applied_sequence_i;
                voice <= applied_voice_i; generation <= applied_generation_i;
                gain <= applied_gain_i; active <= applied_active_i;
                remaining <= CW'(WATCHDOG_CYCLES); have_identity <= 1; timed_out <= 0;
            end else if (remaining != 0) begin
                remaining <= remaining - 1'b1;
                if (remaining == 1) begin active <= 0; timed_out <= 1; end
            end
        end
        stereo_expression_gain #(.SAMPLE_W(SAMPLE_W)) u_gain (
            .clk_i(clk_i),.rst_ni(rst_ni),.in_valid_i(in_valid_i),
            .left_i(in_left_i),.right_i(in_right_i),.gain_q_i(sample_gain),
            .mute_i(1'b0),.bypass_i(!in_sensor_owned_i),
            .out_valid_o(out_valid_o),.left_o(out_left_o),.right_o(out_right_o));
        always @(posedge clk_i) begin
            if (!rst_ni) begin tag_product <= 0; tag_result <= 0; end
            else begin tag_product <= in_tag_i; tag_result <= tag_product; end
        end
        assign out_tag_o = tag_result;
    end else begin : g_disabled
        // The feature-off build preserves the exact existing latency and PCM.
        assign out_valid_o = in_valid_i;
        assign out_tag_o = in_tag_i;
        assign out_left_o = in_left_i;
        assign out_right_o = in_right_i;
        assign alive_o = 1'b0;
        assign timed_out_o = 1'b0;
    end endgenerate
endmodule
`default_nettype wire
