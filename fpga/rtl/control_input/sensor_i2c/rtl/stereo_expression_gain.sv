`timescale 1ns/1ps

// Two registered stages: capture products, then symmetrically round the PCM.
module stereo_expression_gain #(
    parameter integer SAMPLE_W = 24,
    parameter integer GAIN_FRAC_BITS = 16
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic in_valid_i,
    input  logic signed [SAMPLE_W-1:0] left_i,
    input  logic signed [SAMPLE_W-1:0] right_i,
    input  logic [GAIN_FRAC_BITS:0] gain_q_i,
    input  logic mute_i,
    input  logic bypass_i,
    output logic out_valid_o,
    output logic signed [SAMPLE_W-1:0] left_o,
    output logic signed [SAMPLE_W-1:0] right_o
);
    localparam integer PRODUCT_W = SAMPLE_W + GAIN_FRAC_BITS + 2;
    localparam logic [GAIN_FRAC_BITS:0] UNITY = {1'b1, {GAIN_FRAC_BITS{1'b0}}};
    localparam logic signed [PRODUCT_W-1:0] ROUND_HALF = GAIN_FRAC_BITS > 0
        ? (PRODUCT_W'(1) << (GAIN_FRAC_BITS - 1)) : '0;

    logic [GAIN_FRAC_BITS:0] effective_gain;
    logic signed [GAIN_FRAC_BITS+1:0] signed_gain;
    logic signed [PRODUCT_W-1:0] left_product_q;
    logic signed [PRODUCT_W-1:0] right_product_q;
    logic product_valid_q;

    initial begin
        if (SAMPLE_W < 1) $fatal(1, "stereo_expression_gain SAMPLE_W must be positive");
        if (GAIN_FRAC_BITS < 0) $fatal(1, "stereo_expression_gain GAIN_FRAC_BITS must be nonnegative");
    end

    always_comb begin
        if (mute_i) effective_gain = '0;
        else if (bypass_i || gain_q_i[GAIN_FRAC_BITS]) effective_gain = UNITY;
        else effective_gain = gain_q_i;
        signed_gain = $signed({1'b0, effective_gain});
    end

    // The extra product bits make negation safe for the full negative PCM
    // sample. Gain <= unity guarantees the rounded result fits SAMPLE_W.
    function automatic logic signed [SAMPLE_W-1:0] round_sample(
        input logic signed [PRODUCT_W-1:0] product
    );
        if (product < 0)
            round_sample = SAMPLE_W'(-((-product + ROUND_HALF) >>> GAIN_FRAC_BITS));
        else
            round_sample = SAMPLE_W'((product + ROUND_HALF) >>> GAIN_FRAC_BITS);
    endfunction

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            product_valid_q <= 1'b0;
            left_product_q <= '0;
            right_product_q <= '0;
            out_valid_o <= 1'b0;
            left_o <= '0;
            right_o <= '0;
        end else begin
            product_valid_q <= in_valid_i;
            out_valid_o <= product_valid_q;
            if (in_valid_i) begin
                left_product_q <= left_i * signed_gain;
                right_product_q <= right_i * signed_gain;
            end
            if (product_valid_q) begin
                left_o <= round_sample(left_product_q);
                right_o <= round_sample(right_product_q);
            end
        end
    end
endmodule
