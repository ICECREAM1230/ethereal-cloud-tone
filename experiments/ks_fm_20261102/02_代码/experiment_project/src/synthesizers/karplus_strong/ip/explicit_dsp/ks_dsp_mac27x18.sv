// Thin adapter to the user's unmodified Gowin-generated combinational IP.
// A is signed 26 bits; PSEL enables its signed 27-bit preadd result.
// Existing KS registers retain all clock enables, reset and cancellation.
`default_nettype none
module ks_dsp_mac27x18 #(
    parameter bit PREADD = 1'b0,
    parameter bit PRESUB = 1'b0,
    parameter bit DYNAMIC_PRESUB = 1'b0
) (
    input wire signed [25:0] a_i,
    input wire signed [17:0] b_i,
    input wire signed [25:0] d_i,
    input wire signed [47:0] c_i,
    input wire presub_i,
    output wire signed [47:0] y_o
);
    synth_mac27x18_comb u_generated_mac (
        .dout(y_o), .a(a_i), .b(b_i), .c(c_i), .d(d_i),
        .psel(PREADD), .paddsub(DYNAMIC_PRESUB ? presub_i : PRESUB)
    );
endmodule
`default_nettype wire
