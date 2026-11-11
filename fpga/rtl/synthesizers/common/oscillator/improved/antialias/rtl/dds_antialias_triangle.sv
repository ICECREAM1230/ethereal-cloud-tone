// Copyright (c) 2026 ICECREAM1230
// Exact original triangle arithmetic, including 24-bit intermediate wraps.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>; MIT.
// Kept separate from saw/PWM; no new triangle antialias/integrator algorithm.
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module dds_antialias_triangle (
    input wire clk, rst_n, in_valid,
    input wire signed [23:0] phase_in,
    output wire out_valid,
    output wire signed [17:0] sample_out
);
    import dds_antialias_pkg::*;
    wire signed [23:0] triangle = (phase_in < 24'h800000)
        ? (phase_in << 1) - 24'h800000
        : 24'h7fffff - ((phase_in - 24'h800000) << 1);
    logic [WAVE_LATENCY-1:0] valid_pipe;
    logic signed [17:0] sample_pipe [0:WAVE_LATENCY-1] /* synthesis syn_srlstyle = "registers" */;
    assign out_valid = valid_pipe[WAVE_LATENCY-1];
    assign sample_out = sample_pipe[WAVE_LATENCY-1];
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_pipe <= '0;
            for (int i=0; i<WAVE_LATENCY; i++) sample_pipe[i] <= '0;
        end else begin
            valid_pipe <= {valid_pipe[WAVE_LATENCY-2:0], in_valid};
            sample_pipe[0] <= 18'($signed(triangle) >>> 8);
            for (int i=1; i<WAVE_LATENCY; i++) sample_pipe[i] <= sample_pipe[i-1];
        end
    end
endmodule
`default_nettype wire
