// Copyright (c) 2026 ICECREAM1230
// Extracted from element_pipeline.sv without changing registered behavior.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module element_state_store #(
    parameter int NUM_ELEMENTS = synth_pkg::NUM_ELEMENTS
) (
    input wire clk,
    input wire [$clog2(NUM_ELEMENTS)-1:0] read_index,
    output element_types_pkg::element_state_t state_out,
    input wire write_enable,
    input wire [$clog2(NUM_ELEMENTS)-1:0] write_index,
    input element_types_pkg::element_state_t state_in
);
    reg signed [23:0] phase_ram  [0:NUM_ELEMENTS-1];
    reg signed [35:0] ic1eq1_ram [0:NUM_ELEMENTS-1];
    reg signed [35:0] ic2eq1_ram [0:NUM_ELEMENTS-1];
    reg signed [35:0] ic1eq2_ram [0:NUM_ELEMENTS-1];
    reg signed [35:0] ic2eq2_ram [0:NUM_ELEMENTS-1];

    // State RAMs start at zero (power-on init; also keeps X out of sim)
    integer i0;
    initial begin
        for (i0 = 0; i0 < NUM_ELEMENTS; i0 = i0 + 1) begin
            phase_ram[i0]  = '0;
            ic1eq1_ram[i0] = '0;
            ic2eq1_ram[i0] = '0;
            ic1eq2_ram[i0] = '0;
            ic2eq2_ram[i0] = '0;
        end
    end


    // Synchronous read port; runtime reset never clears state.
    always_ff @(posedge clk) begin
        state_out.phase  <= phase_ram[read_index];
        state_out.ic1a <= ic1eq1_ram[read_index];
        state_out.ic2a <= ic2eq1_ram[read_index];
        state_out.ic1b <= ic1eq2_ram[read_index];
        state_out.ic2b <= ic2eq2_ram[read_index];
    end

    // State writeback — sync-only process (BSRAM write port).
    // Writeback lands 33 clocks after lane acceptance (S1). Element
    // 255 writes at slot 288; all state/tag traffic drains before swap 512.
    always_ff @(posedge clk) begin
        if (write_enable) begin
            phase_ram[write_index]  <= state_in.phase;
            ic1eq1_ram[write_index] <= state_in.ic1a;
            ic2eq1_ram[write_index] <= state_in.ic2a;
            ic1eq2_ram[write_index] <= state_in.ic1b;
            ic2eq2_ram[write_index] <= state_in.ic2b;
        end
    end

endmodule
`default_nettype wire
