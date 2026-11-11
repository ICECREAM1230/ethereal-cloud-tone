// SPDX-License-Identifier: GPL-3.0-or-later
// Experiment wrapper, 2026-10-04. The upstream JT51 files are unmodified.
// Clock contract: clk_i = 2 * the intended YM2151 master clock.
// With clk_i = 7,159,090 Hz: cen = 3,579,545 Hz,
// cen_p1 = 1,789,772.5 Hz, Fs = clk_i / 128 = 55,930.390625 Hz.
// Host inputs are synchronous to clk_i. This is not an ESP32 CDC adapter.
// Panic clears all chip state and patch registers; hold it for >=128 clk_i.
module jt51_native_top (
    input clk_i,
    input rst_i,
    input panic_i,
    input cs_n_i,
    input wr_n_i,
    input a0_i,
    input [7:0] din_i,
    output [7:0] status_o,
    output irq_n_o,
    output ready_o,
    output sample_valid_o,
    output signed [15:0] left_o,
    output signed [15:0] right_o,
    output signed [15:0] chip_left_o,
    output signed [15:0] chip_right_o
);
    reg [1:0] divider;
    wire core_rst = rst_i | panic_i;
    always @(posedge clk_i or posedge core_rst) begin
        if (core_rst) divider <= 2'd0;
        else divider <= divider + 2'd1;
    end
    wire cen = ~divider[0];
    wire cen_p1 = divider == 2'd0;
    wire sample;
    // Upstream accumulators/pipeline stages do not all have reset values.
    // Suppress the first 8 raw frames after reset/Panic while these stages
    // refill. The core's register state and arithmetic remain unmodified.
    reg [3:0] warmup_frames;
    always @(posedge clk_i or posedge core_rst) begin
        if (core_rst) warmup_frames <= 4'd8;
        else if (sample && warmup_frames != 4'd0)
            warmup_frames <= warmup_frames - 4'd1;
    end
    wire signed [15:0] core_xleft, core_xright, core_left, core_right;
    assign ready_o = ~core_rst & (warmup_frames == 4'd0);
    assign sample_valid_o = sample & ready_o;
    // Hold all public PCM ports at known zero during reset and warmup.
    // Once ready, samples are held between valid strobes as in the core.
    assign left_o = ready_o ? core_xleft : 16'sd0;
    assign right_o = ready_o ? core_xright : 16'sd0;
    assign chip_left_o = ready_o ? core_left : 16'sd0;
    assign chip_right_o = ready_o ? core_right : 16'sd0;
    jt51 core (
        .rst(core_rst), .clk(clk_i), .cen(cen), .cen_p1(cen_p1),
        .cs_n(cs_n_i), .wr_n(wr_n_i), .a0(a0_i), .din(din_i),
        .dout(status_o), .ct1(), .ct2(), .irq_n(irq_n_o),
        .sample(sample), .left(core_left), .right(core_right),
        .xleft(core_xleft), .xright(core_xright)
    );
endmodule
