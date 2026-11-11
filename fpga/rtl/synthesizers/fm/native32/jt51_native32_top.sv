// SPDX-License-Identifier: GPL-3.0-or-later
// 32 voices x four operators on one JT51-derived pipeline, II=1.
// 20MHz compute clock; sample cadence is owned by the common PCM requester.
// One steady request advances128 consecutive operator slots. State and every
// pipeline stage park together; no operator receives an extra sample update.
// Reset/Panic is held for 512 clocks by the prepared executor to fill serial state.
module jt51_native32_top #(
    parameter bit ENABLE_AE350_VOICE_OWNER = 1'b0,
    parameter bit PCM_REQUEST_DRIVEN = 1'b1
) (
    input [31:0] ae350_mute_i,
    output [31:0] ae350_idle_o,
    input clk_i,
    input rst_i,
    input panic_i,
    input cs_n_i,
    input wr_n_i,
    input a0_i,
    input [1:0] bank_i,
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
    wire core_rst = rst_i | panic_i;
    reg request_active;
    reg [63:0] request_tag;
    wire run_operators = !PCM_REQUEST_DRIVEN || core_rst || request_active;
    // cen_p1 is the one-slot-per-clock pipeline enable, not a divided clock.
    wire cen_p1 = run_operators;
    wire sample;
    assign pcm_request_ready_o = PCM_REQUEST_DRIVEN && !core_rst && !request_active;
    assign operator_step_o = cen_p1 && !core_rst;
    always @(posedge clk_i or posedge core_rst) begin
        if (core_rst) begin
            request_active <= 1'b0;
            request_tag <= 64'd0;
            pcm_request_done_o <= 1'b0;
            pcm_request_tag_o <= 64'd0;
        end else begin
            pcm_request_done_o <= 1'b0;
            if (PCM_REQUEST_DRIVEN) begin
                if (!request_active) begin
                    if (pcm_request_valid_i && pcm_request_ready_o) begin
                        request_active <= 1'b1;
                        request_tag <= pcm_request_tag_i;
                    end
                end else if (sample) begin
                    pcm_request_done_o <= 1'b1;
                    pcm_request_tag_o <= request_tag;
                    request_active <= 1'b0;
                end
            end
        end
    end
    reg [3:0] warmup_frames;
    always @(posedge clk_i or posedge core_rst) begin
        if (core_rst) warmup_frames <= 4'd8;
        else if (sample && warmup_frames != 4'd0)
            warmup_frames <= warmup_frames - 4'd1;
    end
    wire signed [15:0] core_xleft, core_xright, core_left, core_right;
    assign ready_o = ~core_rst & (warmup_frames == 4'd0);
    assign sample_valid_o = sample & ready_o;
    assign left_o = ready_o ? core_xleft : 16'sd0;
    assign right_o = ready_o ? core_xright : 16'sd0;
    assign chip_left_o = ready_o ? core_left : 16'sd0;
    assign chip_right_o = ready_o ? core_right : 16'sd0;
    jt51_native32 #(
        .ENABLE_AE350_VOICE_OWNER(ENABLE_AE350_VOICE_OWNER),
        .PCM_REQUEST_DRIVEN(PCM_REQUEST_DRIVEN)
    ) core (
        .ae350_mute_i(ae350_mute_i), .ae350_idle_o(ae350_idle_o),
        .rst(core_rst), .clk(clk_i), .cen(cen_p1), .cen_p1(cen_p1),
        .cs_n(cs_n_i), .wr_n(wr_n_i), .a0(a0_i), .bank_i(bank_i),
        .din(din_i), .dout(status_o), .ct1(), .ct2(), .irq_n(irq_n_o),
        .sample(sample), .left(core_left), .right(core_right),
        .xleft(core_xleft), .xright(core_xright)
    );
endmodule
