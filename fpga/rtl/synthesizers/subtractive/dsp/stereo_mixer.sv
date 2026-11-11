// Copyright (c) 2026 ICECREAM1230
// Extracted from element_pipeline.sv without changing registered behavior.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module stereo_mixer (
    input wire clk, rst_n, sample_tick, in_act,
    input wire signed [17:0] in_left, in_right,
    output logic signed [25:0] sum_left, sum_right
);
    // Same-edge consumers capture the pre-clear sums on sample_tick.
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sum_left <= '0;
            sum_right <= '0;
        end else if (sample_tick) begin
            sum_left <= '0;
            sum_right <= '0;
        end else if (in_act) begin
            sum_left <= sum_left + {{8{in_left[17]}}, in_left};
            sum_right <= sum_right + {{8{in_right[17]}}, in_right};
        end
    end
endmodule
`default_nettype wire
