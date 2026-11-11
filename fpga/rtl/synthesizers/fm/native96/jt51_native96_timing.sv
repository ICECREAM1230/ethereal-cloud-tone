// SPDX-License-Identifier: GPL-3.0-or-later
// Native-96k time preservation, 2026-10-18.
// Legacy mode physical/frame ratio=3717/6380. Request mode keeps
// physical ratio63/176 at20MHz separate from sample-time ratio14931/25630.
// All signals are synchronous enables; clk is the sole physical clock.
module jt51_native96_timing #(
    parameter bit PCM_REQUEST_DRIVEN = 1'b0
) (
    input wire clk,
    input wire rst,
    input wire native_cen_p1_i,
    input wire native_zero_i,
    output wire legacy_base_ce_o,
    output wire legacy_cen_o,
    output wire legacy_cen_p1_o,
    output wire [4:0] legacy_cycles_o,
    output wire legacy_zero_o,
    output wire eg_time_step_o
);
    // Wall-time peripherals keep advancing while operators wait for another
    // request. EG's shared count advances on audio frames, not compute clocks.
    localparam integer RATE_NUM = PCM_REQUEST_DRIVEN ? 63 : 3717;
    localparam integer RATE_DEN = PCM_REQUEST_DRIVEN ? 176 : 6380;
    localparam integer FRAME_NUM = PCM_REQUEST_DRIVEN ? 14931 : 3717;
    localparam integer FRAME_DEN = PCM_REQUEST_DRIVEN ? 25630 : 6380;
    localparam integer BASE_W = $clog2(RATE_DEN);
    localparam integer FRAME_W = $clog2(FRAME_DEN);
    reg [BASE_W-1:0] native_fraction_q;
    reg [FRAME_W-1:0] frame_fraction_q;
    wire [BASE_W:0] native_sum = {1'b0,native_fraction_q} + (BASE_W+1)'(RATE_NUM);
    wire [FRAME_W:0] frame_sum = {1'b0,frame_fraction_q} + (FRAME_W+1)'(FRAME_NUM);
    reg [1:0] legacy_divider_q;
    reg [4:0] legacy_cycles_q;
    reg legacy_zero_q;
    wire [4:0] next_legacy_cycles = legacy_cycles_q + 5'd1;
    wire native_frame = native_cen_p1_i && native_zero_i;
    assign legacy_base_ce_o = !rst && native_sum >= RATE_DEN;
    assign legacy_cen_o = legacy_base_ce_o && !legacy_divider_q[0];
    assign legacy_cen_p1_o = legacy_base_ce_o && legacy_divider_q == 2'd0;
    assign legacy_cycles_o = legacy_cycles_q;
    assign legacy_zero_o = legacy_zero_q;
    // Frame quantization keeps one shared envelope count for all 32 operators.
    // This enables only eg_cnt_base/eg_cnt, never the operator pipeline.
    assign eg_time_step_o = !rst && native_frame && frame_sum >= FRAME_DEN;
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            native_fraction_q <= '0;
            frame_fraction_q <= '0;
            legacy_divider_q <= 2'd0;
            legacy_cycles_q <= 5'd0;
            legacy_zero_q <= 1'b0;
        end else begin
            native_fraction_q <= legacy_base_ce_o ?
                BASE_W'(native_sum - RATE_DEN) : native_sum[BASE_W-1:0];
            if (legacy_base_ce_o)
                legacy_divider_q <= legacy_divider_q + 2'd1;
            // Match jt51_reg cur/zero, including reset zero=0 while cur=0.
            if (legacy_cen_p1_o) begin
                legacy_cycles_q <= next_legacy_cycles;
                legacy_zero_q <= next_legacy_cycles == 5'd0;
            end
            if (native_frame)
                frame_fraction_q <= eg_time_step_o ?
                    FRAME_W'(frame_sum - FRAME_DEN) : frame_sum[FRAME_W-1:0];
        end
    end
endmodule
