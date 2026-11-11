// SPDX-License-Identifier: GPL-3.0-or-later
// Native-96k wrapper, derived from the 2026-10-04 experiment wrapper.
// Original upstream files remain unchanged; see native96/provenance.json.
// Legacy mode: dedicated 50MHz *29/118 =12,288,135.593Hz, one frame per
// 128 clocks =96,001.059322Hz. Request mode: independent 20MHz compute
// clock, one 128-clock operator job per common PCM request at
// 91015625/948 Hz =96,008.043249Hz; no free-running sample generation.
// Pitch/EG use sample-time ratio14931/25630 in request mode; LFO/noise/
// timers use physical clock ratio63/176 against 50MHz*63/440Hz.
// Host inputs are synchronous to clk_i. This is not an ESP32 CDC adapter.
// Panic clears all chip state and patch registers; hold it for >=128 clk_i.
module jt51_native96_top #(
    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,
    parameter bit PCM_REQUEST_DRIVEN = 1'b0
) (
    input [7:0] ae350_mute_i,
    output [7:0] ae350_idle_o,
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
    output signed [15:0] chip_right_o,
    input wire pcm_request_valid_i,
    output wire pcm_request_ready_o,
    input wire [63:0] pcm_request_tag_i,
    output reg pcm_request_done_o,
    output reg [63:0] pcm_request_tag_o,
    output wire operator_step_o
);
    reg [1:0] divider;
    reg request_active, request_finishing;
    reg [63:0] request_tag;
    wire core_rst = rst_i | panic_i;
    // JT51 shift-state reset is a synchronous fill through cen, not an
    // asynchronous clear. Keep the operator enables active while core_rst is
    // held, exactly as the legacy wrapper does with divider=0.
    wire run_operators = !PCM_REQUEST_DRIVEN || core_rst || request_active;
    assign pcm_request_ready_o = PCM_REQUEST_DRIVEN && !core_rst && !request_active;
    assign operator_step_o = run_operators && !core_rst;
    always @(posedge clk_i or posedge core_rst) begin
        if (core_rst) begin
            divider <= 2'd0;
            request_active <= 1'b0;
            request_finishing <= 1'b0;
            request_tag <= 64'd0;
            pcm_request_done_o <= 1'b0;
            pcm_request_tag_o <= 64'd0;
        end else begin
            pcm_request_done_o <= 1'b0;
            if (!PCM_REQUEST_DRIVEN) divider <= divider + 2'd1;
            else if (!request_active) begin
                if (pcm_request_valid_i && pcm_request_ready_o) begin
                    request_active <= 1'b1;
                    request_finishing <= 1'b0;
                    request_tag <= pcm_request_tag_i;
                end
            end else begin
                divider <= divider + 2'd1;
                if (sample) begin
                    pcm_request_done_o <= 1'b1;
                    pcm_request_tag_o <= request_tag;
                    request_finishing <= 1'b1;
                end
                // Complete the four-clock operator slot before parking. The
                // initial request includes the reset zero-slot alignment;
                // subsequent jobs execute exactly one full 32-slot frame.
                if (request_finishing && divider == 2'd3) begin
                    request_active <= 1'b0;
                    request_finishing <= 1'b0;
                end
            end
        end
    end
    wire cen = run_operators && ~divider[0];
    wire cen_p1 = run_operators && divider == 2'd0;
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
    jt51_native96 #(.ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),.PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN)) core (
        .ae350_mute_i(ae350_mute_i),.ae350_idle_o(ae350_idle_o),
        .rst(core_rst), .clk(clk_i), .cen(cen), .cen_p1(cen_p1),
        .cs_n(cs_n_i), .wr_n(wr_n_i), .a0(a0_i), .din(din_i),
        .dout(status_o), .ct1(), .ct2(), .irq_n(irq_n_o),
        .sample(sample), .left(core_left), .right(core_right),
        .xleft(core_xleft), .xright(core_xright)
    );
endmodule
