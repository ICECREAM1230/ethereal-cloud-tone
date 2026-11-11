// Copyright (c) 2026 ICECREAM1230
// Per-element stereo attenuation: registered decode, then multiply.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module stereo_gain #(
    parameter int IDXW = $clog2(synth_pkg::NUM_ELEMENTS),
    parameter bit FRACTIONAL_GAIN = 1'b0,
    parameter bit PREDECODED_GAIN = 1'b0
) (
    input wire clk, rst_n, in_valid,
    input wire [IDXW-1:0] in_index,
    input wire signed [17:0] sample_in,
    input wire [7:0] atten_l, atten_r,
    input wire [5:0] gain_fraction_l, gain_fraction_r,
    input wire [16:0] linear_gain_l, linear_gain_r,
    input element_types_pkg::element_state_t state_in,
    output logic out_valid,
    output logic [IDXW-1:0] out_index,
    output logic signed [17:0] sample_left, sample_right,
    output element_types_pkg::element_state_t state_out /* synthesis syn_srlstyle = "registers" */
);
    import element_types_pkg::*;
    // Match HEX digit width; padding bits never enter the datapath.
    reg [19:0] att_lut [0:15];
    initial $readmemh("rtl/synthesizers/subtractive/data/att_lut.hex", att_lut);
    logic decode_valid;
    logic [IDXW-1:0] decode_index;
    element_state_t decode_state /* synthesis syn_srlstyle = "registers" */;
    logic signed [17:0] decode_sample, decode_lin_l, decode_lin_r;
    logic signed [17:0] lin_l, lin_r;
    logic signed [34:0] product_l, product_r;

    function automatic logic [16:0] attenuation_node(input logic [7:0] code);
        attenuation_node = (code == 8'hff) ? 17'd0
            : att_lut[code[3:0]][16:0] >> code[7:4];
    endfunction

    generate if (PREDECODED_GAIN) begin : g_predecoded
        // The element filter used existing coefficient/context stages to
        // finish this Q16 gain before its long transport delay. Keep this
        // boundary a register-only capture before the audio DSP multiplier.
        always_comb begin
            lin_l = $signed({1'b0,linear_gain_l});
            lin_r = $signed({1'b0,linear_gain_r});
        end
    end else if (FRACTIONAL_GAIN) begin : g_fractional
        logic [16:0] lower_l, lower_r, upper_l, upper_r;
        logic [16:0] delta_l, delta_r;
        logic [17:0] step_l, step_r;
        // Interpolate the linear Q16 coefficients, rather than rounding the
        // logarithmic code first. Each original knot remains bit exact. The
        // frozen 16-entry table's largest adjacent-code delta is 2779, so
        // twelve slope bits and a six-bit fraction need only an 18-bit product.
        always_comb begin
            lower_l = attenuation_node(atten_l);
            lower_r = attenuation_node(atten_r);
            upper_l = attenuation_node(atten_l == 8'h00 ? 8'h00 : atten_l - 8'd1);
            upper_r = attenuation_node(atten_r == 8'h00 ? 8'h00 : atten_r - 8'd1);
            delta_l = upper_l - lower_l;
            delta_r = upper_r - lower_r;
            step_l = delta_l[11:0] * gain_fraction_l;
            step_r = delta_r[11:0] * gain_fraction_r;
            lin_l = (atten_l == 8'hff) ? 18'sd0
                : 18'({1'b0,lower_l}) + 18'(step_l >> 6);
            lin_r = (atten_r == 8'hff) ? 18'sd0
                : 18'({1'b0,lower_r}) + 18'(step_r >> 6);
        end
    end else begin : g_integer
        // The default ignores optional fraction ports, including old callers
        // that leave them unconnected, and preserves the previous decode.
        always_comb begin
            lin_l = 18'({1'b0,attenuation_node(atten_l)});
            lin_r = 18'({1'b0,attenuation_node(atten_r)});
        end
    end endgenerate

    // 0xff is exact zero, including a muted/gated channel under positive
    // modulation. All other codes retain the original LUT/shift order.
    always_comb begin
        product_l = decode_sample * decode_lin_l;
        product_r = decode_sample * decode_lin_r;
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            decode_valid <= 1'b0; decode_index <= '0; decode_state <= '0;
            decode_sample <= '0; decode_lin_l <= '0; decode_lin_r <= '0;
            out_valid <= 1'b0; out_index <= '0; state_out <= '0;
            sample_left <= '0; sample_right <= '0;
        end else begin
            decode_valid <= in_valid; decode_index <= in_index;
            decode_state <= state_in; decode_sample <= sample_in;
            decode_lin_l <= lin_l; decode_lin_r <= lin_r;
            out_valid <= decode_valid; out_index <= decode_index;
            state_out <= decode_state;
            sample_left <= 18'(product_l >>> 16);
            sample_right <= 18'(product_r >>> 16);
        end
    end
endmodule
