// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Two independent state owners, one physical 11-stage DDS in shared mode.
// Subtractive requests retain their original acceptance edge. The additive
// scheduler reserves a non-overlapping window; ready also makes arbitration
// explicit for independent clients. Only accepted requests enter owner_pipe.
`default_nettype none
module shared_dds_service #(
    parameter bit SHARE_DDS = 1'b1
) (
    input wire clk, rst_n,
    input wire sub_valid,
    input wire [13:0] sub_pitch,
    input wire [23:0] sub_phase, sub_duty,
    input wire [1:0] sub_wave,
    output wire sub_out_valid,
    output wire [23:0] sub_new_phase,
    output wire signed [17:0] sub_sample,
    input wire add_valid,
    input wire [23:0] add_phase, add_step,
    output wire add_ready, add_out_valid,
    output wire [23:0] add_new_phase,
    output wire signed [17:0] add_sample
);
    localparam int LATENCY = dds_antialias_pkg::OSCILLATOR_LATENCY;
    generate if (SHARE_DDS) begin : g_shared
        wire take_add = add_valid && add_ready;
        wire valid;
        wire [23:0] phase;
        wire signed [17:0] audio;
        logic [LATENCY-1:0] owner_pipe;
        assign add_ready = !sub_valid;
        always_ff @(posedge clk or negedge rst_n)
            if (!rst_n) owner_pipe <= '0;
            else owner_pipe <= {owner_pipe[LATENCY-2:0],take_add};
        dds_antialias_oscillator #(.SUPPORT_DIRECT_STEP(1'b1)) u_dds (
            .clk,.rst_n,.in_valid(sub_valid || take_add),
            .pitch_code(sub_pitch),.phase_in(take_add ? add_phase : sub_phase),
            .duty(sub_duty),.wave(take_add ? 2'd3 : sub_wave),
            .direct_step_enable(take_add),.direct_step(add_step),
            .out_valid(valid),.phase_out(phase),.sample_out(audio)
        );
        assign sub_out_valid = valid && !owner_pipe[LATENCY-1];
        assign add_out_valid = valid && owner_pipe[LATENCY-1];
        assign sub_new_phase = phase;
        assign add_new_phase = phase;
        assign sub_sample = audio;
        assign add_sample = audio;
    end else begin : g_separate
        assign add_ready = 1'b1;
        dds_antialias_oscillator u_sub (
            .clk,.rst_n,.in_valid(sub_valid),.pitch_code(sub_pitch),.phase_in(sub_phase),
            .duty(sub_duty),.wave(sub_wave),.direct_step_enable(1'b0),.direct_step(24'd0),
            .out_valid(sub_out_valid),.phase_out(sub_new_phase),.sample_out(sub_sample)
        );
        dds_antialias_oscillator #(.SUPPORT_DIRECT_STEP(1'b1)) u_add (
            .clk,.rst_n,.in_valid(add_valid),.pitch_code(14'd0),.phase_in(add_phase),
            .duty(24'd0),.wave(2'd3),.direct_step_enable(1'b1),.direct_step(add_step),
            .out_valid(add_out_valid),.phase_out(add_new_phase),.sample_out(add_sample)
        );
    end endgenerate
endmodule
`default_nettype wire
