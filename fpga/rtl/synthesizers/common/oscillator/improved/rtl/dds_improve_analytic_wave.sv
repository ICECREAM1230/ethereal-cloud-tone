// Copyright (c) 2026 ICECREAM1230
// Numerical saw/PWM/triangle rules preserved from the project's osc_core.sv.
// Original waveform code: Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module dds_improve_analytic_wave (
    input wire signed [23:0] phase_in, duty,
    input wire [1:0] wave,
    output wire signed [17:0] sample_out
);
    wire signed [23:0] saw = phase_in;
    wire signed [24:0] pulse =
        ((phase_in < duty) ? 25'sd8388607 : -25'sd8388608) - 25'(duty);
    wire signed [23:0] triangle = (phase_in < 24'h800000)
        ? (phase_in << 1) - 24'h800000
        : 24'h7fffff - ((phase_in - 24'h800000) << 1);
    logic signed [24:0] selected;
    always_comb begin
        case (wave)
            2'd0: selected = 25'(saw);
            2'd1: selected = pulse;
            2'd2: selected = 25'(triangle);
            default: selected = '0;
        endcase
    end
    assign sample_out = 18'(selected >>> 8);
endmodule
`default_nettype wire
