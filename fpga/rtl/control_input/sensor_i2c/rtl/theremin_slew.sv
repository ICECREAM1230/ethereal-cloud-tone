`timescale 1ns/1ps

// Signed, tick-enabled target follower. All controls are synchronous.
module theremin_slew #(
    parameter integer WIDTH = 18,
    parameter integer STEP = 1,
    parameter logic signed [WIDTH-1:0] RESET_VALUE = '0
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic clear_i,
    input  logic load_i,
    input  logic step_i,
    input  logic signed [WIDTH-1:0] target_i,
    output logic signed [WIDTH-1:0] value_o,
    output logic at_target_o
);
    // Keep the complete signed difference and a positive integer STEP, even
    // when STEP is larger than the entire representable value range.
    localparam integer DELTA_W = WIDTH < 32 ? 33 : WIDTH + 1;
    localparam logic signed [DELTA_W-1:0] STEP_EXT = DELTA_W'(STEP);
    logic signed [DELTA_W-1:0] delta;

    initial begin
        if (WIDTH < 1) $fatal(1, "theremin_slew WIDTH must be positive");
        if (STEP < 1) $fatal(1, "theremin_slew STEP must be positive");
    end

    assign delta = DELTA_W'(target_i) - DELTA_W'(value_o);
    assign at_target_o = value_o == target_i;

    always_ff @(posedge clk_i) begin
        if (!rst_ni || clear_i) value_o <= RESET_VALUE;
        else if (load_i) value_o <= target_i;
        else if (step_i) begin
            if (delta > STEP_EXT) value_o <= WIDTH'(DELTA_W'(value_o) + STEP_EXT);
            else if (delta < -STEP_EXT) value_o <= WIDTH'(DELTA_W'(value_o) - STEP_EXT);
            else value_o <= target_i;
        end
    end
endmodule
