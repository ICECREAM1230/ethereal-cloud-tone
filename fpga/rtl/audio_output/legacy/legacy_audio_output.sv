`default_nettype none
module legacy_audio_output (
    input wire sysclk,rst_n,sample_tick,cell_tick,sample_tick48,cell_tick48,
    input wire signed [23:0] out_left,out_right,
    output wire spdif_out,spdif48_out,i2s_bclk,i2s_lrclk,i2s_data
);
    //----------------------------------------------------------------
    // SPDIF transmitter
    //----------------------------------------------------------------
    spdif_tx u_spdif (
        .clk         (sysclk),
        .rst_n       (rst_n),
        .sample_tick (sample_tick),
        .cell_tick   (cell_tick),
        .audio_l     (out_left),
        .audio_r     (out_right),
        .spdif_out   (spdif_out)
    );

    //----------------------------------------------------------------
    // 48 kHz SPDIF (#101) — THE PRIMARY AUDIO PATH (Thor 2026-09-11):
    // pin 27 feeds the Focusrite coax (listening) and a red LED
    // (68 Ω series) taped into the dev box's ICUSBAUDIO7D optical
    // input (bit-perfect capture); its CM106 receiver caps at 48 kHz,
    // hence the second transmitter instead of a tap on the 96 kHz one.
    // Decimation by 2 with pair averaging: a 2-tap boxcar whose null
    // sits at 48 kHz — content near the new Nyquist (24 kHz) is
    // already crushed by the 2 kHz master tilt, so no longer filter
    // is warranted. sample_tick48 coincides with a sample_tick, so
    // out_left/out_right below are the very values the 96 kHz
    // transmitter latches on the same edge: the held register is the
    // previous sample, the wire is the current one.
    //----------------------------------------------------------------
    logic signed [23:0] dec48_l_prev, dec48_r_prev;
    always_ff @(posedge sysclk or negedge rst_n)
        if (!rst_n) begin
            dec48_l_prev <= '0;
            dec48_r_prev <= '0;
        end else if (sample_tick) begin
            dec48_l_prev <= out_left;
            dec48_r_prev <= out_right;
        end

    // A 24+24 sum needs 25 bits; halving brings it back into 24.
    wire signed [23:0] dec48_l =
        24'((25'(out_left)  + 25'(dec48_l_prev)) >>> 1);
    wire signed [23:0] dec48_r =
        24'((25'(out_right) + 25'(dec48_r_prev)) >>> 1);

    spdif_tx #(.CS_FREQ(8'h02)) u_spdif48 (   // 0x02 = 48 kHz
        .clk         (sysclk),
        .rst_n       (rst_n),
        .sample_tick (sample_tick48),
        .cell_tick   (cell_tick48),
        .audio_l     (dec48_l),
        .audio_r     (dec48_r),
        .spdif_out   (spdif48_out)
    );

    //----------------------------------------------------------------
    // I2S master transmitter (generates its own BCLK/LRCLK)
    //----------------------------------------------------------------
    i2s_tx #(.BITS(24)) u_i2s_tx (
        .sysclk      (sysclk),
        .rst_n       (rst_n),
        .sample_tick (sample_tick),
        .data_left   (out_left),
        .data_right  (out_right),
        .i2s_bclk    (i2s_bclk),
        .i2s_lrclk   (i2s_lrclk),
        .sd          (i2s_data)
    );


endmodule
`default_nettype wire
