// Copyright (c) 2026 ICECREAM1230
//--------------------------------------------------------------------
// i2s_tx.sv — self-contained I2S master transmitter
//
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
//
// BCLK = sysclk / BCLK_DIV; one stereo frame = BCLK_PER_WS BCLKs.
// Defaults retain the original 73.728MHz / 12 / 64 = 96kHz use.
// The PCM audio clock instead uses BCLK_DIV=4 (256 sysclks/frame).
//
// sample_tick publishes an atomic stereo pair into the holds. At the
// left MSB boundary, snapshot BOTH holds for the entire outgoing frame.
// A coincident sample_tick is accepted directly, without a stale-frame
// delay. A later publication cannot change the outgoing right channel.
// All state uses sysclk: BCLK, WS and DIN are register outputs, and
// WS/DIN update only at BCLK falling enables. WS leads the next MSB
// by one BCLK; unused bits of each channel slot are zero.
//--------------------------------------------------------------------
`default_nettype none
module i2s_tx #(
    parameter integer BITS        = 24,
    parameter integer BCLK_DIV    = 12,
    parameter integer BCLK_PER_WS = 64
) (
    input wire logic                    sysclk,
    input wire logic                    rst_n,
    input wire logic                    sample_tick,
    input wire logic signed [BITS-1:0]   data_left,
    input wire logic signed [BITS-1:0]   data_right,
    output wire                         i2s_bclk,
    output logic                        i2s_lrclk,
    output wire                         sd,
    // Observation only: these strobes never request native audio samples.
    // frame_accept is high before the left-MSB capture edge; latched
    // confirms that the outgoing stereo frame has actually been fixed.
    output wire                         frame_accept_o,
    output logic                        frame_latched_o
);
    localparam integer BCLK_HALF = BCLK_DIV / 2;
    localparam integer BCLK_CNT_W = BCLK_HALF > 1 ? $clog2(BCLK_HALF) : 1;
    localparam integer WS_CNT_W = $clog2(BCLK_PER_WS);
    localparam integer SLOT_BITS = BCLK_PER_WS / 2;
    localparam logic [BCLK_CNT_W-1:0] HALF_LAST = BCLK_CNT_W'(BCLK_HALF-1);
    localparam logic [WS_CNT_W-1:0] FRAME_LAST = WS_CNT_W'(BCLK_PER_WS-1);
    localparam logic [WS_CNT_W-1:0] LEFT_LAST = WS_CNT_W'(SLOT_BITS-1);
    localparam logic [WS_CNT_W-1:0] RIGHT_FIRST = WS_CNT_W'(SLOT_BITS);
`ifndef SYNTHESIS
    initial begin
        if(BCLK_DIV < 2 || BCLK_DIV % 2 != 0)
            $fatal(1,"i2s_tx requires an even BCLK_DIV >= 2");
        if(BCLK_PER_WS < 4 || BCLK_PER_WS % 2 != 0 || BITS < 2 || BITS > SLOT_BITS)
            $fatal(1,"i2s_tx requires two equal slots containing BITS >= 2");
    end
`endif
    logic [BCLK_CNT_W-1:0] bclk_cnt;
    logic bclk_reg;
    // The first falling BCLK launches WS=left (last bit of the preceding
    // right slot). The second launches the first left MSB. At DIV=4 this
    // is sysclk edge8, exactly the existing PT8211 DIV=4 frame_accept.
    logic [WS_CNT_W-1:0] ws_cnt;
    wire [WS_CNT_W-1:0] next_bit = ws_cnt == FRAME_LAST ? '0 : ws_cnt + 1'b1;
    wire bclk_falling = (bclk_cnt == HALF_LAST) && bclk_reg;
    assign i2s_bclk = bclk_reg;
    assign frame_accept_o = rst_n && bclk_falling && next_bit == 0;

    logic signed [BITS-1:0] hold_l,hold_r,frame_r;
    logic [BITS-1:0] shift_register;
    assign sd = shift_register[BITS-1];
    always_ff @(posedge sysclk or negedge rst_n) begin
        if(!rst_n)begin
            bclk_cnt <= '0;bclk_reg <= 1'b0;
            ws_cnt <= WS_CNT_W'(BCLK_PER_WS-2);i2s_lrclk <= 1'b1;
            hold_l <= '0;hold_r <= '0;frame_r <= '0;shift_register <= '0;
            frame_latched_o <= 1'b0;
        end else begin
            frame_latched_o <= 1'b0;
            if(sample_tick)begin hold_l <= data_left;hold_r <= data_right;end
            if(bclk_cnt == HALF_LAST)begin
                bclk_cnt <= '0;bclk_reg <= ~bclk_reg;
                if(bclk_reg)begin
                    ws_cnt <= next_bit;
                    if(next_bit == FRAME_LAST)i2s_lrclk <= 1'b0;
                    else if(next_bit == LEFT_LAST)i2s_lrclk <= 1'b1;
                    if(next_bit == 0)begin
                        shift_register <= sample_tick ? data_left : hold_l;
                        frame_r <= sample_tick ? data_right : hold_r;
                        frame_latched_o <= 1'b1;
                    end else if(next_bit == RIGHT_FIRST)begin
                        shift_register <= frame_r;
                    end else begin
                        shift_register <= {shift_register[BITS-2:0],1'b0};
                    end
                end
            end else bclk_cnt <= bclk_cnt + 1'b1;
        end
    end
endmodule
`default_nettype wire
