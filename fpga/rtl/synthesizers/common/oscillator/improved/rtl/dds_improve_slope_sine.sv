// Fixed 24-bit phase / 256 midpoint / Q3.15 slope implementation.
// Nine registered stages preserve the existing candidate transport contract.
`default_nettype none
module dds_improve_slope_sine #(
    parameter LUT_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/sine_midpoint_256x24.hex",
    parameter SLOPE_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/slope_midpoint_256x18q15.hex"
) (
    input wire clk, rst_n, in_valid,
    input wire [23:0] phase_in,
    output wire out_valid,
    output logic signed [17:0] sample_out
);
    import dds_improve_pkg::*;
    logic [SINE_LATENCY-1:0] valid_pipe;
    logic [23:0] s1_phase;
    logic [7:0] s2_index;
    logic s2_sine_negative, s2_slope_negative, s3_sine_negative, s3_slope_negative;
    logic signed [14:0] s2_error, s3_error, s4_error, s5_error;
    wire [23:0] s3_sine_magnitude;
    wire [17:0] s3_slope_magnitude;
    logic signed [23:0] s4_sine, s5_sine;
    logic signed [17:0] s4_slope, s5_slope;
    logic signed [32:0] s6_product, s7_product;
    logic signed [39:0] s6_base, s7_base, s8_corrected;
    wire signed [39:0] correction = {{7{s7_product[32]}}, s7_product};
    wire signed [16:0] quantized = s8_corrected[39:23]; // F=15 plus audio shift=8
    dds_improve_slope_rom #(.LUT_FILE(LUT_FILE), .SLOPE_FILE(SLOPE_FILE)) u_rom (
        .clk(clk), .index(s2_index), .sine_magnitude(s3_sine_magnitude),
        .slope_magnitude(s3_slope_magnitude)
    );
    assign out_valid = valid_pipe[SINE_LATENCY-1];
`ifndef SYNTHESIS
    initial assert (SINE_LATENCY == 9) else $fatal(1, "fixed slope arithmetic needs nine stages");
`endif
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_pipe <= '0; s1_phase <= '0; s2_index <= '0;
            s2_sine_negative <= 0; s2_slope_negative <= 0;
            s3_sine_negative <= 0; s3_slope_negative <= 0;
            s2_error <= '0; s3_error <= '0; s4_error <= '0; s5_error <= '0;
            s4_sine <= '0; s5_sine <= '0; s4_slope <= '0; s5_slope <= '0;
            s6_product <= '0; s7_product <= '0;
            s6_base <= '0; s7_base <= '0; s8_corrected <= '0; sample_out <= '0;
        end else begin
            valid_pipe <= {valid_pipe[SINE_LATENCY-2:0], in_valid};
            if (in_valid) s1_phase <= phase_in;                       // S1
            s2_index <= s1_phase[22] ? ~s1_phase[21:14] : s1_phase[21:14];
            s2_sine_negative <= s1_phase[23];                        // S2
            s2_slope_negative <= s1_phase[23] ^ s1_phase[22];
            s2_error <= $signed({1'b0, s1_phase[13:0]}) - 15'sd8192;
            s3_sine_negative <= s2_sine_negative;                    // S3 synchronous ROM
            s3_slope_negative <= s2_slope_negative; s3_error <= s2_error;
            s4_sine <= s3_sine_negative ? -$signed(s3_sine_magnitude) : $signed(s3_sine_magnitude);
            s4_slope <= s3_slope_negative ? -$signed(s3_slope_magnitude) : $signed(s3_slope_magnitude);
            s4_error <= s3_error;                                    // S4 signs
            s5_sine <= s4_sine; s5_slope <= s4_slope; s5_error <= s4_error; // S5 multiply inputs
            s6_product <= s5_slope * s5_error;                       // S6 full 18x15
            s6_base <= {s5_sine[23], s5_sine, 15'b0};                 // 40-bit integer guard
            s7_product <= s6_product; s7_base <= s6_base;             // S7 multiply output
            s8_corrected <= s7_base + correction;                    // S8 wide sum
            if (quantized > 17'sd32767) sample_out <= 18'sd32767;      // S9 quantize/clamp
            else if (quantized < -17'sd32768) sample_out <= -18'sd32768;
            else sample_out <= {quantized[16], quantized};
        end
    end
endmodule
`default_nettype wire
