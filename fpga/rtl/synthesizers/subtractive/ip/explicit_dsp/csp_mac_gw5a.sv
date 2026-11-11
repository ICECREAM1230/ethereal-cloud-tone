// Copyright (c) 2026 ICECREAM1230
// CSP X->A registered operands and A-stage exact multiply-add.
// SPDX-License-Identifier: MIT
// Uses the unmodified user-generated official Gowin IP in src/DSP_ip/.
`default_nettype none
module csp_mac_gw5a (
    input wire clk, rst_n,
    input wire signed [17:0] operand, depth,
    input wire multiply_enable,
    input wire signed [17:0] accumulator,
    output wire signed [20:0] sum
);
    // AREG/BREG capture at the old a_product edge. C remains combinational
    // so the existing A-stage forwarding selects the newest accumulator.
    // B=2**16 implements the old non-multiply path operand << 16.
    wire signed [17:0] factor = multiply_enable ? depth : 18'sd65536;
    wire [47:0] mac_result;
    synth_mac27x18_abreg u_ip (
        .dout(mac_result), .a({{9{operand[17]}},operand}), .b(factor),
        .c({{14{accumulator[17]}},accumulator,16'd0}),
        .clk(clk), .ce(1'b1), .reset(~rst_n)
    );
    // C's low 16 bits are zero: slicing after addition equals slicing the
    // signed product then adding, including negative fractional products.
    // Keep the original 21-bit sum and caller's saturation/forwarding order.
    assign sum = mac_result[36:16];
endmodule
`default_nettype wire
