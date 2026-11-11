`timescale 1ns/1ps

// Additional modulation dimension: pitch ADD, expression MULTIPLY.
// All inputs are synchronous to clk_i; no voice/audio/CDC ownership here.
module sensor_control_decoder #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer STALE_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter integer FRAME_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter integer FDC_FILTER_SHIFT = 2,
    parameter integer TOF_FILTER_SHIFT = 1,
    parameter integer FILTER_FRAC_BITS = 8,
    parameter integer FDC_MIN_RAW_SPAN = 64,
    parameter integer TOF_MIN_MM_SPAN = 1,
    parameter bit SQUARE_VOLUME = 1'b1
) (
    input logic clk_i,
    input logic rst_ni,
    input logic clear_i,
    input logic enable_i,
    input logic source_select_i, // 0: FDC CH0/CH1; 1: ToF1/ToF2
    input logic cfg_valid_i,
    output logic cfg_ready_o,
    input logic cfg_source_i,
    input logic [27:0] cfg_pitch_far_i,
    input logic [27:0] cfg_pitch_near_i,
    input logic [27:0] cfg_volume_far_i,
    input logic [27:0] cfg_volume_near_i,
    input logic [13:0] cfg_pitch_min_i,
    input logic [13:0] cfg_pitch_max_i,
    input logic [13:0] cfg_reference_pitch_i,
    output logic [1:0] calibrated_o,
    output logic [1:0] cfg_applied_o,
    output logic [1:0] cfg_error_o,
    input logic fdc_ready_i,
    input logic fdc_fault_i,
    input logic fdc_sample_valid_i,
    input logic fdc_sample_error_i,
    input logic [27:0] fdc_raw_ch0_i,
    input logic [27:0] fdc_raw_ch1_i,
    input logic [1:0] tof_ready_i,
    input logic [1:0] tof_fault_i,
    input logic [1:0] tof_sample_valid_i,
    input logic [1:0] tof_sample_error_i,
    input logic [1:0][15:0] tof_distance_mm_i,
    output logic controls_valid_o,
    output logic modulation_active_o,
    output logic source_o,
    output logic source_ready_o,
    output logic source_fault_o,
    output logic stale_o,
    output logic [13:0] pitch_code_o,
    output logic signed [17:0] pitch_delta_o,
    output logic [16:0] expression_gain_q16_o,
    output logic [16:0] normalized_pitch_o,
    output logic [16:0] normalized_volume_o,
    output logic sample_error_o,
    output logic overrun_o,
    output logic expired_o
);
    localparam integer AGE_W = STALE_TIMEOUT_CYCLES > 1 ? $clog2(STALE_TIMEOUT_CYCLES) : 1;
    localparam logic [AGE_W-1:0] AGE_LAST = AGE_W'(STALE_TIMEOUT_CYCLES-1);
    logic source_q, enabled_q;
    logic [1:0] operational_q;
    wire [1:0] operational = {(&tof_ready_i) && !(|tof_fault_i), fdc_ready_i && !fdc_fault_i};
    wire [1:0] operational_changed = operational ^ operational_q;
    wire source_changed = source_select_i != source_q;
    wire enable_changed = enable_i != enabled_q;
    wire common_flush = source_changed || enable_changed;
    wire selected_health_changed = operational_changed[source_select_i];
    logic [AGE_W-1:0] age_q;
    logic stale_q;
    wire capable = enable_i && operational[source_select_i] && calibrated_o[source_select_i];
    // Deadline wins over same-edge data. This also avoids a combinational
    // loop through mapper ready -> timeout flush -> mapper ready.
    wire timeout_event = capable && !stale_q && age_q == AGE_LAST
                       && !common_flush && !selected_health_changed && !clear_i;
    logic [1:0] mapper_cfg_ready, mapper_cfg_error, mapper_sample_ready, mapper_valid;
    logic [1:0][13:0] mapper_pitch;
    logic [1:0][17:0] mapper_delta;
    logic [1:0][16:0] mapper_gain, mapper_normalized_pitch, mapper_normalized_volume;
    logic [1:0] cfg_rejected;
    logic [27:0] cfg_pitch_span, cfg_volume_span;
    logic cfg_payload_valid;
    wire cfg_accept = cfg_valid_i && cfg_ready_o;
    wire selected_config_write = cfg_accept && cfg_payload_valid && cfg_source_i == source_select_i;
    wire selected_cancel = common_flush || selected_health_changed || selected_config_write || timeout_event;
    wire [1:0] mapper_flush = {2{common_flush}} | operational_changed
                           | (timeout_event ? (source_select_i ? 2'b10 : 2'b01) : 2'b00);
    wire frame_valid;
    wire [27:0] raw_pitch, raw_volume;
    wire frame_ready = capable && !clear_i && !selected_cancel && mapper_sample_ready[source_select_i];
    wire frame_accept = frame_valid && frame_ready;

    initial begin
        if (CLK_HZ < 1 || FRAME_TIMEOUT_CYCLES < 1)
            $fatal(1, "Decoder clock/frame timeout must be positive");
        // A worst-case two-channel map takes 38 clocks plus one publish stage.
        if (STALE_TIMEOUT_CYCLES < 40)
            $fatal(1, "STALE_TIMEOUT_CYCLES must be >=40 to cover mapper latency");
        if (TOF_MIN_MM_SPAN < 1 || TOF_MIN_MM_SPAN > 65535)
            $fatal(1, "TOF_MIN_MM_SPAN must be 1..65535 mm");
    end

    always_comb begin
        cfg_pitch_span = cfg_pitch_near_i >= cfg_pitch_far_i
                       ? cfg_pitch_near_i - cfg_pitch_far_i : cfg_pitch_far_i - cfg_pitch_near_i;
        cfg_volume_span = cfg_volume_near_i >= cfg_volume_far_i
                        ? cfg_volume_near_i - cfg_volume_far_i : cfg_volume_far_i - cfg_volume_near_i;
        cfg_payload_valid = cfg_pitch_span >= (cfg_source_i ? 28'(TOF_MIN_MM_SPAN) : 28'(FDC_MIN_RAW_SPAN))
                         && cfg_volume_span >= (cfg_source_i ? 28'(TOF_MIN_MM_SPAN) : 28'(FDC_MIN_RAW_SPAN))
                         && cfg_pitch_max_i >= cfg_pitch_min_i
                         && (!cfg_source_i || (cfg_pitch_far_i[27:16] == 0 && cfg_pitch_near_i[27:16] == 0
                             && cfg_volume_far_i[27:16] == 0 && cfg_volume_near_i[27:16] == 0));
        cfg_ready_o = rst_ni && !clear_i && mapper_cfg_ready[cfg_source_i];
        cfg_error_o = cfg_rejected | mapper_cfg_error;
        stale_o = stale_q;
    end

    sensor_raw_frame_select #(.FRAME_TIMEOUT_CYCLES(FRAME_TIMEOUT_CYCLES)) u_frames (
        .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear_i || selected_cancel),
        .enable_i(enable_i && calibrated_o[source_select_i]), .source_i(source_select_i),
        .fdc_ready_i(fdc_ready_i), .fdc_fault_i(fdc_fault_i),
        .fdc_sample_valid_i(fdc_sample_valid_i), .fdc_sample_error_i(fdc_sample_error_i),
        .fdc_raw_ch0_i(fdc_raw_ch0_i), .fdc_raw_ch1_i(fdc_raw_ch1_i),
        .tof_ready_i(tof_ready_i), .tof_fault_i(tof_fault_i),
        .tof_sample_valid_i(tof_sample_valid_i), .tof_sample_error_i(tof_sample_error_i),
        .tof_distance_mm_i(tof_distance_mm_i),
        .frame_ready_i(frame_ready), .frame_valid_o(frame_valid),
        .raw_pitch_o(raw_pitch), .raw_volume_o(raw_volume),
        .source_ready_o(source_ready_o), .source_fault_o(source_fault_o),
        .sample_error_o(sample_error_o), .overrun_o(overrun_o), .expired_o(expired_o)
    );

    for (genvar bank=0; bank<2; bank++) begin : mapping
        wire [27:0] unused_filtered_pitch, unused_filtered_volume;
        wire [13:0] unused_reference_pitch;
        wire unused_bad_sample, unused_overrun, unused_busy;
        sensor_control_mapper #(
            .FILTER_SHIFT(bank == 0 ? FDC_FILTER_SHIFT : TOF_FILTER_SHIFT),
            .FILTER_FRAC_BITS(FILTER_FRAC_BITS),
            .MIN_RAW_SPAN(bank == 0 ? FDC_MIN_RAW_SPAN : TOF_MIN_MM_SPAN), .SQUARE_VOLUME(SQUARE_VOLUME)
        ) u_mapper (
            .clk_i(clk_i), .rst_ni(rst_ni), .clear_i(clear_i), .sample_flush_i(mapper_flush[bank]),
            .cfg_valid_i(cfg_valid_i && cfg_source_i == 1'(bank) && cfg_payload_valid),
            .cfg_ready_o(mapper_cfg_ready[bank]),
            .cfg_pitch_far_i(cfg_pitch_far_i), .cfg_pitch_near_i(cfg_pitch_near_i),
            .cfg_volume_far_i(cfg_volume_far_i), .cfg_volume_near_i(cfg_volume_near_i),
            .cfg_pitch_min_i(cfg_pitch_min_i), .cfg_pitch_max_i(cfg_pitch_max_i),
            .cfg_reference_pitch_i(cfg_reference_pitch_i), .calibrated_o(calibrated_o[bank]),
            .cfg_applied_o(cfg_applied_o[bank]), .cfg_error_o(mapper_cfg_error[bank]),
            .sample_valid_i(frame_accept && source_select_i == 1'(bank)),
            .sample_ready_o(mapper_sample_ready[bank]), .sample_error_i(1'b0),
            .raw_pitch_i(raw_pitch), .raw_volume_i(raw_volume), .mapped_valid_o(mapper_valid[bank]),
            .mapped_pitch_o(mapper_pitch[bank]), .mapped_delta_o(mapper_delta[bank]),
            .mapped_gain_q16_o(mapper_gain[bank]), .normalized_pitch_o(mapper_normalized_pitch[bank]),
            .normalized_volume_o(mapper_normalized_volume[bank]),
            .filtered_pitch_o(unused_filtered_pitch), .filtered_volume_o(unused_filtered_volume),
            .reference_pitch_o(unused_reference_pitch), .bad_sample_o(unused_bad_sample),
            .overrun_o(unused_overrun), .busy_o(unused_busy)
        );
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            source_q <= 1'b0;
            enabled_q <= 1'b0;
            operational_q <= '0;
            cfg_rejected <= '0;
            age_q <= '0;
            stale_q <= 1'b0;
        end else begin
            source_q <= source_select_i;
            enabled_q <= enable_i;
            operational_q <= operational;
            cfg_rejected <= '0;
            if (cfg_accept && !cfg_payload_valid) cfg_rejected[cfg_source_i] <= 1'b1;
            if (clear_i || common_flush || selected_health_changed || selected_config_write || !capable) begin
                age_q <= '0;
                stale_q <= 1'b0;
            end else if (frame_accept) begin
                age_q <= '0;
                stale_q <= 1'b0;
            end else begin
                if (age_q < AGE_LAST) age_q <= age_q + 1'b1;
                if (timeout_event) stale_q <= 1'b1;
            end
        end
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            controls_valid_o <= 1'b0;
            modulation_active_o <= 1'b0;
            source_o <= 1'b0;
            pitch_code_o <= '0;
            pitch_delta_o <= '0;
            expression_gain_q16_o <= 17'd65536;
            normalized_pitch_o <= '0;
            normalized_volume_o <= '0;
        end else begin
            controls_valid_o <= 1'b0;
            if (!capable || clear_i || selected_cancel || stale_q) begin
                controls_valid_o <= modulation_active_o || clear_i || selected_cancel;
                modulation_active_o <= 1'b0;
                source_o <= source_select_i;
                pitch_code_o <= '0;
                pitch_delta_o <= '0;
                expression_gain_q16_o <= 17'd65536;
                normalized_pitch_o <= '0;
                normalized_volume_o <= '0;
            end else if (mapper_valid[source_select_i]) begin
                controls_valid_o <= 1'b1;
                modulation_active_o <= 1'b1;
                source_o <= source_select_i;
                pitch_code_o <= mapper_pitch[source_select_i];
                pitch_delta_o <= $signed(mapper_delta[source_select_i]);
                expression_gain_q16_o <= mapper_gain[source_select_i];
                normalized_pitch_o <= mapper_normalized_pitch[source_select_i];
                normalized_volume_o <= mapper_normalized_volume[source_select_i];
            end
        end
    end
endmodule
