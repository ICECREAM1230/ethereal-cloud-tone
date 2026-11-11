`timescale 1ns/1ps

// Sensor-to-control composition, independent of any voice allocator/audio IP.
// Every input, including audio_tick_i and voice_ready_i, must be synchronous.
module theremin_frontend #(
    parameter integer FILTER_SHIFT = 2,
    parameter integer FILTER_FRAC_BITS = 8,
    parameter integer MIN_RAW_SPAN = 64,
    parameter bit SQUARE_VOLUME = 1'b1,
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
    input  logic sensor_ready_i,
    input  logic sensor_fault_i,
    input  logic cfg_valid_i,
    output logic cfg_ready_o,
    input  logic [27:0] cfg_pitch_far_i,
    input  logic [27:0] cfg_pitch_near_i,
    input  logic [27:0] cfg_volume_far_i,
    input  logic [27:0] cfg_volume_near_i,
    input  logic [13:0] cfg_pitch_min_i,
    input  logic [13:0] cfg_pitch_max_i,
    input  logic [13:0] cfg_reference_pitch_i,
    output logic calibrated_o,
    output logic cfg_applied_o,
    output logic cfg_error_o,
    input  logic sample_valid_i,
    output logic sample_ready_o,
    input  logic sample_error_i,
    input  logic [27:0] raw_pitch_i,
    input  logic [27:0] raw_volume_i,
    output logic mapped_valid_o,
    output logic [13:0] mapped_pitch_o,
    output logic signed [17:0] mapped_delta_o,
    output logic [16:0] mapped_gain_q16_o,
    output logic [16:0] normalized_pitch_o,
    output logic [16:0] normalized_volume_o,
    output logic [27:0] filtered_pitch_o,
    output logic [27:0] filtered_volume_o,
    output logic [13:0] reference_pitch_o,
    output logic bad_sample_o,
    output logic overrun_o,
    output logic busy_o,
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
    logic mapper_cfg_ready;
    // Calibrating an active voice could change its fixed pitch reference.
    assign cfg_ready_o = mapper_cfg_ready && !voice_request_o;

    theremin_mapper #(
        .FILTER_SHIFT(FILTER_SHIFT), .FILTER_FRAC_BITS(FILTER_FRAC_BITS),
        .MIN_RAW_SPAN(MIN_RAW_SPAN), .SQUARE_VOLUME(SQUARE_VOLUME)
    ) mapper (
        .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear_i),
        .cfg_valid_i(cfg_valid_i && !voice_request_o), .cfg_ready_o(mapper_cfg_ready),
        .cfg_pitch_far_i(cfg_pitch_far_i), .cfg_pitch_near_i(cfg_pitch_near_i),
        .cfg_volume_far_i(cfg_volume_far_i), .cfg_volume_near_i(cfg_volume_near_i),
        .cfg_pitch_min_i(cfg_pitch_min_i), .cfg_pitch_max_i(cfg_pitch_max_i),
        .cfg_reference_pitch_i(cfg_reference_pitch_i),
        .calibrated_o(calibrated_o), .cfg_applied_o(cfg_applied_o), .cfg_error_o(cfg_error_o),
        .sample_valid_i(sample_valid_i), .sample_ready_o(sample_ready_o),
        .sample_error_i(sample_error_i), .raw_pitch_i(raw_pitch_i), .raw_volume_i(raw_volume_i),
        .mapped_valid_o(mapped_valid_o), .mapped_pitch_o(mapped_pitch_o),
        .mapped_delta_o(mapped_delta_o), .mapped_gain_q16_o(mapped_gain_q16_o),
        .normalized_pitch_o(normalized_pitch_o), .normalized_volume_o(normalized_volume_o),
        .filtered_pitch_o(filtered_pitch_o), .filtered_volume_o(filtered_volume_o),
        .reference_pitch_o(reference_pitch_o), .bad_sample_o(bad_sample_o),
        .overrun_o(overrun_o), .busy_o(busy_o)
    );

    theremin_control_bridge #(
        .STALE_AUDIO_TICKS(STALE_AUDIO_TICKS),
        .VOICE_READY_TIMEOUT_TICKS(VOICE_READY_TIMEOUT_TICKS),
        .PITCH_STEP(PITCH_STEP), .GAIN_STEP(GAIN_STEP),
        .MUTE_CLOSE_Q16(MUTE_CLOSE_Q16), .MUTE_OPEN_Q16(MUTE_OPEN_Q16)
    ) bridge (
        // An accepted configuration has priority over a coincident mapped
        // result. Clear immediately so no voice_start can escape for one cycle.
        .clk_i(clk_i), .rst_ni(rst_ni),
        .clear_i(clear_i || cfg_applied_o || (cfg_valid_i && cfg_ready_o)),
        .enable_i(enable_i), .calibrated_i(calibrated_o && sensor_ready_i),
        .sensor_fault_i(sensor_fault_i), .target_valid_i(mapped_valid_o),
        .target_pitch_i(mapped_pitch_o), .reference_pitch_i(reference_pitch_o),
        .target_gain_q16_i(mapped_gain_q16_o), .audio_tick_i(audio_tick_i),
        .voice_ready_i(voice_ready_i), .pitch_code_o(pitch_code_o),
        .pitch_delta_o(pitch_delta_o), .gain_q16_o(gain_q16_o),
        .voice_request_o(voice_request_o), .voice_start_o(voice_start_o),
        .voice_stop_o(voice_stop_o), .voice_gate_o(voice_gate_o), .active_o(active_o),
        .stale_o(stale_o), .voice_fault_o(voice_fault_o), .controls_valid_o(controls_valid_o)
    );
endmodule
