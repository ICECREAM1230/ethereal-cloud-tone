// Independent candidate. Original DDS public ports, 11 registered stages,
// one accepted element per clock, no backpressure or owned element RAM.
// Shared phase/pitch preparation; wave implementations remain separate.
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module dds_antialias_oscillator #(
    parameter bit SUPPORT_DIRECT_STEP = 1'b0,
    parameter LUT_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/sine_midpoint_256x24.hex",
    parameter SLOPE_FILE = "rtl/synthesizers/common/oscillator/improved/rom/precomputed_256x18q15/slope_midpoint_256x18q15.hex",
    parameter RECIPROCAL_FILE = "rtl/synthesizers/common/oscillator/improved/antialias/rom/reciprocal_normalized_256x54.hex"
) (
    input wire clk, rst_n, in_valid,
    input wire [13:0] pitch_code,
    input wire signed [23:0] phase_in, duty,
    input wire [1:0] wave,
    input wire direct_step_enable,
    input wire [23:0] direct_step,
    output wire out_valid,
    output wire signed [23:0] phase_out,
    output logic signed [17:0] sample_out
);
    import dds_antialias_pkg::*;
    wire phase_valid;
    wire signed [23:0] advanced_phase;
    wire [29:0] shared_reciprocal;
    wire reciprocal_valid;
    logic signed [23:0] duty_pipe [0:PHASE_LATENCY-1];
    logic [1:0] input_wave_pipe [0:PHASE_LATENCY-1];
    wire step_valid;
    wire signed [23:0] decoded_step;
    wire [23:0] step = $unsigned(decoded_step);
    wire [1:0] phase_wave = input_wave_pipe[PHASE_LATENCY-1];
    wire signed [17:0] saw_sample, pwm_sample, triangle_sample, sine_sample;
    wire saw_valid, pwm_valid, triangle_valid, sine_valid;
    logic [WAVE_LATENCY-1:0] valid_pipe;
    logic signed [23:0] phase_pipe [0:WAVE_LATENCY-1] /* synthesis syn_srlstyle = "registers" */;
    logic [1:0] wave_pipe [0:WAVE_LATENCY-1];
// synthesis translate_off
`ifndef SYNTHESIS
    initial begin
        assert (OSCILLATOR_LATENCY == dds_improve_pkg::OSCILLATOR_LATENCY)
            else $fatal(1, "Candidate and active DDS latency differ");
    end
`endif
// synthesis translate_on

    dds_phase_generator #(.SUPPORT_DIRECT_STEP(SUPPORT_DIRECT_STEP)) u_phase (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid), .pitch_code(pitch_code),
        .phase_in(phase_in), .direct_step_enable(direct_step_enable), .direct_step(direct_step),
        .out_valid(phase_valid), .phase_out(advanced_phase)
    );
    // Decode the actual clamped step in parallel with the real phase advance.
    // The same untouched decoder with zero prior phase produces delta only;
    // it owns no element state and does not advance the caller's phase twice.
    // This removes a 24-bit subtraction from the PWM edge-input path.
    dds_phase_generator #(.SUPPORT_DIRECT_STEP(SUPPORT_DIRECT_STEP)) u_step (
        .clk(clk), .rst_n(rst_n), .in_valid(in_valid), .pitch_code(pitch_code),
        .phase_in(24'sd0), .direct_step_enable(direct_step_enable), .direct_step(direct_step),
        .out_valid(step_valid), .phase_out(decoded_step)
    );
    // C1..C4: one normalized reciprocal preparation shared by all three
    // saw/PWM edge operators. Their registered distances consume R at C5.
    dds_antialias_reciprocal_rom #(.RECIPROCAL_FILE(RECIPROCAL_FILE)) u_reciprocal (
        .clk(clk), .rst_n(rst_n), .en(phase_valid && phase_wave < 2'd2), .step(step),
        .out_valid(reciprocal_valid), .reciprocal(shared_reciprocal)
    );
    dds_antialias_saw u_saw (
        .clk(clk), .rst_n(rst_n), .in_valid(phase_valid && phase_wave == 2'd0),
        .phase_in(advanced_phase), .step(step), .reciprocal(shared_reciprocal),
        .out_valid(saw_valid), .sample_out(saw_sample)
    );
    dds_antialias_pwm u_pwm (
        .clk(clk), .rst_n(rst_n), .in_valid(phase_valid && phase_wave == 2'd1),
        .phase_in(advanced_phase), .duty(duty_pipe[PHASE_LATENCY-1]),
        .step(step), .reciprocal(shared_reciprocal), .out_valid(pwm_valid), .sample_out(pwm_sample)
    );
    dds_antialias_triangle u_triangle (
        .clk(clk), .rst_n(rst_n), .in_valid(phase_valid && phase_wave == 2'd2),
        .phase_in(advanced_phase), .out_valid(triangle_valid), .sample_out(triangle_sample)
    );
    dds_antialias_sine #(.LUT_FILE(LUT_FILE), .SLOPE_FILE(SLOPE_FILE)) u_sine (
        .clk(clk), .rst_n(rst_n), .in_valid(phase_valid && phase_wave == 2'd3),
        .phase_in(advanced_phase), .out_valid(sine_valid), .sample_out(sine_sample)
    );
    assign out_valid = valid_pipe[WAVE_LATENCY-1];
    assign phase_out = phase_pipe[WAVE_LATENCY-1];
    always_comb begin
        case (wave_pipe[WAVE_LATENCY-1])
            2'd0: sample_out = saw_sample;
            2'd1: sample_out = pwm_sample;
            2'd2: sample_out = triangle_sample;
            default: sample_out = sine_sample;
        endcase
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_pipe <= '0;
            for (int i=0; i<PHASE_LATENCY; i++) begin
                duty_pipe[i] <= '0; input_wave_pipe[i] <= '0;
            end
            for (int i=0; i<WAVE_LATENCY; i++) begin
                phase_pipe[i] <= '0; wave_pipe[i] <= '0;
            end
        end else begin
            duty_pipe[0] <= duty; input_wave_pipe[0] <= wave;
            for (int i=1; i<PHASE_LATENCY; i++) begin
                duty_pipe[i] <= duty_pipe[i-1];
                input_wave_pipe[i] <= input_wave_pipe[i-1];
            end
            valid_pipe <= {valid_pipe[WAVE_LATENCY-2:0], phase_valid};
            phase_pipe[0] <= advanced_phase; wave_pipe[0] <= phase_wave;
            for (int i=1; i<WAVE_LATENCY; i++) begin
                phase_pipe[i] <= phase_pipe[i-1]; wave_pipe[i] <= wave_pipe[i-1];
            end
// synthesis translate_off
`ifndef SYNTHESIS
            if (rst_n === 1'b1) assert (phase_valid == step_valid)
                else $fatal(1, "DDS phase and decoded step alignment");
            if (rst_n === 1'b1) assert (reciprocal_valid == (valid_pipe[3] && wave_pipe[3] < 2'd2))
                else $fatal(1, "DDS shared reciprocal C4 alignment");
            if (rst_n === 1'b1) assert ({sine_valid, triangle_valid, pwm_valid, saw_valid}
                == (out_valid ? (4'b0001 << wave_pipe[WAVE_LATENCY-1]) : 4'b0000))
                else $fatal(1, "DDS wave/phase/valid alignment");
`endif
// synthesis translate_on
        end
    end
endmodule
`default_nettype wire
