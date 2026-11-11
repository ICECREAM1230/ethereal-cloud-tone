// Copyright (c) 2026 ICECREAM1230
// Extracted from element_pipeline.sv without changing registered behavior.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
`default_nettype none
module master_limiter (
    input wire clk, rst_n, sample_tick,
    input wire signed [25:0] sum_left, sum_right,
    output logic signed [23:0] pcm_left, pcm_right
);
    function automatic logic signed [23:0] sat24(input logic signed [35:0] x);
        if (x > 36'sd8388607)
            sat24 = 24'sd8388607;
        else if (x < -36'sd8388608)
            sat24 = -24'sd8388608;
        else
            sat24 = x[23:0];
    endfunction


    //----------------------------------------------------------------
    // S11 master limiter (#121, deployment A): log-domain peak limiter
    // on the PRE-clip accumulators (attenuate before the clamp -- the
    // #43 lesson), stereo-linked on max(|L|,|R|), FEEDFORWARD (this
    // sample's level gates this sample), then sat24. Fully registered,
    // one operation class per stage (the silicon timing rule): the first
    // cut computed abs -> max -> lzc -> shift -> LUT -> adds -> LUT ->
    // shift in ONE cycle, passed STA, and sputtered at -63 dBFS on the
    // board (2026-09-18). 768 cycles of slack per sample, so the phase
    // walk is free:  p1 abs | p2 max -> level | p3..p7 limiter pipeline
    // settles (5 stages) | p8 latch gain_q + gain | p9 multiply | p10
    // sat24 publish. Every consumer latches mix_* at the NEXT tick, so
    // audio semantics are unchanged. lim_gain_q persists across samples
    // = the slow envelope. On this Q4.14 -> PCM seam, code 192 means
    // level 4096, or -6.02 dBFS (PCM full scale is acc 8192). With SUB=11,
    // attack_q=256 is 0.047 dB/sample and release_q=1 is 17.64 dB/s
    // at 96kHz. Applied gain additionally obeys the CURRENT peak target
    // instantly, with two log codes (~0.75 dB) of quantization headroom:
    // a large synthetic sum cannot clip its first frame while the slow
    // envelope catches up. sat24 remains the final numeric guard.
    //----------------------------------------------------------------
    localparam int          LIM_SUB       = 11;
    localparam logic [7:0]  LIM_THRESH    = 8'd192;
    localparam logic [18:0] LIM_ATTACK_Q  = 19'd256;
    localparam logic [18:0] LIM_RELEASE_Q = 19'd1;
    logic signed [25:0] lim_acc_l, lim_acc_r, lim_prod_l, lim_prod_r;
    logic        [25:0] lim_abs_l, lim_abs_r, lim_level;   // unsigned magnitudes
    logic        [18:0] lim_gain_q;                        // envelope state
    logic        [16:0] lim_gain;                          // UQ0.16 applied gain
    logic        [3:0]  lim_phase;
    wire         [18:0] lim_gain_q_next;
    wire         [16:0] lim_gain_lin;
    limiter #(.LEVEL_W(26), .SUB(LIM_SUB), .INSTANT_PEAK(1'b1),
              .HEADROOM_CODES(2)) u_lim (
        .clk(clk), .rst_n(rst_n),
        .level      (lim_level),
        .gain_q_in  (lim_gain_q),
        .thresh_code(LIM_THRESH),
        .attack_q   (LIM_ATTACK_Q),
        .release_q  (LIM_RELEASE_Q),
        .gain_q_out (lim_gain_q_next),
        .gain_lin   (lim_gain_lin)
    );
    // 26-bit acc x UQ0.16 gain -> back to the acc scale (>>16); gain <= 1
    // so it always fits. Registered-input, registered-output DSP.
    wire signed [43:0] lim_mul_l = lim_acc_l * $signed({1'b0, lim_gain});
    wire signed [43:0] lim_mul_r = lim_acc_r * $signed({1'b0, lim_gain});

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pcm_left   <= '0;
            pcm_right  <= '0;
            lim_acc_l  <= '0;
            lim_acc_r  <= '0;
            lim_abs_l  <= '0;
            lim_abs_r  <= '0;
            lim_level  <= '0;
            lim_prod_l <= '0;
            lim_prod_r <= '0;
            lim_gain_q <= '0;
            lim_gain   <= 17'h10000;
            lim_phase  <= 4'd0;
        end else begin
            if (sample_tick) begin
                // sample boundary: hold the finished pre-clear sum
                lim_acc_l  <= sum_left;
                lim_acc_r  <= sum_right;
                lim_phase  <= 4'd1;
            end
            case (lim_phase)
                4'd1: begin   // |acc|: explicit two's-complement negate on the bits
                    lim_abs_l <= lim_acc_l[25] ? (~lim_acc_l + 26'd1) : lim_acc_l;
                    lim_abs_r <= lim_acc_r[25] ? (~lim_acc_r + 26'd1) : lim_acc_r;
                    lim_phase <= 4'd2;
                end
                4'd2: begin   // stereo link: the louder channel gates both
                    lim_level <= (lim_abs_l > lim_abs_r) ? lim_abs_l : lim_abs_r;
                    lim_phase <= 4'd3;
                end
                4'd3, 4'd4, 4'd5, 4'd6, 4'd7:   // limiter pipeline settles
                    lim_phase <= lim_phase + 4'd1;
                4'd8: begin   // both outputs valid (gain_q from p7, gain from p8)
                    lim_gain_q <= lim_gain_q_next;
                    lim_gain   <= lim_gain_lin;
                    lim_phase  <= 4'd9;
                end
                4'd9: begin   // apply the gain
                    lim_prod_l <= 26'(lim_mul_l >>> 16);
                    lim_prod_r <= 26'(lim_mul_r >>> 16);
                    lim_phase  <= 4'd10;
                end
                4'd10: begin  // Q4.14 -> Q0.24, rail
                    pcm_left   <= sat24(36'(lim_prod_l) <<< 10);
                    pcm_right  <= sat24(36'(lim_prod_r) <<< 10);
                    lim_phase  <= 4'd0;
                end
                default: ;
            endcase
        end
    end

endmodule
`default_nettype wire
