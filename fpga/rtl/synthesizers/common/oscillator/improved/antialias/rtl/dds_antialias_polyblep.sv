// Fixed-point adaptation of DaisySP Polyblep(), pinned commit 2c72eaf.
// DaisySP copyright (c) 2020 Electrosmith, Corp.; MIT, see licenses/upstream/antialias/LICENSE.DaisySP (FPGA root).
// Pure per-token edge correction; no oscillator/integrator state.
// edge_phase and step are unsigned fractions of a full 2^24 phase cycle.
// Requires actual step 1428..2^23-1 and the shared normalized reciprocal.
// reciprocal arrives after C4; all edge operators consume that shared R at C5.
// Eight registered stages, initiation interval 1. Output has 48 fraction bits.
`default_nettype none
module dds_antialias_polyblep (
    input wire clk, rst_n, in_valid,
    input wire [23:0] edge_phase, step,
    input wire [29:0] reciprocal,
    output wire out_valid,
    output logic signed [50:0] correction
);
    logic [7:0] valid_pipe;
    logic [3:0] active_pipe;
    logic [5:0] positive_pipe;
    logic [23:0] phase_c1, step_c1, distance_c2, distance_c3, distance_c4;
    logic [53:0] product_c5;
    logic [24:0] residual_c6;
    logic [49:0] square_c7;
    wire early_edge = step_c1 != 0 && phase_c1 < step_c1;
    wire [24:0] late_window_sum = {1'b0, phase_c1} + {1'b0, step_c1};
    wire late_edge = step_c1 != 0 && late_window_sum[24] && (|late_window_sum[23:0]);
    // Modulo distance fits 24 bits whenever the late-edge window is active.
    wire [23:0] late_distance = 24'd0 - phase_c1;
    assign out_valid = valid_pipe[7];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_pipe <= '0; active_pipe <= '0; positive_pipe <= '0;
            phase_c1 <= '0; step_c1 <= '0; distance_c2 <= '0; distance_c3 <= '0;
            distance_c4 <= '0; product_c5 <= '0; residual_c6 <= '0;
            square_c7 <= '0; correction <= '0;
        end else begin
            valid_pipe <= {valid_pipe[6:0], in_valid};
            active_pipe <= {active_pipe[2:0], early_edge || late_edge};
            positive_pipe <= {positive_pipe[4:0], late_edge};
            // C1: isolate PWM's phase-duty subtraction from window selection.
            if (in_valid) begin
                phase_c1 <= edge_phase; step_c1 <= step;
            end
            // C2: edge window and distance; early B negative, late B positive.
            if (valid_pipe[0]) begin
                distance_c2 <= early_edge ? phase_c1 : late_edge ? late_distance : 24'd0;
            end
            // C3/C4: keep edge distance aligned with the shared reciprocal C4.
            if (valid_pipe[1]) distance_c3 <= distance_c2;
            if (valid_pipe[2]) distance_c4 <= distance_c3;
            // C5: complete 24x30 distance/reciprocal product. The single
            // shared preparation supplies R for this token without per-edge ROMs.
            if (valid_pipe[3]) product_c5 <= distance_c4 * reciprocal;
            // C6: u = 1 - distance/step in Q1.24. Clamp a rounded-up
            // reciprocal before subtraction so unsigned u cannot underflow.
            if (valid_pipe[4]) begin
                if (!active_pipe[3] || product_c5 >= 54'd1099511627776)
                    residual_c6 <= '0;
                else residual_c6 <= 25'h1000000 - {1'b0, product_c5[39:16]};
            end
            // C7: full 25x25 square, including the exactly-unity endpoint.
            if (valid_pipe[5]) square_c7 <= residual_c6 * residual_c6;
            // C8: restore sign directly at the output register.
            if (valid_pipe[6]) correction <= positive_pipe[5]
                ? $signed({1'b0, square_c7}) : -$signed({1'b0, square_c7});
        end
    end
endmodule
`default_nettype wire
