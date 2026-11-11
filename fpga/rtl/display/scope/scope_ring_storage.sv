`default_nettype none
// Scope display mapping consumes only signed PCM bits [23:13] in each lane.
// Keep the 64-bit seam and the official RAM's synchronous one-cycle read.
// Trigger detection still uses the original live 24-bit PCM in acquisition.
module scope_ring_storage (
    output wire [63:0] dout,
    input wire clka, cea, clkb, ceb, oce, reset,
    input wire [12:0] ada,
    input wire [63:0] din,
    input wire [12:0] adb
);
    wire [21:0] packed_din = {din[47:37], din[23:13]};
    wire [21:0] packed_dout;
    Scope_Ring_Compact_RAM22 u_ram (
        .dout(packed_dout), .clka(clka), .cea(cea), .clkb(clkb), .ceb(ceb),
        .oce(oce), .reset(reset), .ada(ada), .din(packed_din), .adb(adb)
    );
    assign dout = {16'd0, packed_dout[21:11], 13'd0,
                          packed_dout[10:0], 13'd0};
endmodule
`default_nettype wire
