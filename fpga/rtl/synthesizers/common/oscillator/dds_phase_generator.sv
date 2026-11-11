// Copyright (c) 2026 ICECREAM1230
// DDS pitch lookup and one modulo-2^24 phase advance (S3 / S3B).
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module dds_phase_generator #(
    parameter bit SUPPORT_DIRECT_STEP = 1'b0
) (
    input wire clk, rst_n, in_valid,
    input wire [13:0] pitch_code,
    input wire signed [23:0] phase_in,
    input wire direct_step_enable,
    input wire [23:0] direct_step,
    output logic out_valid,
    output logic signed [23:0] phase_out
);
    reg [23:0] phase_lut [0:1023];
    initial $readmemh("rtl/synthesizers/common/oscillator/data/phase_lut.hex", phase_lut);

    logic lookup_valid;
    logic [23:0] delta_lut;
    logic [3:0] pitch_oct;
    logic signed [23:0] prior_phase;
    logic [27:0] delta_scaled;
    logic [23:0] delta;
    logic direct_q;
    logic [23:0] direct_step_q;

    // Preserve all LUT << 4 bits before the Nyquist clamp, including
    // pitch octaves above 11. Phase is unsigned modulo arithmetic even
    // though the waveform core views the same 24 bits as signed.
    always_comb begin
        if (pitch_oct <= 4'd11)
            delta_scaled = {4'd0, delta_lut} >> (4'd11 - pitch_oct);
        else
            delta_scaled = {4'd0, delta_lut} << (pitch_oct - 4'd11);
        delta = delta_scaled >= 28'h0800000 ? 24'h7fffff : delta_scaled[23:0];
        if (SUPPORT_DIRECT_STEP && direct_q)
            delta = direct_step_q;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            lookup_valid <= 1'b0;
            delta_lut <= '0;
            pitch_oct <= '0;
            prior_phase <= '0;
            direct_q <= 1'b0;
            direct_step_q <= '0;
            out_valid <= 1'b0;
            phase_out <= '0;
        end else begin
            lookup_valid <= in_valid;
            delta_lut <= phase_lut[pitch_code[9:0]];
            pitch_oct <= pitch_code[13:10];
            prior_phase <= phase_in;
            direct_q <= SUPPORT_DIRECT_STEP && direct_step_enable;
            direct_step_q <= direct_step;
            out_valid <= lookup_valid;
            phase_out <= $unsigned(prior_phase) + delta;
        end
    end
endmodule
