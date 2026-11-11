// Sine branch adapter: reuse the currently active slope sine unchanged.
// Isolates its ROM/configuration from the new saw/PWM implementations.
`default_nettype none
module dds_antialias_sine #(
    parameter LUT_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/sine_midpoint_256x24.hex",
    parameter SLOPE_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/slope_midpoint_256x18q15.hex"
) (
    input wire clk, rst_n, in_valid,
    input wire signed [23:0] phase_in,
    output wire out_valid,
    output wire signed [17:0] sample_out
);
    dds_improve_slope_sine #(.LUT_FILE(LUT_FILE), .SLOPE_FILE(SLOPE_FILE)) u_sine (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid), .phase_in(phase_in),
        .out_valid(out_valid), .sample_out(sample_out)
    );
endmodule
`default_nettype wire
