`timescale 1ns/1ps

// Calibrated two-channel control mapper. All ports are in clk_i's domain.
module sensor_control_mapper #(
    parameter integer FILTER_SHIFT = 2,
    parameter integer FILTER_FRAC_BITS = 8,
    parameter integer MIN_RAW_SPAN = 64,
    parameter bit SQUARE_VOLUME = 1'b1
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic clear_i,
    input  logic sample_flush_i, // cancel samples/filter history; retain calibration
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
    output logic busy_o
);
    localparam integer FW = 28 + FILTER_FRAC_BITS;
    localparam logic [FW:0] FILTER_HALF = (FW+1)'(1) << (FILTER_FRAC_BITS-1);
    typedef enum logic [3:0] {
        IDLE, SETUP_PITCH, DIV_PITCH, ROUND_PITCH, SETUP_VOLUME,
        DIV_VOLUME, ROUND_VOLUME, SCALE, PUBLISH
    } state_t;
    state_t state;
    logic [27:0] pitch_far, pitch_near, volume_far, volume_near;
    logic [27:0] pitch_span, volume_span;
    logic [13:0] pitch_min, pitch_max;
    logic primed;
    logic [FW-1:0] pitch_filter, volume_filter;
    logic [FW-1:0] next_pitch_filter, next_volume_filter;
    logic signed [FW:0] pitch_difference, volume_difference;
    logic [27:0] rounded_pitch, rounded_volume;
    logic [27:0] work_pitch, work_volume;
    logic [16:0] pitch_u, volume_u;
    logic [27:0] denominator, remainder;
    logic [28:0] shifted_remainder;
    logic [15:0] quotient;
    logic [4:0] bit_count;
    logic [27:0] cfg_pitch_span, cfg_volume_span;
    logic [13:0] pitch_range;
    logic [16:0] volume_open;
    logic [13:0] next_mapped_pitch;
    logic [16:0] next_mapped_gain;
    logic [13:0] pending_pitch;
    logic [16:0] pending_gain;

    initial begin
        if (FILTER_SHIFT < 0 || FILTER_SHIFT > 16)
            $fatal(1, "FILTER_SHIFT must be 0..16");
        if (FILTER_FRAC_BITS < 1 || FILTER_FRAC_BITS > 16)
            $fatal(1, "FILTER_FRAC_BITS must be 1..16");
        if (MIN_RAW_SPAN < 1 || MIN_RAW_SPAN > 268435455)
            $fatal(1, "MIN_RAW_SPAN must be 1..2^28-1");
    end

    always_comb begin
        cfg_pitch_span = (cfg_pitch_near_i >= cfg_pitch_far_i)
            ? cfg_pitch_near_i - cfg_pitch_far_i : cfg_pitch_far_i - cfg_pitch_near_i;
        cfg_volume_span = (cfg_volume_near_i >= cfg_volume_far_i)
            ? cfg_volume_near_i - cfg_volume_far_i : cfg_volume_far_i - cfg_volume_near_i;
        cfg_ready_o = rst_ni && !clear_i && !sample_flush_i && state == IDLE;
        sample_ready_o = cfg_ready_o && calibrated_o && !cfg_valid_i;
        busy_o = state != IDLE;

        pitch_difference = $signed({1'b0, raw_pitch_i, {FILTER_FRAC_BITS{1'b0}}})
                         - $signed({1'b0, pitch_filter});
        volume_difference = $signed({1'b0, raw_volume_i, {FILTER_FRAC_BITS{1'b0}}})
                          - $signed({1'b0, volume_filter});
        next_pitch_filter = primed ? FW'($signed({1'b0, pitch_filter})
                                         + (pitch_difference >>> FILTER_SHIFT))
                                   : {raw_pitch_i, {FILTER_FRAC_BITS{1'b0}}};
        next_volume_filter = primed ? FW'($signed({1'b0, volume_filter})
                                          + (volume_difference >>> FILTER_SHIFT))
                                    : {raw_volume_i, {FILTER_FRAC_BITS{1'b0}}};
        rounded_pitch = 28'(({1'b0, next_pitch_filter} + FILTER_HALF) >> FILTER_FRAC_BITS);
        rounded_volume = 28'(({1'b0, next_volume_filter} + FILTER_HALF) >> FILTER_FRAC_BITS);

        shifted_remainder = {remainder, 1'b0};
        pitch_range = pitch_max - pitch_min;
        next_mapped_pitch = pitch_min
            + 14'((31'(pitch_range) * 31'(pitch_u) + 31'd32768) >> 16);
        volume_open = 17'd65536 - volume_u;
        next_mapped_gain = SQUARE_VOLUME
            ? 17'((34'(volume_open) * 34'(volume_open) + 34'd32768) >> 16) : volume_open;
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni || clear_i) begin
            state <= IDLE;
            pitch_far <= '0;
            pitch_near <= '0;
            volume_far <= '0;
            volume_near <= '0;
            pitch_span <= '0;
            volume_span <= '0;
            pitch_min <= '0;
            pitch_max <= '0;
            reference_pitch_o <= '0;
            calibrated_o <= 1'b0;
            cfg_applied_o <= 1'b0;
            cfg_error_o <= 1'b0;
            primed <= 1'b0;
            pitch_filter <= '0;
            volume_filter <= '0;
            work_pitch <= '0;
            work_volume <= '0;
            pitch_u <= '0;
            volume_u <= '0;
            denominator <= '0;
            remainder <= '0;
            quotient <= '0;
            bit_count <= '0;
            pending_pitch <= '0;
            pending_gain <= '0;
            mapped_valid_o <= 1'b0;
            mapped_pitch_o <= '0;
            mapped_delta_o <= '0;
            mapped_gain_q16_o <= '0;
            normalized_pitch_o <= '0;
            normalized_volume_o <= '0;
            filtered_pitch_o <= '0;
            filtered_volume_o <= '0;
            bad_sample_o <= 1'b0;
            overrun_o <= 1'b0;
        end else if (sample_flush_i) begin
            // Source/enable changes invalidate only the sampling generation.
            // Preserve the calibrated endpoints, spans, pitch range and reference
            // so configuration can be loaded and reused while modulation is off.
            state <= IDLE;
            cfg_applied_o <= 1'b0;
            cfg_error_o <= 1'b0;
            primed <= 1'b0;
            pitch_filter <= '0;
            volume_filter <= '0;
            work_pitch <= '0;
            work_volume <= '0;
            pitch_u <= '0;
            volume_u <= '0;
            denominator <= '0;
            remainder <= '0;
            quotient <= '0;
            bit_count <= '0;
            pending_pitch <= '0;
            pending_gain <= '0;
            mapped_valid_o <= 1'b0;
            mapped_pitch_o <= '0;
            mapped_delta_o <= '0;
            mapped_gain_q16_o <= '0;
            normalized_pitch_o <= '0;
            normalized_volume_o <= '0;
            filtered_pitch_o <= '0;
            filtered_volume_o <= '0;
            bad_sample_o <= 1'b0;
            overrun_o <= 1'b0;
        end else begin
            cfg_applied_o <= 1'b0;
            cfg_error_o <= 1'b0;
            mapped_valid_o <= 1'b0;
            bad_sample_o <= sample_error_i;
            overrun_o <= sample_valid_i && calibrated_o && !sample_ready_o && !sample_error_i;
            case (state)
                IDLE: begin
                    if (cfg_valid_i) begin
                        if (cfg_pitch_span >= 28'(MIN_RAW_SPAN)
                            && cfg_volume_span >= 28'(MIN_RAW_SPAN)
                            && cfg_pitch_max_i >= cfg_pitch_min_i) begin
                            pitch_far <= cfg_pitch_far_i;
                            pitch_near <= cfg_pitch_near_i;
                            volume_far <= cfg_volume_far_i;
                            volume_near <= cfg_volume_near_i;
                            pitch_span <= cfg_pitch_span;
                            volume_span <= cfg_volume_span;
                            pitch_min <= cfg_pitch_min_i;
                            pitch_max <= cfg_pitch_max_i;
                            reference_pitch_o <= cfg_reference_pitch_i;
                            calibrated_o <= 1'b1;
                            primed <= 1'b0;
                            cfg_applied_o <= 1'b1;
                        end else cfg_error_o <= 1'b1;
                    end else if (sample_valid_i && calibrated_o && !sample_error_i) begin
                        pitch_filter <= next_pitch_filter;
                        volume_filter <= next_volume_filter;
                        work_pitch <= rounded_pitch;
                        work_volume <= rounded_volume;
                        primed <= 1'b1;
                        state <= SETUP_PITCH;
                    end
                end
                SETUP_PITCH: begin
                    denominator <= pitch_span;
                    quotient <= '0;
                    bit_count <= '0;
                    if ((pitch_near > pitch_far && work_pitch <= pitch_far)
                        || (pitch_near < pitch_far && work_pitch >= pitch_far)) begin
                        pitch_u <= '0;
                        state <= SETUP_VOLUME;
                    end else if ((pitch_near > pitch_far && work_pitch >= pitch_near)
                                 || (pitch_near < pitch_far && work_pitch <= pitch_near)) begin
                        pitch_u <= 17'd65536;
                        state <= SETUP_VOLUME;
                    end else begin
                        remainder <= pitch_near > pitch_far
                            ? work_pitch - pitch_far : pitch_far - work_pitch;
                        state <= DIV_PITCH;
                    end
                end
                DIV_PITCH, DIV_VOLUME: begin
                    if (shifted_remainder >= {1'b0, denominator}) begin
                        remainder <= 28'(shifted_remainder - {1'b0, denominator});
                        quotient <= {quotient[14:0], 1'b1};
                    end else begin
                        remainder <= shifted_remainder[27:0];
                        quotient <= {quotient[14:0], 1'b0};
                    end
                    if (bit_count == 5'd15)
                        state <= state == DIV_PITCH ? ROUND_PITCH : ROUND_VOLUME;
                    else bit_count <= bit_count + 1'b1;
                end
                ROUND_PITCH: begin
                    pitch_u <= {1'b0, quotient}
                        + 17'(shifted_remainder >= {1'b0, denominator});
                    state <= SETUP_VOLUME;
                end
                SETUP_VOLUME: begin
                    denominator <= volume_span;
                    quotient <= '0;
                    bit_count <= '0;
                    if ((volume_near > volume_far && work_volume <= volume_far)
                        || (volume_near < volume_far && work_volume >= volume_far)) begin
                        volume_u <= '0;
                        state <= SCALE;
                    end else if ((volume_near > volume_far && work_volume >= volume_near)
                                 || (volume_near < volume_far && work_volume <= volume_near)) begin
                        volume_u <= 17'd65536;
                        state <= SCALE;
                    end else begin
                        remainder <= volume_near > volume_far
                            ? work_volume - volume_far : volume_far - work_volume;
                        state <= DIV_VOLUME;
                    end
                end
                ROUND_VOLUME: begin
                    volume_u <= {1'b0, quotient}
                        + 17'(shifted_remainder >= {1'b0, denominator});
                    state <= SCALE;
                end
                SCALE: begin
                    pending_pitch <= next_mapped_pitch;
                    pending_gain <= next_mapped_gain;
                    state <= PUBLISH;
                end
                PUBLISH: begin
                    mapped_pitch_o <= pending_pitch;
                    mapped_delta_o <= $signed({4'b0, pending_pitch})
                                    - $signed({4'b0, reference_pitch_o});
                    mapped_gain_q16_o <= pending_gain;
                    normalized_pitch_o <= pitch_u;
                    normalized_volume_o <= volume_u;
                    filtered_pitch_o <= work_pitch;
                    filtered_volume_o <= work_volume;
                    mapped_valid_o <= 1'b1;
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule
