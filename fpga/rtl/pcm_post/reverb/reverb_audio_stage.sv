// Frame adapter for the existing PCM -> PT8211 path.
// Delay acceptance until the selected source has published its frame.
// ENABLE=0 is a combinational bypass, preserving the original DAC schedule.
`default_nettype none
module reverb_audio_stage #(
    parameter bit ENABLE = 1'b1,
    parameter bit RUNTIME_PARAMS = 1'b0,
    parameter bit COMPACT = 1'b0,
    parameter integer INPUT_DELAY_CYCLES = 1,
    parameter logic [15:0] WET_Q15 = 16'd8192,
    parameter logic [15:0] DAMP_Q15 = 16'd8192
) (
    input  wire clk,
    input  wire rst_n,
    input  wire sample_tick,
    input  wire [15:0] wet_q15,
    input  wire [15:0] damp_q15,
    input  wire signed [23:0] pcm_l_in,
    input  wire signed [23:0] pcm_r_in,
    output wire signed [23:0] pcm_l_out,
    output wire signed [23:0] pcm_r_out,
    output wire out_valid,
    output wire ready,
    output wire late,
    output wire clipped
);
    generate
        if (ENABLE) begin : enabled
            logic [INPUT_DELAY_CYCLES-1:0] sample_tick_d;
            always_ff @(posedge clk or negedge rst_n)
                if (!rst_n) sample_tick_d <= '0;
                // Preserve the old OR's Z-to-X behavior in four-state simulation.
                else sample_tick_d <= INPUT_DELAY_CYCLES'({sample_tick_d,1'b0 | sample_tick});

            schroeder_reverb #(.COMPACT(COMPACT)) u_core (
                .clk(clk), .rst_n(rst_n), .clear(1'b0),
                .sample_tick(sample_tick_d[INPUT_DELAY_CYCLES-1]), .pcm_l_in(pcm_l_in), .pcm_r_in(pcm_r_in),
                .bypass(1'b0), .mute(1'b0),
                .wet_q15(RUNTIME_PARAMS ? wet_q15 : WET_Q15),
                .damp_q15(RUNTIME_PARAMS ? damp_q15 : DAMP_Q15),
                .feedback0_q15(16'd26621), .feedback1_q15(16'd25929),
                .feedback2_q15(16'd25248), .feedback3_q15(16'd24409),
                .pcm_l_out(pcm_l_out), .pcm_r_out(pcm_r_out), .out_valid(out_valid),
                .ready(ready), .busy(), .late(late), .clipped(clipped)
            );
        end else begin : disabled
            assign pcm_l_out = pcm_l_in;
            assign pcm_r_out = pcm_r_in;
            assign out_valid = sample_tick;
            assign ready = rst_n;
            assign late = 1'b0;
            assign clipped = 1'b0;
        end
    endgenerate
`ifndef SYNTHESIS
    initial assert (INPUT_DELAY_CYCLES >= 1)
        else $fatal(1,"reverb PCM acceptance delay must be positive");
`endif
endmodule
`default_nettype wire
