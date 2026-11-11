// Shared reciprocal preparation from the ACTUAL clamped phase step.
// 256 linear segments of normalized 1/m, m in [1,2), not a pitch table.
// Packed 54-bit word = {unsigned Q30 base[30:0], signed Q30 delta[22:0]}.
// C1 normalized synchronous ROM read; C2 rounded product; C3 interpolation;
// C4 rescale/reconstruct. Four registered stages, II=1.
// R approximates nearest(2^40/step).
`default_nettype none
module dds_antialias_reciprocal_rom #(
    parameter RECIPROCAL_FILE = "rtl/synthesizers/common/oscillator/improved/antialias/rom/reciprocal_normalized_256x54.hex"
) (
    input wire clk, rst_n, en,
    input wire [23:0] step,
    output wire out_valid,
    output logic [29:0] reciprocal
);
    // Match HEX digit width; padding bits never enter the datapath.
    reg [55:0] reciprocal_lut [0:255];
    initial $readmemh(RECIPROCAL_FILE, reciprocal_lut);
    logic [4:0] exponent;
    logic [22:0] normalized_fraction; // normalized leading one is implicit
    logic [3:0] valid_pipe;
    logic [53:0] coefficients_c1;
    logic [14:0] fraction_c1;
    logic [3:0] shift_c1, shift_c2, shift_c3;
    logic [30:0] base_c2;
    wire signed [47:0] interpolation_mac_c2;
    wire signed [38:0] interpolation_c2 = interpolation_mac_c2[38:0];
    // The product's rounding constant is applied in C2. Its upper 24 bits
    // are the same arithmetic >>>15 result used by the original C3.
    wire signed [23:0] rounded_delta = $signed(interpolation_c2[38:15]);
    wire [30:0] interpolated_c2 = base_c2 + 31'(rounded_delta);
    logic [30:0] interpolated_c3;
    wire [30:0] rescaled = interpolated_c3 >> shift_c3;
    // For positive interpolation, add-half-before-shift equals incrementing
    // the shifted result by its guard bit. Avoid a 40-bit variable-round
    // adder before the barrel shifter; retain nearest/ties-up rounding.
    wire reconstruction_guard = shift_c3 == 0 ? 1'b0 :
        interpolated_c3[{1'b0, shift_c3} - 5'd1];
    wire [31:0] reconstructed = {1'b0, rescaled} + 32'(reconstruction_guard);
    assign out_valid = valid_pipe[3];

    // Preserve the C2 capture edge, CE and asynchronous clear in the DSP.
    // Its 48-bit ALU applies the rounding bias before the existing 39-bit slice.
    synth_mac27x18_oreg u_interpolation_mac (
        .dout(interpolation_mac_c2),
        .a({{4{coefficients_c1[22]}}, coefficients_c1[22:0]}),
        .b({3'b000, fraction_c1}), .c(48'sd16384),
        .clk(clk), .ce(valid_pipe[0]), .reset(~rst_n)
    );

    // Balanced highest-one search: the previous increasing-index overwrite
    // loop formed a long priority chain before the normalization shifter.
    // Zero retains exponent 0, and every nonzero 24-bit input is unchanged.
    wire exponent_16 = |step[23:16];
    wire [15:0] exponent_half = exponent_16 ? {8'd0, step[23:16]} : step[15:0];
    wire exponent_8 = |exponent_half[15:8];
    wire [7:0] exponent_octet = exponent_8 ? exponent_half[15:8] : exponent_half[7:0];
    wire exponent_4 = |exponent_octet[7:4];
    wire [3:0] exponent_nibble = exponent_4 ? exponent_octet[7:4] : exponent_octet[3:0];
    wire exponent_2 = |exponent_nibble[3:2];
    wire [1:0] exponent_pair = exponent_2 ? exponent_nibble[3:2] : exponent_nibble[1:0];
    always_comb begin
        exponent = {exponent_16, exponent_8, exponent_4, exponent_2, exponent_pair[1]};
        normalized_fraction = 23'(step << (5'd23 - exponent));
    end
    // No reset on the synchronous ROM output: preserve memory inference.
    always_ff @(posedge clk) begin
        if (en && step != 0) coefficients_c1 <= reciprocal_lut[normalized_fraction[22:15]][53:0];
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_pipe <= '0; fraction_c1 <= '0; shift_c1 <= '0;
            shift_c2 <= '0; shift_c3 <= '0; base_c2 <= '0;
            interpolated_c3 <= '0; reciprocal <= '0;
        end else begin
            valid_pipe <= {valid_pipe[2:0], en && step != 0};
            // C1: normalize the integer step before extracting segment/fraction.
            if (en && step != 0) begin
                fraction_c1 <= normalized_fraction[14:0];
                shift_c1 <= 4'(exponent - 5'd10);
            end
            // C2: signed 23 x unsigned 15 interpolation, with sign-extension.
            if (valid_pipe[0]) begin
                base_c2 <= coefficients_c1[53:23];
                shift_c2 <= shift_c1;
            end
            // C3/C4: register the interpolation sum before the variable
            // rescale, preserving the exact nearest/ties-up result.
            if (valid_pipe[1]) begin
                interpolated_c3 <= interpolated_c2;
                shift_c3 <= shift_c2;
            end
            if (valid_pipe[2]) reciprocal <= reconstructed[29:0];
// synthesis translate_off
`ifndef SYNTHESIS
            if (rst_n === 1'b1 && en && step != 0)
                assert (step >= 24'd1428 && step <= 24'h7fffff)
                    else $fatal(1, "Reciprocal step outside actual clamped decoder domain");
            if (rst_n === 1'b1 && valid_pipe[1])
                assert (interpolated_c2 > 0)
                    else $fatal(1, "Reciprocal interpolation is not positive");
            if (rst_n === 1'b1 && valid_pipe[2])
                assert (reconstructed < 32'h40000000)
                    else $fatal(1, "Reciprocal reconstruction exceeds unsigned 30-bit range");
`endif
// synthesis translate_on
        end
    end
endmodule
`default_nettype wire
