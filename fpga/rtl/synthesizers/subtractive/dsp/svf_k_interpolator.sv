// Copyright (c) 2026 ICECREAM1230
// Three-stage prewarped SVF K lookup: packed ROM, delta*frac, rounded add.
// 1024 stride-16 entries retain the complete 14-bit cutoff encoding.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module svf_k_interpolator (
    input wire clk, rst_n, in_valid,
    input wire [13:0] in_code,
    output logic out_valid,
    output logic [28:0] out_k /* synthesis syn_srlstyle = "registers" */
);
    localparam int ENTRIES = 1024;
    localparam logic [9:0] ENDPOINT_INDEX = 10'((int'(synth_pkg::FC_MAX) + 15) / 16);
    // One word supplies both operands of a segment. Endpoint/plateau
    // entries retain FC_MAX exactly, including a future non-knot ceiling.
    // Match HEX digit width; padding bits never enter the datapath.
    reg [51:0] coefficient_rom [0:ENTRIES-1];
    initial $readmemh("rtl/synthesizers/subtractive/data/svf_k_compact.hex", coefficient_rom);
    wire at_ceiling = in_code >= synth_pkg::FC_MAX;
    wire [9:0] rom_address = at_ceiling ? ENDPOINT_INDEX : in_code[13:4];
    wire [3:0] fraction = at_ceiling ? 4'd0 : in_code[3:0];
    logic s1_valid, s2_valid;
    logic [50:0] s1_segment;
    logic [3:0] s1_fraction;
    logic [28:0] s2_base;
    logic [25:0] s2_product;
    wire [26:0] rounded_product = {1'b0, s2_product} + 27'd8;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s1_valid <= 1'b0; s2_valid <= 1'b0; out_valid <= 1'b0;
            s1_segment <= '0; s1_fraction <= '0;
            s2_base <= '0; s2_product <= '0; out_k <= '0;
        end else begin
            s1_valid <= in_valid;
            s1_segment <= coefficient_rom[rom_address][50:0];
            s1_fraction <= fraction;
            s2_valid <= s1_valid;
            s2_base <= s1_segment[50:22];
            s2_product <= s1_segment[21:0] * s1_fraction;
            out_valid <= s2_valid;
            out_k <= s2_base + {6'd0, rounded_product[26:4]};
        end
    end
endmodule
