// Separate candidate with the active dds_oscillator's pitch/phase/audio ports.
// Reuses the untouched project phase generator; it owns no per-element RAM.
// All waveforms, advanced phase and valid emerge after 11 registered stages.
`default_nettype none
module dds_improve_oscillator #(
    parameter LUT_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/sine_midpoint_256x24.hex",
    parameter SLOPE_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/slope_midpoint_256x18q15.hex"
) (
    input wire clk, rst_n, in_valid,
    input wire [13:0] pitch_code,
    input wire signed [23:0] phase_in, duty,
    input wire [1:0] wave,
    output wire out_valid,
    output wire signed [23:0] phase_out,
    output wire signed [17:0] sample_out
);
    import dds_improve_pkg::*;
    wire phase_valid;
    wire signed [23:0] advanced_phase;
    logic signed [23:0] lookup_duty, phase_duty;
    logic [1:0] lookup_wave, phase_wave;
    wire signed [17:0] corrected_sine, analytic_sample;
    logic signed [23:0] phase_pipe [0:SINE_LATENCY-1];
    logic signed [17:0] analytic_pipe [0:SINE_LATENCY-1];
    logic [1:0] wave_pipe [0:SINE_LATENCY-1];

    dds_phase_generator u_phase (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid),
        .pitch_code(pitch_code), .phase_in(phase_in),
        .direct_step_enable(1'b0),.direct_step(24'd0),
        .out_valid(phase_valid), .phase_out(advanced_phase)
    );
    dds_improve_slope_sine #(.LUT_FILE(LUT_FILE), .SLOPE_FILE(SLOPE_FILE)) u_sine (
        .clk(clk), .rst_n(rst_n), .in_valid(phase_valid),
        .phase_in(advanced_phase), .out_valid(out_valid), .sample_out(corrected_sine)
    );
    dds_improve_analytic_wave u_analytic (
        .phase_in(advanced_phase), .duty(phase_duty), .wave(phase_wave),
        .sample_out(analytic_sample)
    );
    assign phase_out = phase_pipe[SINE_LATENCY-1];
    assign sample_out = wave_pipe[SINE_LATENCY-1] == 2'd3
        ? corrected_sine : analytic_pipe[SINE_LATENCY-1];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            lookup_duty <= '0; phase_duty <= '0;
            lookup_wave <= '0; phase_wave <= '0;
            for (int i = 0; i < SINE_LATENCY; i++) begin
                phase_pipe[i] <= '0; analytic_pipe[i] <= '0; wave_pipe[i] <= '0;
            end
        end else begin
            lookup_duty <= duty; phase_duty <= lookup_duty;
            lookup_wave <= wave; phase_wave <= lookup_wave;
            phase_pipe[0] <= advanced_phase;
            analytic_pipe[0] <= analytic_sample;
            wave_pipe[0] <= phase_wave;
            for (int i = 1; i < SINE_LATENCY; i++) begin
                phase_pipe[i] <= phase_pipe[i-1];
                analytic_pipe[i] <= analytic_pipe[i-1];
                wave_pipe[i] <= wave_pipe[i-1];
            end
        end
    end
endmodule
`default_nettype wire
