`default_nettype none
// Interface bound to official Spectrum_Sample_RAM.ipc and instantiation template.
// Bypass is a synchronous one-cycle read; reset clears output, never the array.
module spectrum_sample_ram (
    input  wire         clk_i,
    input  wire         rst_n_i,
    input  wire         wr_en_i,
    input  wire [10:0]  wr_addr_i,
    input  wire [15:0]  wr_data_i,
    input  wire         rd_en_i,
    input  wire [10:0]  rd_addr_i,
    output wire [15:0]  rd_data_o
);
    Spectrum_Sample_RAM u_ram (
        .dout(rd_data_o),
        .clka(clk_i),
        .cea(wr_en_i & rst_n_i),
        .clkb(clk_i),
        .ceb(rd_en_i | ~rst_n_i),
        .oce(1'b1),
        .reset(~rst_n_i),
        .ada(wr_addr_i),
        .din(wr_data_i),
        .adb(rd_addr_i)
    );
endmodule
`default_nettype wire
