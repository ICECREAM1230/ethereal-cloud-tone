`timescale 1ns/1ps

// Hand-control lifetime and tick-rate smoothing. A separate engine owns voices.
module theremin_control_bridge #(
    parameter integer STALE_AUDIO_TICKS = 9600,
    parameter integer VOICE_READY_TIMEOUT_TICKS = 9600,
    parameter integer PITCH_STEP = 4,
    parameter integer GAIN_STEP = 256,
    parameter integer MUTE_CLOSE_Q16 = 64,
    parameter integer MUTE_OPEN_Q16 = 256
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic clear_i,
    input  logic enable_i,
    input  logic calibrated_i,
    input  logic sensor_fault_i,
    input  logic target_valid_i,
    input  logic [13:0] target_pitch_i,
    input  logic [13:0] reference_pitch_i,
    input  logic [16:0] target_gain_q16_i,
    input  logic audio_tick_i,
    input  logic voice_ready_i,
    output logic [13:0] pitch_code_o,
    output logic signed [17:0] pitch_delta_o,
    output logic [16:0] gain_q16_o,
    output logic voice_request_o,
    output logic voice_start_o,
    output logic voice_stop_o,
    output logic voice_gate_o,
    output logic active_o,
    output logic stale_o,
    output logic voice_fault_o,
    output logic controls_valid_o
);
    localparam integer AGE_W = STALE_AUDIO_TICKS <= 1 ? 1 : $clog2(STALE_AUDIO_TICKS);
    localparam integer READY_W = VOICE_READY_TIMEOUT_TICKS <= 1
        ? 1 : $clog2(VOICE_READY_TIMEOUT_TICKS);
    typedef enum logic [1:0] {OFF, REQUEST, PLAY, RELEASE} state_t;
    state_t state;
    logic healthy, accept_target, have_target, fresh_pending, expires_now, want_run;
    logic [AGE_W-1:0] age_ticks;
    logic [READY_W-1:0] ready_ticks;
    logic [13:0] cached_pitch, pitch_reference;
    logic [16:0] cached_gain, effective_gain;
    logic [13:0] effective_pitch;
    logic volume_open, pass_volume;
    logic gate_held;
    logic signed [17:0] pitch_target, pitch_value, gain_target, gain_value;
    logic unused_pitch_at_target, unused_gain_at_target;

    initial begin
        if (STALE_AUDIO_TICKS < 1 || VOICE_READY_TIMEOUT_TICKS < 1)
            $fatal(1, "Control deadlines must be positive audio tick counts");
        if (PITCH_STEP < 1 || GAIN_STEP < 1 || GAIN_STEP > 65536)
            $fatal(1, "PITCH_STEP must be positive and GAIN_STEP must be 1..65536");
        if (MUTE_CLOSE_Q16 < 0 || MUTE_CLOSE_Q16 >= MUTE_OPEN_Q16 || MUTE_OPEN_Q16 > 65536)
            $fatal(1, "Mute thresholds must satisfy 0 <= close < open <= 65536");
    end

    always_comb begin
        healthy = enable_i && calibrated_i && !sensor_fault_i && !voice_fault_o;
        accept_target = healthy && state != RELEASE && target_valid_i;
        effective_pitch = accept_target ? target_pitch_i : cached_pitch;
        effective_gain = accept_target
            ? (target_gain_q16_i > 17'd65536 ? 17'd65536 : target_gain_q16_i) : cached_gain;
        expires_now = audio_tick_i && have_target && !accept_target && !fresh_pending
            && age_ticks >= AGE_W'(STALE_AUDIO_TICKS - 1);
        want_run = healthy && (have_target || accept_target) && !expires_now;
        pass_volume = volume_open ? effective_gain > 17'(MUTE_CLOSE_Q16)
                                  : effective_gain >= 17'(MUTE_OPEN_Q16);
        pitch_target = $signed({4'b0, effective_pitch});
        gain_target = state == PLAY && want_run && pass_volume
            ? $signed({1'b0, effective_gain}) : 18'sd0;
        pitch_code_o = 14'(pitch_value);
        pitch_delta_o = pitch_value - $signed({4'b0, pitch_reference});
        gain_q16_o = gain_value < 0 ? 17'd0
            : (gain_value > 18'sd65536 ? 17'd65536 : 17'(gain_value));
        voice_request_o = state != OFF;
        active_o = state != OFF;
        voice_gate_o = gate_held;
    end

    theremin_slew #(.WIDTH(18), .STEP(PITCH_STEP)) pitch_slew (
        .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear_i),
        .load_i((state == OFF || state == REQUEST) && want_run),
        .step_i(audio_tick_i && state == PLAY && want_run),
        .target_i(pitch_target), .value_o(pitch_value), .at_target_o(unused_pitch_at_target)
    );
    theremin_slew #(.WIDTH(18), .STEP(GAIN_STEP)) gain_slew (
        .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear_i || state == OFF || state == REQUEST),
        .load_i(1'b0), .step_i(audio_tick_i), .target_i(gain_target),
        .value_o(gain_value), .at_target_o(unused_gain_at_target)
    );

    always_ff @(posedge clk_i) begin
        if (!rst_ni || clear_i) begin
            state <= OFF;
            cached_pitch <= '0;
            cached_gain <= '0;
            pitch_reference <= '0;
            have_target <= 1'b0;
            fresh_pending <= 1'b0;
            age_ticks <= '0;
            ready_ticks <= '0;
            volume_open <= 1'b0;
            gate_held <= 1'b0;
            stale_o <= 1'b0;
            voice_fault_o <= 1'b0;
            voice_start_o <= 1'b0;
            voice_stop_o <= 1'b0;
            controls_valid_o <= 1'b0;
        end else begin
            voice_start_o <= 1'b0;
            voice_stop_o <= 1'b0;
            controls_valid_o <= audio_tick_i;
            if (!enable_i) voice_fault_o <= 1'b0;

            if (accept_target) begin
                cached_pitch <= effective_pitch;
                cached_gain <= effective_gain;
                have_target <= 1'b1;
                fresh_pending <= !audio_tick_i;
                age_ticks <= '0;
                stale_o <= 1'b0;
                volume_open <= pass_volume;
            end else if (!healthy || state == RELEASE) begin
                have_target <= 1'b0;
                fresh_pending <= 1'b0;
                age_ticks <= '0;
                volume_open <= 1'b0;
            end else if (audio_tick_i) begin
                if (fresh_pending) begin
                    fresh_pending <= 1'b0;
                    age_ticks <= '0;
                end else if (have_target) begin
                    if (expires_now) begin
                        stale_o <= 1'b1;
                        have_target <= 1'b0;
                    end else age_ticks <= age_ticks + 1'b1;
                end
            end

            case (state)
                OFF: begin
                    gate_held <= 1'b0;
                    ready_ticks <= '0;
                    if (want_run) begin
                        pitch_reference <= reference_pitch_i;
                        voice_start_o <= 1'b1;
                        state <= REQUEST;
                    end
                end
                REQUEST: begin
                    if (!want_run) state <= RELEASE;
                    else if (voice_ready_i) begin
                        gate_held <= 1'b1;
                        state <= PLAY;
                    end else if (audio_tick_i) begin
                        if (ready_ticks >= READY_W'(VOICE_READY_TIMEOUT_TICKS - 1)) begin
                            voice_fault_o <= 1'b1;
                            have_target <= 1'b0;
                            fresh_pending <= 1'b0;
                            state <= RELEASE;
                        end else ready_ticks <= ready_ticks + 1'b1;
                    end
                end
                PLAY: begin
                    if (!want_run) begin
                        state <= RELEASE;
                        have_target <= 1'b0;
                        fresh_pending <= 1'b0;
                    end
                end
                RELEASE: begin
                    if (audio_tick_i && gain_q16_o <= 17'(GAIN_STEP)) begin
                        state <= OFF;
                        gate_held <= 1'b0;
                        voice_stop_o <= 1'b1;
                        have_target <= 1'b0;
                        fresh_pending <= 1'b0;
                        volume_open <= 1'b0;
                    end
                end
                default: state <= OFF;
            endcase
        end
    end
endmodule
