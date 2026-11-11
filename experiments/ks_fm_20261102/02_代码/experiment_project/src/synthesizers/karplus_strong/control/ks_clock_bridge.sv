// Copyright (c) 2026 ICECREAM1230
// Atomic command/status and audio interfaces to the native KS clock.
// Copyright (c) 2026 project contributors. License: MIT.
`default_nettype none
module ks_clock_bridge #(
    parameter bit SPLIT_AUDIO_CLOCK = 1'b0,
    parameter bit AUDIO_BRIDGES = 1'b1,
    parameter integer COMMAND_WIDTH = 167,
    parameter integer STATUS_WIDTH = 721,
    parameter integer PCM_WIDTH = 48
) (
    input  wire         src_clk_i,
    input  wire         ks_clk_i,
    input  wire         rst_n_i,
    input  wire         sample_tick_i,
    output wire         sample_tick_o,
    input  wire [COMMAND_WIDTH-1:0] command_i,
    input  wire         command_valid_i,
    output wire         command_ready_o,
    output wire [COMMAND_WIDTH-1:0] command_o,
    output wire         command_valid_o,
    input  wire         command_ready_i,
    input  wire [STATUS_WIDTH-1:0] status_i,
    output wire [STATUS_WIDTH-1:0] status_o,
    input  wire [PCM_WIDTH-1:0] pcm_i,
    input  wire         pcm_valid_i,
    output wire [PCM_WIDTH-1:0] pcm_o,
    output wire         pcm_valid_o,
    output logic [31:0] sample_overrun_o,
    output logic [31:0] pcm_overrun_o,
    input  wire         audio_clk_i,
    input  wire         audio_rst_n_i
);
    // rst_n_i is shared RAW reset, including loss of either clock's PLL lock.
    // Assertion clears both CDC ends even with a clock stopped. Local release
    // matches held_bus_cdc's two-clock qualifier. The parent releases its
    // control/core producers after three local clocks. Pulses before these
    // local qualifiers release are ignored and are not running overruns.
    wire shared_rst_n = rst_n_i && (!SPLIT_AUDIO_CLOCK || audio_rst_n_i);
    wire audio_clk = SPLIT_AUDIO_CLOCK ? audio_clk_i : src_clk_i;
    wire audio_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_meta, src_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic ks_reset_meta, ks_reset_sync;
    always_ff @(posedge src_clk_i or negedge shared_rst_n) begin
        if (!shared_rst_n) begin
            src_reset_meta <= 1'b0;
            src_reset_sync <= 1'b0;
        end else begin
            src_reset_meta <= 1'b1;
            src_reset_sync <= src_reset_meta;
        end
    end
    always_ff @(posedge ks_clk_i or negedge shared_rst_n) begin
        if (!shared_rst_n) begin
            ks_reset_meta <= 1'b0;
            ks_reset_sync <= 1'b0;
        end else begin
            ks_reset_meta <= 1'b1;
            ks_reset_sync <= ks_reset_meta;
        end
    end
    generate if (SPLIT_AUDIO_CLOCK) begin : g_audio_reset
        (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic reset_meta, reset_sync;
        always_ff @(posedge audio_clk or negedge shared_rst_n) begin
            if (!shared_rst_n) begin
                reset_meta <= 1'b0;
                reset_sync <= 1'b0;
            end else begin
                reset_meta <= 1'b1;
                reset_sync <= reset_meta;
            end
        end
        assign audio_reset_sync = reset_sync;
    end else begin : g_shared_audio_reset
        assign audio_reset_sync = src_reset_sync;
    end endgenerate

    // A periodic pulse cannot apply backpressure to its producer. Keep one
    // pending event in addition to the CDC's in-flight event. On simultaneous
    // dequeue/new input, retain the new event for the next handshake.
    logic sample_pending;
    wire sample_ready;
    wire unused_sample_data;
    held_bus_cdc #(.WIDTH(1)) u_sample_tick_cdc (
        .src_clk_i(audio_clk), .dst_clk_i(ks_clk_i), .rst_n_i(shared_rst_n),
        .src_data_i(1'b1),
        .src_valid_i(AUDIO_BRIDGES && audio_reset_sync && (sample_pending || sample_tick_i)),
        .src_ready_o(sample_ready), .dst_data_o(unused_sample_data),
        .dst_valid_o(sample_tick_o), .dst_ready_i(1'b1)
    );
    always_ff @(posedge audio_clk or negedge audio_reset_sync) begin
        if (!audio_reset_sync) begin
            sample_pending <= 1'b0;
            sample_overrun_o <= '0;
        end else if (sample_pending) begin
            if (sample_ready) begin
                sample_pending <= sample_tick_i;
            end else if (sample_tick_i && sample_overrun_o != 32'hffffffff) begin
                sample_overrun_o <= sample_overrun_o + 1'b1;
            end
        end else if (sample_tick_i && !sample_ready) begin
            sample_pending <= 1'b1;
        end
    end

    // The producer owns this payload through its source-side handshake;
    // held_bus_cdc owns the captured payload until KS accepts the command.
    held_bus_cdc #(.WIDTH(COMMAND_WIDTH), .PRELOAD_WHEN_READY(1'b1)) u_command_cdc (
        .src_clk_i(src_clk_i), .dst_clk_i(ks_clk_i), .rst_n_i(shared_rst_n),
        .src_data_i(command_i), .src_valid_i(command_valid_i),
        .src_ready_o(command_ready_o), .dst_data_o(command_o),
        .dst_valid_o(command_valid_o), .dst_ready_i(command_ready_i)
    );

    // Every source handshake captures a fresh complete status vector. The
    // receiver is always ready; individual status bits never cross alone.
    wire unused_status_ready, unused_status_valid;
    held_bus_cdc #(.WIDTH(STATUS_WIDTH)) u_status_cdc (
        .src_clk_i(ks_clk_i), .dst_clk_i(src_clk_i), .rst_n_i(shared_rst_n),
        .src_data_i(status_i), .src_valid_i(1'b1), .src_ready_o(unused_status_ready),
        .dst_data_o(status_o), .dst_valid_o(unused_status_valid), .dst_ready_i(1'b1)
    );

    logic pcm_pending;
    logic [PCM_WIDTH-1:0] pending_pcm;
    wire pcm_ready;
    held_bus_cdc #(.WIDTH(PCM_WIDTH)) u_pcm_cdc (
        .src_clk_i(ks_clk_i), .dst_clk_i(audio_clk), .rst_n_i(shared_rst_n),
        .src_data_i(pcm_pending ? pending_pcm : pcm_i),
        .src_valid_i(AUDIO_BRIDGES && ks_reset_sync && (pcm_pending || pcm_valid_i)),
        .src_ready_o(pcm_ready), .dst_data_o(pcm_o),
        .dst_valid_o(pcm_valid_o), .dst_ready_i(1'b1)
    );
    always_ff @(posedge ks_clk_i or negedge ks_reset_sync) begin
        if (!ks_reset_sync) begin
            pcm_pending <= 1'b0;
            pending_pcm <= '0;
            pcm_overrun_o <= '0;
        end else if (pcm_pending) begin
            if (pcm_ready) begin
                pcm_pending <= pcm_valid_i;
                if (pcm_valid_i) pending_pcm <= pcm_i;
            end else if (pcm_valid_i && pcm_overrun_o != 32'hffffffff) begin
                pcm_overrun_o <= pcm_overrun_o + 1'b1;
            end
        end else if (pcm_valid_i && !pcm_ready) begin
            pcm_pending <= 1'b1;
            pending_pcm <= pcm_i;
        end
    end
endmodule
`default_nettype wire
