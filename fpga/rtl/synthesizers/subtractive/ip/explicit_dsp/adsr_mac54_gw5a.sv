// Copyright (c) 2026 ICECREAM1230
// Exact 27s x 18u + 54-bit addend using user-generated official Arora V IP.
// SPDX-License-Identifier: MIT
// Generated modules remain unmodified in src/DSP_ip/gowin_multalu/.
`default_nettype none
module adsr_mac54_gw5a #(
    parameter int K_SHIFT = 24
) (
    input  wire clk,
    input  wire rst_n,
    input  wire signed [26:0] delta,
    input  wire [17:0] coefficient,
    input  wire [25:0] level,
    output wire signed [53:0] result
);
    // delta = unsigned(delta[16:0]) + signed(delta[26:17]) * 2**17.
    // Split the addend at the same bit. Both low operands are nonnegative:
    // L = k*delta_lo + addend_lo; T = k*delta_hi + addend_hi;
    // H = T + (L >> 17). Concatenating H with L[16:0] restores the
    // original expression modulo 2**54, including all negative products.
    // The third IP adds through its normal A/B/C ports; CASI is disabled.
    wire [53:0] addend = {{(54-26-K_SHIFT){1'b0}},level,{K_SHIFT{1'b0}}};
    // comb's generated 26-bit A is sign-extended internally to 27 bits.
    // Zero extension here preserves the coefficient's unsigned bit 17.
    wire [25:0] k_positive = {8'd0,coefficient};
    wire [17:0] delta_low = {1'b0,delta[16:0]};
    wire [17:0] delta_high = {{8{delta[26]}},delta[26:17]};
    wire [47:0] low_sum, high_partial, high_sum;
    logic [16:0] low_q;

    synth_mac27x18_comb u_low (
        .dout(low_sum), .a(k_positive), .b(delta_low),
        .c({31'd0,addend[16:0]}), .d(26'd0), .psel(1'b0), .paddsub(1'b0)
    );
    synth_mac27x18_comb u_high (
        .dout(high_partial), .a(k_positive), .b(delta_high),
        .c({11'd0,addend[53:17]}), .d(26'd0), .psel(1'b0), .paddsub(1'b0)
    );
    // L is nonnegative and below 2**35, so this carry fits 18 positive
    // bits. Use its zero-extended low 18 bits as A, never as arbitrary CASI.
    synth_mac27x18_oreg u_combine (
        .dout(high_sum), .a({9'd0,low_sum[34:17]}), .b(18'sd1),
        .c(high_partial),
        .clk(clk), .ce(1'b1), .reset(~rst_n)
    );
    always_ff @(posedge clk or negedge rst_n)
        if (!rst_n) low_q <= 17'd0;
        else low_q <= low_sum[16:0];
    assign result = {high_sum[36:0],low_q};
endmodule
`default_nettype wire
