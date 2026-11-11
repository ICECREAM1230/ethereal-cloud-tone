// Digital audio oscilloscope acquisition.  One core clock domain; the parent
// synchronizes ack_toggle_i and supplies synchronous one-cycle ring RAM reads.
// PCM remains live during trigger wait, feature conversion and display handoff.
module scope_acquisition #(
    parameter integer COLS = 512,
    parameter integer RING_AW = 13,
    parameter integer DEFAULT_TIMEBASE = 2
) (
    input  wire                          clk_i,
    input  wire                          rst_n_i,
    input  wire                          pcm_valid_i,
    input  wire signed [23:0]             pcm_left_i,
    input  wire signed [23:0]             pcm_right_i,
    input  wire                          cfg_valid_i,
    output wire                          cfg_ready_o,
    input  wire [1:0]                    cfg_timebase_i,
    input  wire [1:0]                    cfg_gain_i,
    input  wire                          cfg_auto_i,
    input  wire                          cfg_falling_i,
    input  wire                          cfg_channel_i,
    input  wire signed [23:0]             cfg_threshold_i,
    input  wire [23:0]                   cfg_hysteresis_i,
    input  wire [15:0]                   cfg_holdoff_i,
    input  wire [15:0]                   cfg_timeout_i,
    input  wire                          ack_toggle_i,
    output wire                          ring_wr_en_o,
    output wire [RING_AW-1:0]             ring_wr_addr_o,
    output wire [63:0]                   ring_wr_data_o,
    output wire                          ring_rd_en_o,
    output wire [RING_AW-1:0]             ring_rd_addr_o,
    input  wire [63:0]                   ring_rd_data_i,
    output wire                          feature_wr_en_o,
    output wire [$clog2(COLS):0]          feature_wr_addr_o,
    output wire [35:0]                   feature_wr_data_o,
    output reg                           publish_toggle_o,
    output reg  [95:0]                   publish_meta_o,
    output reg  [31:0]                   sample_count_o,
    output reg  [31:0]                   publish_count_o,
    output reg  [31:0]                   overrun_count_o,
    output wire                          waiting_o
);
    localparam integer COL_AW = $clog2(COLS);
    localparam [31:0] RING_DEPTH = 32'd1 << RING_AW;
    localparam [31:0] COL_COUNT = COLS;
    localparam [2:0] ST_ARMED = 3'd0;
    localparam [2:0] ST_POST = 3'd1;
    localparam [2:0] ST_READ = 3'd2;
    localparam [2:0] ST_MAP = 3'd3;
    localparam [2:0] ST_ACCUMULATE = 3'd4;
    localparam [2:0] ST_COLUMN = 3'd5;
    localparam [2:0] ST_PUBLISH = 3'd6;
    localparam [2:0] ST_ACK = 3'd7;

    reg [2:0] state_q;
    reg [RING_AW:0] valid_samples_q;
    reg [1:0] timebase_q;
    reg [1:0] gain_q;
    reg auto_q, falling_q, channel_q;
    reg signed [23:0] threshold_q;
    reg [23:0] hysteresis_q;
    reg [15:0] holdoff_q, timeout_q;
    reg [15:0] holdoff_remaining_q, wait_samples_q;
    reg trigger_armed_q;
    reg [31:0] capture_start_q, read_sequence_q;
    reg capture_triggered_q, write_bank_q;
    reg [COL_AW-1:0] column_q;
    reg [2:0] bucket_sample_q;
    reg [8:0] mapped_left_y_q, mapped_right_y_q;
    reg [8:0] left_ymin_q, left_ymax_q, right_ymin_q, right_ymax_q;

    wire [31:0] window_samples = COL_COUNT << timebase_q;
    wire [31:0] pre_samples = (COL_COUNT >> 2) << timebase_q;
    wire [31:0] valid_samples_wide = {{(31-RING_AW){1'b0}}, valid_samples_q};
    wire [2:0] bucket_last = 3'd7 >> (2'd3 - timebase_q);
    wire [31:0] capture_age = sample_count_o - capture_start_q;
    wire converting = (state_q == ST_POST) || (state_q == ST_READ) ||
                      (state_q == ST_MAP) || (state_q == ST_ACCUMULATE) ||
                      (state_q == ST_COLUMN) || (state_q == ST_PUBLISH);
    // At depth exactly, the oldest sample is still valid only if no new PCM
    // writes that edge.  Cancel conservatively before ANY same-address access.
    wire overwrite_risk = converting && ((capture_age > RING_DEPTH) ||
                          (pcm_valid_i && (capture_age >= RING_DEPTH)));
    wire cfg_accept = cfg_valid_i && cfg_ready_o;

    wire signed [23:0] trigger_pcm = channel_q ? pcm_right_i : pcm_left_i;
    wire signed [25:0] trigger_value = {{2{trigger_pcm[23]}}, trigger_pcm};
    wire signed [25:0] threshold_wide = {{2{threshold_q[23]}}, threshold_q};
    wire signed [25:0] hysteresis_wide = {2'b00, hysteresis_q};
    wire signed [25:0] arm_low = threshold_wide - hysteresis_wide;
    wire signed [25:0] arm_high = threshold_wide + hysteresis_wide;
    // Zero hysteresis still requires a real edge; a threshold-valued DC input
    // cannot repeatedly arm and trigger on its plateau.
    wire arm_condition = falling_q ?
        ((hysteresis_q == 0) ? (trigger_value > threshold_wide) :
                               (trigger_value >= arm_high)) :
        ((hysteresis_q == 0) ? (trigger_value < threshold_wide) :
                               (trigger_value <= arm_low));
    wire crossing_condition = falling_q ? (trigger_value <= threshold_wide) :
                                          (trigger_value >= threshold_wide);
    wire trigger_edge = trigger_armed_q && crossing_condition;
    wire timeout_reached = (timeout_q == 0) ||
                            ({1'b0, wait_samples_q} + 17'd1 >= {1'b0, timeout_q});

    // Map genuine samples into panel coordinates before min/max aggregation.
    // Only fixed right shifts and saturation: no smoothing or multiplier.
    function automatic [8:0] sample_y;
        input signed [23:0] sample;
        input [1:0] gain;
        reg signed [24:0] scaled;
        reg signed [24:0] position;
        begin
            case (gain)
                2'd0: scaled = $signed({sample[23], sample}) >>> 16;
                2'd1: scaled = $signed({sample[23], sample}) >>> 15;
                2'd2: scaled = $signed({sample[23], sample}) >>> 14;
                default: scaled = $signed({sample[23], sample}) >>> 13;
            endcase
            position = 25'sd128 - scaled;
            if (position < 0)
                sample_y = 9'd0;
            else if (position > 25'sd256)
                sample_y = 9'd256;
            else
                sample_y = position[8:0];
        end
    endfunction
    wire [8:0] read_left_y = sample_y($signed(ring_rd_data_i[23:0]), gain_q);
    wire [8:0] read_right_y = sample_y($signed(ring_rd_data_i[47:24]), gain_q);

    assign ring_wr_en_o = rst_n_i && pcm_valid_i;
    assign ring_wr_addr_o = sample_count_o[RING_AW-1:0];
    assign ring_wr_data_o = {16'd0, pcm_right_i, pcm_left_i};
    assign ring_rd_en_o = rst_n_i && (state_q == ST_READ) && !overwrite_risk;
    assign ring_rd_addr_o = read_sequence_q[RING_AW-1:0];
    assign feature_wr_en_o = rst_n_i && (state_q == ST_COLUMN) && !overwrite_risk;
    assign feature_wr_addr_o = {write_bank_q, column_q};
    assign feature_wr_data_o = {right_ymax_q, right_ymin_q, left_ymax_q, left_ymin_q};
    assign cfg_ready_o = rst_n_i && (state_q == ST_ARMED) &&
                         (ack_toggle_i == publish_toggle_o);
    assign waiting_o = rst_n_i && (state_q == ST_ARMED);

    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            state_q <= ST_ARMED;
            valid_samples_q <= 0;
            timebase_q <= DEFAULT_TIMEBASE[1:0];
            gain_q <= 2'd0;
            auto_q <= 1'b1;
            falling_q <= 1'b0;
            channel_q <= 1'b0;
            threshold_q <= 24'sd0;
            hysteresis_q <= 24'd65536;
            holdoff_q <= 16'd256;
            timeout_q <= 16'd9600;
            holdoff_remaining_q <= 0;
            wait_samples_q <= 0;
            trigger_armed_q <= 1'b0;
            capture_start_q <= 0;
            read_sequence_q <= 0;
            capture_triggered_q <= 1'b0;
            write_bank_q <= 1'b1;
            column_q <= 0;
            bucket_sample_q <= 0;
            mapped_left_y_q <= 0;
            mapped_right_y_q <= 0;
            left_ymin_q <= 0;
            left_ymax_q <= 0;
            right_ymin_q <= 0;
            right_ymax_q <= 0;
            publish_toggle_o <= 1'b0;
            publish_meta_o <= 0;
            sample_count_o <= 0;
            publish_count_o <= 0;
            overrun_count_o <= 0;
        end else begin
            if (pcm_valid_i) begin
                sample_count_o <= sample_count_o + 32'd1;
                if (valid_samples_wide < RING_DEPTH)
                    valid_samples_q <= valid_samples_q + 1'b1;
                if (holdoff_remaining_q != 0)
                    holdoff_remaining_q <= holdoff_remaining_q - 16'd1;
                // Track physical edges even while busy.  A crossing discarded
                // during a frozen capture cannot become a stale later trigger.
                if (arm_condition)
                    trigger_armed_q <= 1'b1;
                else if (trigger_edge)
                    trigger_armed_q <= 1'b0;
            end

            if (cfg_accept) begin
                timebase_q <= cfg_timebase_i;
                gain_q <= cfg_gain_i;
                auto_q <= cfg_auto_i;
                falling_q <= cfg_falling_i;
                channel_q <= cfg_channel_i;
                threshold_q <= cfg_threshold_i;
                hysteresis_q <= cfg_hysteresis_i;
                holdoff_q <= cfg_holdoff_i;
                timeout_q <= cfg_timeout_i;
                holdoff_remaining_q <= 0;
                wait_samples_q <= 0;
                trigger_armed_q <= 1'b0;
            end else if (overwrite_risk) begin
                state_q <= ST_ARMED;
                wait_samples_q <= 0;
                overrun_count_o <= overrun_count_o + 32'd1;
            end else begin
                case (state_q)
                    ST_ARMED: begin
                        if (pcm_valid_i) begin
                            if (wait_samples_q != 16'hffff)
                                wait_samples_q <= wait_samples_q + 16'd1;
                            if (trigger_edge && (holdoff_remaining_q == 0) &&
                                (valid_samples_wide >= pre_samples)) begin
                                capture_start_q <= sample_count_o - pre_samples;
                                capture_triggered_q <= 1'b1;
                                holdoff_remaining_q <= holdoff_q;
                                wait_samples_q <= 0;
                                state_q <= ST_POST;
                            end else if (auto_q && timeout_reached &&
                                         (valid_samples_wide + 32'd1 >= window_samples)) begin
                                // AUTO freezes the latest COMPLETE window,
                                // including the PCM sample written on this edge.
                                capture_start_q <= sample_count_o + 32'd1 - window_samples;
                                capture_triggered_q <= 1'b0;
                                wait_samples_q <= 0;
                                state_q <= ST_POST;
                            end
                        end
                    end
                    ST_POST: begin
                        if (capture_age >= window_samples) begin
                            read_sequence_q <= capture_start_q;
                            column_q <= 0;
                            bucket_sample_q <= 0;
                            state_q <= ST_READ;
                        end
                    end
                    ST_READ: begin
                        // RAM samples the request at this edge.  Its NBA output
                        // is consumed on the following ST_MAP edge.
                        state_q <= ST_MAP;
                    end
                    ST_MAP: begin
                        // Isolate RAM-to-coordinate mapping from the min/max
                        // compare enables. Both channels retain all 9 bits,
                        // including the saturated coordinate 256 endpoint.
                        mapped_left_y_q <= read_left_y;
                        mapped_right_y_q <= read_right_y;
                        state_q <= ST_ACCUMULATE;
                    end
                    ST_ACCUMULATE: begin
                        read_sequence_q <= read_sequence_q + 32'd1;
                        if (bucket_sample_q == 0) begin
                            left_ymin_q <= mapped_left_y_q;
                            left_ymax_q <= mapped_left_y_q;
                            right_ymin_q <= mapped_right_y_q;
                            right_ymax_q <= mapped_right_y_q;
                        end else begin
                            if (mapped_left_y_q < left_ymin_q) left_ymin_q <= mapped_left_y_q;
                            if (mapped_left_y_q > left_ymax_q) left_ymax_q <= mapped_left_y_q;
                            if (mapped_right_y_q < right_ymin_q) right_ymin_q <= mapped_right_y_q;
                            if (mapped_right_y_q > right_ymax_q) right_ymax_q <= mapped_right_y_q;
                        end
                        if (bucket_sample_q == bucket_last)
                            state_q <= ST_COLUMN;
                        else begin
                            bucket_sample_q <= bucket_sample_q + 3'd1;
                            state_q <= ST_READ;
                        end
                    end
                    ST_COLUMN: begin
                        if (&column_q)
                            state_q <= ST_PUBLISH;
                        else begin
                            column_q <= column_q + 1'b1;
                            bucket_sample_q <= 0;
                            state_q <= ST_READ;
                        end
                    end
                    ST_PUBLISH: begin
                        publish_meta_o <= {8'd0, threshold_q, 7'd0, write_bank_q,
                                           auto_q, channel_q, falling_q, capture_triggered_q,
                                           gain_q, timebase_q,
                                           publish_count_o[15:0] + 16'd1, capture_start_q};
                        publish_toggle_o <= ~publish_toggle_o;
                        publish_count_o <= publish_count_o + 32'd1;
                        state_q <= ST_ACK;
                    end
                    ST_ACK: begin
                        if (ack_toggle_i == publish_toggle_o) begin
                            write_bank_q <= ~write_bank_q;
                            wait_samples_q <= 0;
                            state_q <= ST_ARMED;
                        end
                    end
                    default: state_q <= ST_ARMED;
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if ((COLS < 4) || ((COLS & (COLS-1)) != 0))
            $fatal(1, "scope_acquisition COLS must be a power of two >= 4");
        if ((RING_AW < 1) || (RING_AW > 30) || (RING_DEPTH <= COLS*8))
            $fatal(1, "scope_acquisition ring must exceed largest capture window");
        if ((DEFAULT_TIMEBASE < 0) || (DEFAULT_TIMEBASE > 3))
            $fatal(1, "scope_acquisition DEFAULT_TIMEBASE must be 0..3");
    end
`endif
endmodule
