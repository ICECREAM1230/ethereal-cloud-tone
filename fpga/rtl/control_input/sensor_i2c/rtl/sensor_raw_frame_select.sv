`timescale 1ns/1ps

// One-entry raw-frame queue. ToF lanes must each contribute a new, healthy
// sample; pairing preserves the older lane's age instead of renewing its life.
module sensor_raw_frame_select #(
    parameter integer unsigned FRAME_TIMEOUT_CYCLES = 5_000_000
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic clear_i,
    input  logic enable_i,
    input  logic source_i,
    input  logic fdc_ready_i,
    input  logic fdc_fault_i,
    input  logic fdc_sample_valid_i,
    input  logic fdc_sample_error_i,
    input  logic [27:0] fdc_raw_ch0_i,
    input  logic [27:0] fdc_raw_ch1_i,
    input  logic [1:0] tof_ready_i,
    input  logic [1:0] tof_fault_i,
    input  logic [1:0] tof_sample_valid_i,
    input  logic [1:0] tof_sample_error_i,
    input  logic [1:0][15:0] tof_distance_mm_i,
    input  logic frame_ready_i,
    output logic frame_valid_o,
    output logic [27:0] raw_pitch_o,
    output logic [27:0] raw_volume_o,
    output logic source_ready_o,
    output logic source_fault_o,
    output logic sample_error_o,
    output logic overrun_o,
    output logic expired_o
);
    localparam integer AGE_W = (FRAME_TIMEOUT_CYCLES <= 1)
        ? 1 : $clog2(FRAME_TIMEOUT_CYCLES);
    localparam logic [AGE_W-1:0] AGE_LIMIT = AGE_W'(FRAME_TIMEOUT_CYCLES - 1);
    logic enable_q, source_q, ready_q, fault_q;
    logic cancel;
    logic frame_valid_q, frame_valid_d;
    logic [27:0] pitch_d, volume_d;
    logic [AGE_W-1:0] frame_age_q, frame_age_d;
    logic [1:0] fresh_q, fresh_d;
    logic [1:0][15:0] distance_q, distance_d;
    logic [1:0][AGE_W-1:0] lane_age_q, lane_age_d;
    logic sample_error_d, overrun_d, expired_d;
    logic candidate_valid;
    logic [27:0] candidate_pitch, candidate_volume;
    logic [AGE_W-1:0] candidate_age;
    logic [1:0] good_tof_sample;

    assign source_ready_o = source_i ? &tof_ready_i : fdc_ready_i;
    assign source_fault_o = source_i ? |tof_fault_i : fdc_fault_i;
    assign good_tof_sample = tof_sample_valid_i & ~tof_sample_error_i;
    assign cancel = clear_i || !enable_i || enable_i != enable_q || source_i != source_q
        || source_ready_o != ready_q || source_fault_o != fault_q
        || !source_ready_o || source_fault_o;
    // Cancellation also masks the pre-edge handshake, so a simultaneous ready
    // cannot consume an old-source frame on a switch/clear/health boundary.
    assign frame_valid_o = rst_ni && !cancel && frame_valid_q;

    always_comb begin
        frame_valid_d = frame_valid_q;
        pitch_d = raw_pitch_o;
        volume_d = raw_volume_o;
        frame_age_d = frame_age_q;
        fresh_d = fresh_q;
        distance_d = distance_q;
        lane_age_d = lane_age_q;
        sample_error_d = 1'b0;
        overrun_d = 1'b0;
        expired_d = 1'b0;
        candidate_valid = 1'b0;
        candidate_pitch = '0;
        candidate_volume = '0;
        candidate_age = '0;

        if (cancel) begin
            frame_valid_d = 1'b0;
            frame_age_d = '0;
            fresh_d = '0;
            lane_age_d = '0;
        end else begin
            if (frame_valid_q) begin
                // A frame is visible for its remaining lifetime. Ready on the
                // deadline edge consumes it; expiration applies only if stalled.
                if (frame_ready_i) begin
                    frame_valid_d = 1'b0;
                    frame_age_d = '0;
                end else if (frame_age_q == AGE_LIMIT) begin
                    frame_valid_d = 1'b0;
                    frame_age_d = '0;
                    expired_d = 1'b1;
                end else frame_age_d = frame_age_q + AGE_W'(1);
            end else frame_age_d = '0;

            if (!source_i) begin
                fresh_d = '0;
                lane_age_d = '0;
                sample_error_d = fdc_sample_error_i;
                if (fdc_sample_valid_i && !fdc_sample_error_i) begin
                    candidate_valid = 1'b1;
                    candidate_pitch = fdc_raw_ch0_i;
                    candidate_volume = fdc_raw_ch1_i;
                end
            end else begin
                sample_error_d = |tof_sample_error_i;
                for (integer lane = 0; lane < 2; lane = lane + 1) begin
                    if (fresh_q[lane]) begin
                        if (lane_age_q[lane] == AGE_LIMIT) begin
                            fresh_d[lane] = 1'b0;
                            lane_age_d[lane] = '0;
                            // A valid replacement retires the old sample on
                            // this edge; errors do not replace or refresh it.
                            if (!good_tof_sample[lane]) expired_d = 1'b1;
                        end else lane_age_d[lane] = lane_age_q[lane] + AGE_W'(1);
                    end else lane_age_d[lane] = '0;

                    if (good_tof_sample[lane]) begin
                        fresh_d[lane] = 1'b1;
                        distance_d[lane] = tof_distance_mm_i[lane];
                        lane_age_d[lane] = '0;
                    end
                end
                if (&fresh_d) begin
                    candidate_valid = 1'b1;
                    candidate_pitch = {12'd0, distance_d[0]};
                    candidate_volume = {12'd0, distance_d[1]};
                    candidate_age = (lane_age_d[0] > lane_age_d[1])
                        ? lane_age_d[0] : lane_age_d[1];
                    // Even an overflowed complete pair consumes both fresh
                    // markers; neither half may be reused in a later frame.
                    fresh_d = '0;
                    lane_age_d = '0;
                end
            end

            if (candidate_valid) begin
                if (!frame_valid_q || frame_ready_i) begin
                    frame_valid_d = 1'b1;
                    pitch_d = candidate_pitch;
                    volume_d = candidate_volume;
                    frame_age_d = candidate_age;
                end else begin
                    // A stalled frame is never overwritten, even on its expiry
                    // edge. It expires while the arriving frame is dropped.
                    overrun_d = 1'b1;
                end
            end
        end
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            enable_q <= 1'b0;
            source_q <= 1'b0;
            ready_q <= 1'b0;
            fault_q <= 1'b0;
            frame_valid_q <= 1'b0;
            raw_pitch_o <= '0;
            raw_volume_o <= '0;
            frame_age_q <= '0;
            fresh_q <= '0;
            distance_q <= '0;
            lane_age_q <= '0;
            sample_error_o <= 1'b0;
            overrun_o <= 1'b0;
            expired_o <= 1'b0;
        end else begin
            enable_q <= enable_i;
            source_q <= source_i;
            ready_q <= source_ready_o;
            fault_q <= source_fault_o;
            frame_valid_q <= frame_valid_d;
            raw_pitch_o <= pitch_d;
            raw_volume_o <= volume_d;
            frame_age_q <= frame_age_d;
            fresh_q <= fresh_d;
            distance_q <= distance_d;
            lane_age_q <= lane_age_d;
            sample_error_o <= sample_error_d;
            overrun_o <= overrun_d;
            expired_o <= expired_d;
        end
    end
endmodule
