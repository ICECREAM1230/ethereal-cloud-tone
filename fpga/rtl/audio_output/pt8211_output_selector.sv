`default_nettype none
// Two independent serial transmitters share the DAC pins through local gates.
// Gate changes happen one local clock after the final BCK falling edge, when
// frame_latched is observed. HALF_CYCLES >= 2 leaves that edge safely low.
module pt8211_output_selector #(
    parameter integer SYNTH_HALF_CYCLES = 12,
    parameter integer FM_HALF_CYCLES = 2
) (
    input  wire               sys_clk_i,
    input  wire               sys_rst_n_i,
    input  wire               fm_clk_i,
    input  wire               fm_rst_n_i,
    input  wire               request_fm_i,
    input  wire signed [23:0] synth_left_i,
    input  wire signed [23:0] synth_right_i,
    input  wire signed [23:0] fm_left_i,
    input  wire signed [23:0] fm_right_i,
    output wire               hp_bck_o,
    output wire               hp_ws_o,
    output wire               hp_data_o,
    output wire               pcm_bck_o, pcm_ws_o, pcm_data_o,
    output reg signed [23:0]  tap_left_o,
    output reg signed [23:0]  tap_right_o,
    output reg                tap_valid_o,
    output reg                tap_fm_o,
    output wire               fm_active_o,
    output wire               busy_o
);
// synthesis translate_off
`ifndef SYNTHESIS
    initial begin
        if (SYNTH_HALF_CYCLES < 2 || FM_HALF_CYCLES < 2)
            $fatal(1, "pt8211_output_selector requires BCK half-periods of at least two local clocks");
    end
`endif
// synthesis translate_on
    // Both fault inputs assert the FM path reset asynchronously. Even if a
    // system reset is released off the FM clock, release here is synchronous.
    wire fm_reset_request_n_w = sys_rst_n_i && fm_rst_n_i;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) reg [1:0] fm_reset_sync_r;
    always @(posedge fm_clk_i or negedge fm_reset_request_n_w) begin
        if (!fm_reset_request_n_w) fm_reset_sync_r <= 2'b00;
        else fm_reset_sync_r <= {fm_reset_sync_r[0], 1'b1};
    end
    wire fm_path_rst_n_w = fm_reset_sync_r[1];

    wire synth_bck_w, synth_ws_w, synth_data_w;
    wire synth_i2s_ws_w, fm_i2s_ws_w;
    wire synth_frame_accept_w, synth_frame_latched_w;
    wire fm_bck_w, fm_ws_w, fm_data_w;
    wire fm_frame_accept_w, fm_frame_latched_w;
    pt8211_tx #(.BCK_HALF_CYCLES(SYNTH_HALF_CYCLES)) u_synth_tx (
        .clk_i(sys_clk_i), .rst_n_i(sys_rst_n_i),
        .left_i(synth_left_i[23:8]), .right_i(synth_right_i[23:8]),
        .bck_o(synth_bck_w), .ws_o(synth_ws_w), .data_o(synth_data_w),
        .i2s_ws_o(synth_i2s_ws_w),
        .frame_accept_o(synth_frame_accept_w), .frame_latched_o(synth_frame_latched_w)
    );
    pt8211_tx #(.BCK_HALF_CYCLES(FM_HALF_CYCLES)) u_fm_tx (
        .clk_i(fm_clk_i), .rst_n_i(fm_path_rst_n_w),
        .left_i(fm_left_i[23:8]), .right_i(fm_right_i[23:8]),
        .bck_o(fm_bck_w), .ws_o(fm_ws_w), .data_o(fm_data_w),
        .i2s_ws_o(fm_i2s_ws_w),
        .frame_accept_o(fm_frame_accept_w), .frame_latched_o(fm_frame_latched_w)
    );

    reg synth_enable_req_r, fm_enable_req_r;
    reg synth_gate_r, fm_gate_r;
    (* ASYNC_REG = "TRUE" *) reg [1:0] fm_enable_sync_r;
    always @(posedge fm_clk_i or negedge fm_path_rst_n_w) begin
        if (!fm_path_rst_n_w) begin
            fm_enable_sync_r <= 2'b00;
            fm_gate_r <= 1'b0;
        end else begin
            fm_enable_sync_r <= {fm_enable_sync_r[0], fm_enable_req_r};
            if (fm_frame_latched_w) fm_gate_r <= fm_enable_sync_r[1];
        end
    end
    always @(posedge sys_clk_i or negedge sys_rst_n_i) begin
        if (!sys_rst_n_i) synth_gate_r <= 1'b0;
        else if (synth_frame_latched_w) synth_gate_r <= synth_enable_req_r;
    end

    (* ASYNC_REG = "TRUE" *) reg [1:0] fm_gate_sys_r;
    (* ASYNC_REG = "TRUE" *) reg [1:0] fm_ready_sys_r;
    always @(posedge sys_clk_i or negedge fm_path_rst_n_w) begin
        if (!fm_path_rst_n_w) begin
            fm_gate_sys_r <= 2'b00;
            fm_ready_sys_r <= 2'b00;
        end else begin
            fm_gate_sys_r <= {fm_gate_sys_r[0], fm_gate_r};
            fm_ready_sys_r <= {fm_ready_sys_r[0], 1'b1};
        end
    end

    localparam [2:0] STOP = 3'd0, QUIET = 3'd1, START_SYNTH = 3'd2,
                     START_FM = 3'd3, RUN_SYNTH = 3'd4, RUN_FM = 3'd5;
    reg [2:0] state_r;
    reg [4:0] quiet_count_r;
    always @(posedge sys_clk_i or negedge sys_rst_n_i) begin
        if (!sys_rst_n_i) begin
            synth_enable_req_r <= 1'b0;
            fm_enable_req_r <= 1'b0;
            quiet_count_r <= 5'd0;
            state_r <= STOP;
        end else begin
            case (state_r)
                STOP: begin
                    synth_enable_req_r <= 1'b0;
                    fm_enable_req_r <= 1'b0;
                    quiet_count_r <= 5'd0;
                    if (!synth_gate_r && !fm_gate_sys_r[1]) state_r <= QUIET;
                end
                QUIET: begin
                    if (synth_gate_r || fm_gate_sys_r[1]) begin
                        state_r <= STOP;
                        quiet_count_r <= 5'd0;
                    end else if (quiet_count_r != 5'd31) begin
                        quiet_count_r <= quiet_count_r + 1'b1;
                    end else if (!request_fm_i) begin
                        synth_enable_req_r <= 1'b1;
                        state_r <= START_SYNTH;
                    end else if (fm_ready_sys_r[1]) begin
                        fm_enable_req_r <= 1'b1;
                        state_r <= START_FM;
                    end
                end
                // Do not cancel an unacknowledged enable by reusing a stale
                // OFF acknowledgement. First observe ON, then close a full
                // frame if the latest request has reversed in the meantime.
                START_SYNTH: if (synth_gate_r) begin
                    if (request_fm_i) begin
                        synth_enable_req_r <= 1'b0;
                        state_r <= STOP;
                    end else state_r <= RUN_SYNTH;
                end
                START_FM: begin
                    if (!fm_ready_sys_r[1]) begin
                        // A reset is an explicit asynchronous OFF guarantee.
                        fm_enable_req_r <= 1'b0;
                        state_r <= STOP;
                    end else if (fm_gate_sys_r[1]) begin
                        if (!request_fm_i) begin
                            fm_enable_req_r <= 1'b0;
                            state_r <= STOP;
                        end else state_r <= RUN_FM;
                    end
                end
                RUN_SYNTH: if (request_fm_i) begin
                    synth_enable_req_r <= 1'b0;
                    state_r <= STOP;
                end
                RUN_FM: if (!request_fm_i || !fm_ready_sys_r[1]) begin
                    fm_enable_req_r <= 1'b0;
                    state_r <= STOP;
                end
                default: begin
                    synth_enable_req_r <= 1'b0;
                    fm_enable_req_r <= 1'b0;
                    quiet_count_r <= 5'd0;
                    state_r <= STOP;
                end
            endcase
        end
    end
    assign fm_active_o = fm_gate_sys_r[1];
    assign busy_o = !(((state_r == RUN_SYNTH) && !request_fm_i && synth_gate_r) ||
                      ((state_r == RUN_FM) && request_fm_i && fm_gate_sys_r[1] && fm_ready_sys_r[1]));

    // Each clock is gated in its own domain before the pin-level OR. Neither
    // the asynchronous request nor a raw cross-domain clock is a mux select.
    assign hp_bck_o = (synth_bck_w && synth_gate_r) || (fm_bck_w && fm_gate_r);
    assign hp_ws_o = (synth_ws_w && synth_gate_r) || (fm_ws_w && fm_gate_r);
    assign hp_data_o = (synth_data_w && synth_gate_r) || (fm_data_w && fm_gate_r);

    // Both DACs use exactly the same frame capture and safe source gates.
    assign pcm_bck_o = hp_bck_o;
    assign pcm_data_o = hp_data_o;
    assign pcm_ws_o = (synth_i2s_ws_w && synth_gate_r) || (fm_i2s_ws_w && fm_gate_r);

    reg [47:0] synth_pcm_r, fm_pcm_r;
    always @(posedge sys_clk_i or negedge sys_rst_n_i) begin
        if (!sys_rst_n_i) synth_pcm_r <= 48'd0;
        else if (synth_frame_accept_w) synth_pcm_r <= {synth_left_i, synth_right_i};
    end
    always @(posedge fm_clk_i or negedge fm_path_rst_n_w) begin
        if (!fm_path_rst_n_w) fm_pcm_r <= 48'd0;
        else if (fm_frame_accept_w) fm_pcm_r <= {fm_left_i, fm_right_i};
    end

    // The common reset flushes both ends at every mode departure, so an old
    // FM sample cannot emerge after the next synth/FM selection. The held-bus
    // module synchronizes its own reset release in both clock domains.
    wire fm_tap_reset_request_n_w = fm_path_rst_n_w && fm_enable_req_r;
    (* ASYNC_REG = "TRUE" *) reg [1:0] fm_tap_reset_sync_r;
    always @(posedge fm_clk_i or negedge fm_tap_reset_request_n_w) begin
        if (!fm_tap_reset_request_n_w) fm_tap_reset_sync_r <= 2'b00;
        else fm_tap_reset_sync_r <= {fm_tap_reset_sync_r[0], 1'b1};
    end
    reg [47:0] fm_tap_data_r;
    reg fm_tap_pending_r;
    wire fm_tap_ready_w;
    wire [47:0] fm_tap_data_w;
    wire fm_tap_valid_w;
    always @(posedge fm_clk_i or negedge fm_tap_reset_sync_r[1]) begin
        if (!fm_tap_reset_sync_r[1]) begin
            fm_tap_data_r <= 48'd0;
            fm_tap_pending_r <= 1'b0;
        end else begin
            if (fm_tap_pending_r && fm_tap_ready_w) fm_tap_pending_r <= 1'b0;
            if (fm_frame_latched_w && fm_enable_sync_r[1]) begin
                fm_tap_data_r <= fm_pcm_r;
                fm_tap_pending_r <= 1'b1;
            end
        end
    end
    held_bus_cdc #(.WIDTH(48)) u_fm_tap_cdc (
        .src_clk_i(fm_clk_i), .dst_clk_i(sys_clk_i), .rst_n_i(fm_tap_reset_request_n_w),
        .src_data_i(fm_tap_data_r), .src_valid_i(fm_tap_pending_r), .src_ready_o(fm_tap_ready_w),
        .dst_data_o(fm_tap_data_w), .dst_valid_o(fm_tap_valid_w), .dst_ready_i(1'b1)
    );
    always @(posedge sys_clk_i or negedge sys_rst_n_i) begin
        if (!sys_rst_n_i) begin
            tap_left_o <= 24'sd0;
            tap_right_o <= 24'sd0;
            tap_valid_o <= 1'b0;
            tap_fm_o <= 1'b0;
        end else begin
            tap_valid_o <= 1'b0;
            if (synth_frame_latched_w && synth_enable_req_r && !request_fm_i) begin
                tap_left_o <= $signed(synth_pcm_r[47:24]);
                tap_right_o <= $signed(synth_pcm_r[23:0]);
                tap_valid_o <= 1'b1;
                tap_fm_o <= 1'b0;
            end else if (fm_tap_valid_w && fm_enable_req_r && request_fm_i && fm_ready_sys_r[1]) begin
                tap_left_o <= $signed(fm_tap_data_w[47:24]);
                tap_right_o <= $signed(fm_tap_data_w[23:0]);
                tap_valid_o <= 1'b1;
                tap_fm_o <= 1'b1;
            end
        end
    end
// synthesis translate_off
`ifndef SYNTHESIS
    // With 73 MHz consuming each pair and a 128-FM-clock producer period,
    // the real mailbox must complete before the next frame is offered.
    always @(posedge fm_clk_i)
        if (fm_tap_reset_sync_r[1] && fm_frame_latched_w && fm_enable_sync_r[1] &&
            fm_tap_pending_r && !fm_tap_ready_w)
            $fatal(1, "pt8211_output_selector FM tap mailbox overrun");
`endif
// synthesis translate_on
endmodule
`default_nettype wire
