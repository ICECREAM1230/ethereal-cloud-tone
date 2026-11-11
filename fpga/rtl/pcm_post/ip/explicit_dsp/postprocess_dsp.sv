// Adapters for the unmodified, user-generated V1.9.12.04 Gowin DSP IP.
// IP configuration lives in src/DSP_ip; this file only extends operands,
// selects the configured operation and retains the original result widths.
`default_nettype none
module post_reverb_mult (
    input wire clk, rst_n, ce,
    input wire signed [32:0] a,
    input wire [15:0] b,
    output wire signed [49:0] product
);
    wire [62:0] full_product;
    synth_mul27x36_oreg u_mult (
        .a({11'd0,b}), .b({{3{a[32]}},a}),
        .clk(clk), .ce(ce), .reset(!rst_n), .dout(full_product)
    );
    assign product = full_product[49:0];
endmodule

module post_fm_product (
    input wire signed [15:0] head_a, head_b,
    input wire [15:0] phase,
    output wire [32:0] product
);
    wire [47:0] full_product;
    synth_mac27x18_comb u_mult (
        .a({{10{head_b[15]}},head_b}), .b({2'd0,phase}), .c(48'd0),
        .d({{10{head_a[15]}},head_a}), .psel(1'b1), .paddsub(1'b1),
        .dout(full_product)
    );
    // Move the endpoint subtraction into the DSP pre-adder. Taking abs after
    // the exact signed multiply equals abs(delta)*phase for unsigned phase.
    // Keep the separate original delta sign for the phase==0 case.
    assign product = full_product[47] ? -full_product[32:0] : full_product[32:0];
endmodule
`default_nettype wire
