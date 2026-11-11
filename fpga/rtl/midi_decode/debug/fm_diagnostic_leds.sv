`default_nettype none
// Observation only: no diagnostic signal feeds the musical control path.
// Sticky milestones clear on leaving FM or global reset. Clock health is live.
module fm_diagnostic_leds #(
    parameter integer REF_CLK_HZ = 50000000,
    parameter integer HEARTBEAT_BITS = 18,
    parameter integer WATCHDOG_CYCLES = REF_CLK_HZ / 10,
    parameter integer BLINK_HALF_CYCLES = REF_CLK_HZ / 2,
    parameter bit ACTIVE_LOW = 1'b1
) (
    input wire ref_clk_i, sys_clk_i, fm_clk_i, rst_n_i, core_rst_n_i,
    input wire requested_i, parsed_note_i, serial_nonzero_i, local_mute_i,
    // FM [7:4] = {published PCM seen, native PCM seen, NOTE_ON done, native note};
    // Request mode observes tagged response/publication; legacy uses FIFO/SRC.
    // [3:0] = {unmuted, backend-ready, selected, reset-n}.
    input wire [7:0] fm_status_i,
    input wire post_valid_i,
    input wire signed [23:0] post_left_i, post_right_i,
    output wire [7:0] led_o
);
    localparam integer WW = WATCHDOG_CYCLES > 1 ? $clog2(WATCHDOG_CYCLES) : 1;
    localparam integer BW = BLINK_HALF_CYCLES > 1 ? $clog2(BLINK_HALF_CYCLES) : 1;
    reg [HEARTBEAT_BITS-1:0] fm_heartbeat_r;
    reg post_seen_r;
    reg [1:0] sys_seen_r;
    always @(posedge fm_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin fm_heartbeat_r <= '0; post_seen_r <= 1'b0; end
        else begin
            fm_heartbeat_r <= fm_heartbeat_r + 1'b1;
            if (!fm_status_i[0] || !fm_status_i[1]) post_seen_r <= 1'b0;
            else if (fm_status_i[7] && post_valid_i &&
                     ((post_left_i != 0) || (post_right_i != 0))) post_seen_r <= 1'b1;
        end
    end
    always @(posedge sys_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) sys_seen_r <= 2'b00;
        else if (!core_rst_n_i || !requested_i) sys_seen_r <= 2'b00;
        else begin
            if (parsed_note_i) sys_seen_r[0] <= 1'b1;
            if (serial_nonzero_i) sys_seen_r[1] <= 1'b1;
        end
    end

    // Single-bit health and sticky flags cross independently through 2FFs.
    (* ASYNC_REG="TRUE" *) reg [9:0] fm_meta_r, fm_sync_r;
    (* ASYNC_REG="TRUE" *) reg [3:0] sys_meta_r, sys_sync_r;
    reg beat_seen_r, blink_r;
    reg [WW-1:0] watchdog_r;
    reg [BW-1:0] blink_count_r;
    reg clock_alive_r;
    always @(posedge ref_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            fm_meta_r <= '0; fm_sync_r <= '0; sys_meta_r <= '0; sys_sync_r <= '0;
            beat_seen_r <= 1'b0; watchdog_r <= '0; clock_alive_r <= 1'b0;
            blink_count_r <= '0; blink_r <= 1'b0;
        end else begin
            fm_meta_r <= {fm_heartbeat_r[HEARTBEAT_BITS-1], post_seen_r, fm_status_i};
            fm_sync_r <= fm_meta_r;
            sys_meta_r <= {requested_i, local_mute_i, sys_seen_r};
            sys_sync_r <= sys_meta_r;
            beat_seen_r <= fm_sync_r[9];
            if (fm_sync_r[9] != beat_seen_r) begin
                watchdog_r <= '0; clock_alive_r <= 1'b1;
            end else if (watchdog_r == WW'(WATCHDOG_CYCLES-1)) clock_alive_r <= 1'b0;
            else watchdog_r <= watchdog_r + 1'b1;
            if (blink_count_r == BW'(BLINK_HALF_CYCLES-1)) begin
                blink_count_r <= '0; blink_r <= !blink_r;
            end else blink_count_r <= blink_count_r + 1'b1;
        end
    end
    wire live = clock_alive_r && fm_sync_r[0];
    wire fm_requested = sys_sync_r[3];
    wire [7:0] on_w = {
        fm_requested && sys_sync_r[1],                 // 7: DAC PCM16 frame observed
        fm_requested && fm_sync_r[8] && fm_sync_r[7], // 6: published and postprocessed PCM24 nonzero
        fm_requested && fm_sync_r[6],                 // 5: native PCM seen (legacy FIFO read)
        fm_requested && fm_sync_r[5],                 // 4: backend NOTE_ON completed OK
        fm_requested && fm_sync_r[4],                 // 3: native MIDI NOTE_ON accepted
        fm_requested && sys_sync_r[0],                // 2: shared decoder presents NOTE_ON
        live && fm_requested && !sys_sync_r[2] && (&fm_sync_r[3:1]), // 1: selected/ready/unmuted
        live && blink_r                               // 0: live FM clock/reset heartbeat
    };
    assign led_o = ACTIVE_LOW ? ~on_w : on_w;
`ifndef SYNTHESIS
    initial if (HEARTBEAT_BITS < 1 || WATCHDOG_CYCLES < 2 || BLINK_HALF_CYCLES < 1)
        $fatal(1, "invalid FM diagnostic timing parameters");
`endif
endmodule
`default_nettype wire
