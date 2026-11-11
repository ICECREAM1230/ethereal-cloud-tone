// Copyright (c) 2026 ICECREAM1230
// Streaming DDS: pitch/phase -> registered phase -> registered waveform.
// The per-element accumulator state is supplied by element_state_store.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module dds_oscillator (
    input wire clk, rst_n, in_valid,
    input wire [13:0] pitch_code,
    input wire signed [23:0] phase_in, duty,
    input wire [1:0] wave,
    output logic out_valid,
    output logic signed [23:0] phase_out,
    output logic signed [17:0] sample_out
);
    wire phase_valid;
    wire signed [23:0] advanced_phase;
    logic signed [23:0] lookup_duty, phase_duty;
    logic [1:0] lookup_wave, phase_wave;
    wire signed [17:0] waveform;

    dds_phase_generator u_phase (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid),
        .pitch_code(pitch_code), .phase_in(phase_in),
        .direct_step_enable(1'b0),.direct_step(24'd0),
        .out_valid(phase_valid), .phase_out(advanced_phase)
    );
    // Reuse the existing sine table, saw, triangle and PWM arithmetic.
    osc_core u_osc (
        .phase_next(advanced_phase), .duty(phase_duty),
        .wave(phase_wave), .sample_out(waveform)
    );
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            lookup_duty <= '0; phase_duty <= '0;
            lookup_wave <= '0; phase_wave <= '0;
            out_valid <= 1'b0; phase_out <= '0; sample_out <= '0;
        end else begin
            lookup_duty <= duty; phase_duty <= lookup_duty;
            lookup_wave <= wave; phase_wave <= lookup_wave;
            out_valid <= phase_valid;
            phase_out <= advanced_phase;
            sample_out <= waveform;
        end
    end
endmodule
