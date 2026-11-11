// First-quadrant midpoint magnitudes; signs are restored outside the ROM.
`default_nettype none
module dds_improve_slope_rom #(
    parameter LUT_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/sine_midpoint_256x24.hex",
    parameter SLOPE_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/slope_midpoint_256x18q15.hex"
) (
    input wire clk,
    input wire [7:0] index,
    output logic [23:0] sine_magnitude,
    output logic [17:0] slope_magnitude
);
    reg [23:0] sine_lut [0:255];
    // Match HEX digit width; padding bits never enter the datapath.
    reg [19:0] slope_lut [0:255];
    initial begin
        $readmemh(LUT_FILE, sine_lut);
        $readmemh(SLOPE_FILE, slope_lut);
// synthesis translate_off
`ifndef SYNTHESIS
        for (int i = 0; i < 256; i++)
            assert (!slope_lut[i][17]) else $fatal(1, "slope ROM exceeds positive Q3.15 range");
`endif
// synthesis translate_on
    end
    // Keep ROM read registers reset-free for target-tool memory inference.
    always_ff @(posedge clk) begin
        sine_magnitude <= sine_lut[index];
        slope_magnitude <= slope_lut[index][17:0];
    end
endmodule
`default_nettype wire
