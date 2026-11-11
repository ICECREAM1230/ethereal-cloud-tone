// JT51 rational-rate stereo window interpolation, 2026-10-11.
// SPDX-License-Identifier: GPL-3.0-or-later
// SOURCE_RATE_NUM / SOURCE_RATE_DEN is the exact source/output sample ratio.
// The default preserves the legacy PLL ratio; native 96k uses 13746/13747.
// The upstream JT51 core and the official asynchronous FIFO are unchanged.
`default_nettype none
module fm_audio_resampler #(
    parameter integer SOURCE_RATE_NUM = 14931,
    parameter integer SOURCE_RATE_DEN = 25630
) (
    input  wire clk_i,
    input  wire rst_n_i,
    input  wire enable_i,
    input  wire flush_i,
    input  wire frame_tick_i,
    input  wire [31:0] fifo_data_i,
    input  wire fifo_empty_i,
    output wire fifo_read_o,
    output logic signed [23:0] left_o,
    output logic signed [23:0] right_o,
    output logic valid_o,
    output wire primed_o,
    output logic [31:0] underflow_count_o
);
    // One source endpoint at most is consumed per frame. The 16-bit phase and
    // remainder datapaths require 1 <= numerator <= denominator <= 65535.
    // Keep parameters wide until validation so out-of-range overrides cannot
    // silently wrap into an apparently legal 16-bit ratio.
    initial begin : check_source_rate
        if (SOURCE_RATE_NUM < 1 || SOURCE_RATE_NUM > 65535 ||
            SOURCE_RATE_DEN < 1 || SOURCE_RATE_DEN > 65535 ||
            SOURCE_RATE_NUM > SOURCE_RATE_DEN)
            $fatal(1, "fm_audio_resampler requires 1 <= SOURCE_RATE_NUM <= SOURCE_RATE_DEN <= 65535");
    end
    localparam logic [15:0] RATE_STEP = 16'(SOURCE_RATE_NUM);
    localparam logic [15:0] RATE_MOD = 16'(SOURCE_RATE_DEN);
    logic [31:0] head_a, head_b;
    logic has_a, has_b, read_pending;
    logic priming;
    logic [15:0] phase;
    wire [16:0] next_phase = {1'b0, phase} + {1'b0, RATE_STEP};

    // Standard (not FWFT) official FIFO: Q updates after the accepting edge.
    // Capture at the following edge without another RdEn. These are two
    // interpolation endpoints, not a replacement asynchronous FIFO.
    assign fifo_read_o = enable_i && !flush_i && !read_pending &&
                         !has_b && !fifo_empty_i;
    assign primed_o = has_a && has_b && enable_i && !flush_i;

    wire signed [16:0] delta_l = {head_b[31],head_b[31:16]} -
                                 {head_a[31],head_a[31:16]};
    wire signed [16:0] delta_r = {head_b[15],head_b[15:0]} -
                                 {head_a[15],head_a[15:0]};
    wire [32:0] product_l, product_r;
    generate if (SOURCE_RATE_NUM == SOURCE_RATE_DEN) begin : g_equal_rate
        // Reset and each completed window leave phase at zero for a 1:1
        // rate. Keep the original window/42-clock publication schedule,
        // but do not allocate DSPs for the identically zero interpolation.
        assign product_l = 33'd0;
        assign product_r = 33'd0;
    end else begin : g_fractional_rate
        post_fm_product u_product_l(.head_a(head_a[31:16]), .head_b(head_b[31:16]),
            .phase(phase), .product(product_l));
        post_fm_product u_product_r(.head_a(head_a[15:0]), .head_b(head_b[15:0]),
            .phase(phase), .product(product_r));
    end endgenerate

    // Divide in 41 clocks, then round the registered final result in one clock.
    // One job per 768-clock frame leaves ample deadline headroom and avoids a
    // wide combinational divide. All discarded fractional bits participate
    // in symmetric rounding. The exact rational phase never accumulates
    // a rounded clock-ratio error during a long note.
    logic dividing, finishing;
    logic [5:0] bit_count;
    logic [40:0] dividend_l, dividend_r;
    // |delta| < 65536 and phase < RATE_MOD, so the scaled quotient < 2^24.
    logic [23:0] quotient_l, quotient_r;
    logic [15:0] remainder_l, remainder_r;
    logic negative_l, negative_r;
    logic signed [15:0] base_l, base_r;
    wire [16:0] trial_l = {remainder_l, dividend_l[40]};
    wire [16:0] trial_r = {remainder_r, dividend_r[40]};
    wire subtract_l = trial_l >= {1'b0, RATE_MOD};
    wire subtract_r = trial_r >= {1'b0, RATE_MOD};
    wire [16:0] remainder_next_l = subtract_l ? trial_l - RATE_MOD : trial_l;
    wire [16:0] remainder_next_r = subtract_r ? trial_r - RATE_MOD : trial_r;
    wire [24:0] quotient_next_l = {quotient_l, subtract_l};
    wire [24:0] quotient_next_r = {quotient_r, subtract_r};
    wire [25:0] rounded_l = {2'b0, quotient_l} +
                           ({1'b0,remainder_l,1'b0} >= {2'b0, RATE_MOD});
    wire [25:0] rounded_r = {2'b0, quotient_r} +
                           ({1'b0,remainder_r,1'b0} >= {2'b0, RATE_MOD});
    wire signed [26:0] base_scaled_l = {{3{base_l[15]}},base_l,8'b0};
    wire signed [26:0] base_scaled_r = {{3{base_r[15]}},base_r,8'b0};
    wire signed [26:0] interpolated_l = negative_l ?
        base_scaled_l - $signed({1'b0,rounded_l}) :
        base_scaled_l + $signed({1'b0,rounded_l});
    wire signed [26:0] interpolated_r = negative_r ?
        base_scaled_r - $signed({1'b0,rounded_r}) :
        base_scaled_r + $signed({1'b0,rounded_r});
    function automatic logic signed [23:0] saturate24(input logic signed [26:0] x);
        if (x > 27'sd8388607) saturate24 = 24'sh7fffff;
        else if (x < -27'sd8388608) saturate24 = 24'sh800000;
        else saturate24 = x[23:0];
    endfunction

    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            head_a <= 0; head_b <= 0;
            has_a <= 0; has_b <= 0; read_pending <= 0; phase <= 0;
            priming <= 1;
            left_o <= 0; right_o <= 0; valid_o <= 0;
            dividing <= 0; finishing <= 0; bit_count <= 0;
            dividend_l <= 0; dividend_r <= 0;
            quotient_l <= 0; quotient_r <= 0;
            remainder_l <= 0; remainder_r <= 0;
            negative_l <= 0; negative_r <= 0; base_l <= 0; base_r <= 0;
            underflow_count_o <= 0;
        end else begin
            valid_o <= 0;
            if (!enable_i || flush_i) begin
                head_a <= 0; head_b <= 0;
                has_a <= 0; has_b <= 0; read_pending <= 0;
                phase <= 0; dividing <= 0; finishing <= 0; priming <= 1;
                left_o <= 0; right_o <= 0;
            end else begin
                if (fifo_read_o) read_pending <= 1;
                if (read_pending) begin
                    if (!has_a) begin head_a <= fifo_data_i; has_a <= 1; end
                    else begin head_b <= fifo_data_i; has_b <= 1; end
                    read_pending <= 0;
                end
                if (frame_tick_i) begin
                    if (primed_o && !dividing) begin
                        priming <= 0;
                        dividend_l <= {product_l,8'b0};
                        dividend_r <= {product_r,8'b0};
                        quotient_l <= 0; quotient_r <= 0;
                        remainder_l <= 0; remainder_r <= 0;
                        negative_l <= delta_l[16]; negative_r <= delta_r[16];
                        base_l <= head_a[31:16]; base_r <= head_a[15:0];
                        bit_count <= 6'd40; dividing <= 1; finishing <= 0;
                        if (SOURCE_RATE_NUM == SOURCE_RATE_DEN) begin
                            phase <= 16'd0;
                            head_a <= head_b; has_b <= 0;
                        end else if (next_phase >= {1'b0,RATE_MOD}) begin
                            phase <= 16'(next_phase - RATE_MOD);
                            head_a <= head_b; has_b <= 0;
                        end else phase <= next_phase[15:0];
                    end else begin
                        // The source is slower than output. During priming,
                        // retain the first fresh endpoint (and pending read)
                        // across silent ticks until the second can arrive.
                        // A playback underflow/deadline first discards the old
                        // window, then enters this fresh priming phase again.
                        if (!priming || dividing) begin
                            head_a <= 0; head_b <= 0;
                            has_a <= 0; has_b <= 0; read_pending <= 0;
                            phase <= 0; dividing <= 0; finishing <= 0; priming <= 1;
                        end
                        left_o <= 0; right_o <= 0; valid_o <= 1;
                        underflow_count_o <= underflow_count_o + 1'b1;
                    end
                end else if (dividing) begin
                    if (finishing) begin
                        left_o <= saturate24(interpolated_l);
                        right_o <= saturate24(interpolated_r);
                        valid_o <= 1; dividing <= 0; finishing <= 0;
                    end else begin
                        dividend_l <= {dividend_l[39:0],1'b0};
                        dividend_r <= {dividend_r[39:0],1'b0};
                        quotient_l <= quotient_next_l[23:0]; quotient_r <= quotient_next_r[23:0];
                        remainder_l <= remainder_next_l[15:0];
                        remainder_r <= remainder_next_r[15:0];
                        if (bit_count == 0) finishing <= 1;
                        else bit_count <= bit_count - 1'b1;
                    end
                end
            end
        end
    end
endmodule
`default_nettype wire
