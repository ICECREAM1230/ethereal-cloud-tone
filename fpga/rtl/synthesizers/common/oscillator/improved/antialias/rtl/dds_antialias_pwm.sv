// Copyright (c) 2026 ICECREAM1230
// DC-corrected PWM: preserve the project's comparator, duty subtraction,
// full 25-bit pulse and Q2.16 scale; correct BOTH edges with PolyBLEP.
// Original pulse formula: Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License for the original waveform formula: MIT.
// Edge polynomial adaptation: DaisySP MIT, see licenses/upstream/antialias/LICENSE.DaisySP (FPGA root).
// Nine registered stages, II=1. No persistent per-element state.
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module dds_antialias_pwm (
    input wire clk, rst_n, in_valid,
    input wire signed [23:0] phase_in, duty,
    input wire [23:0] step,
    // Shared R is aligned to edge stage C4, consumed by PolyBLEP at C5.
    input wire [29:0] reciprocal,
    output logic out_valid,
    output logic signed [17:0] sample_out
);
    import dds_antialias_pkg::*;
    wire rise_valid, fall_valid;
    wire signed [50:0] rise_blep, fall_blep;
    wire [23:0] falling_phase = $unsigned(phase_in) - $unsigned(duty);
    wire signed [24:0] pulse = ((phase_in < duty) ? 25'sd8388607 : -25'sd8388608)
                              - 25'(duty);
    logic signed [24:0] base_pipe [0:BLEP_LATENCY-1] /* synthesis syn_srlstyle = "registers" */;
    // Compute the exact carry from the low 33 bits without propagating a
    // carry through the entire 51-bit correction. The base has 25 zero LSBs.
    wire [8:0] low_sum = {1'b0, base_pipe[BLEP_LATENCY-1][7:0]} +
        {1'b0, rise_blep[32:25]};
    wire low_fraction_borrow = (rise_blep[24:12] < fall_blep[24:12]) ||
        ((rise_blep[24:12] == fall_blep[24:12]) &&
         (rise_blep[11:0] < fall_blep[11:0]));
    wire low_borrow = (low_sum[7:0] < fall_blep[32:25]) ||
        ((low_sum[7:0] == fall_blep[32:25]) && low_fraction_borrow);
    // A + B - C = A + B + ~C + 1. The low part contributes -1, 0 or +1;
    // adding its correction to that complement bias leaves an offset 0..2.
    wire [1:0] high_offset = 2'd1 + {1'b0, low_sum[8]} - {1'b0, low_borrow};
    wire [17:0] high_base =
        {base_pipe[BLEP_LATENCY-1][24], base_pipe[BLEP_LATENCY-1][24:8]};
    wire [17:0] high_rise = rise_blep[50:33];
    wire [17:0] high_fall_not = ~fall_blep[50:33];
    // Carry-save compression keeps the three signed high operands and the
    // small offset out of a cascade of wide carry-propagating adders.
    wire [17:0] high_sum_1 = high_base ^ high_rise ^ high_fall_not;
    wire [17:0] high_carry_1 = ((high_base & high_rise) |
        (high_base & high_fall_not) | (high_rise & high_fall_not)) << 1;
    wire [17:0] offset_bits = {16'd0, high_offset};
    wire [17:0] high_sum_2 = high_sum_1 ^ high_carry_1 ^ offset_bits;
    wire [17:0] high_carry_2 = ((high_sum_1 & high_carry_1) |
        (high_sum_1 & offset_bits) | (high_carry_1 & offset_bits)) << 1;
    wire [17:0] sample_next = high_sum_2 + high_carry_2;
    dds_antialias_polyblep u_rising_edge (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid),
        .edge_phase($unsigned(phase_in) ^ 24'h800000), .step(step),
        .reciprocal(reciprocal), .out_valid(rise_valid), .correction(rise_blep)
    );
    dds_antialias_polyblep u_falling_edge (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid),
        .edge_phase(falling_phase), .step(step), .reciprocal(reciprocal),
        .out_valid(fall_valid), .correction(fall_blep)
    );
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            out_valid <= 1'b0; sample_out <= '0;
            for (int i=0; i<BLEP_LATENCY; i++) base_pipe[i] <= '0;
        end else begin
            base_pipe[0] <= pulse;
            for (int i=1; i<BLEP_LATENCY; i++) base_pipe[i] <= base_pipe[i-1];
            out_valid <= rise_valid;
            if (rise_valid) sample_out <= sample_next;
// synthesis translate_off
`ifndef SYNTHESIS
            if (rst_n === 1'b1)
                assert (rise_valid == fall_valid) else $fatal(1, "PWM edge alignment");
`endif
// synthesis translate_on
        end
    end
endmodule
`default_nettype wire
