// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// FLU1 control wrapper; see README.md for CDC, cancellation and control limits.
`default_nettype none
module flute_engine (
    input wire control_clk_i, rst_n_i, audio_clk_i, audio_rst_n_i,
    input wire sample_tick48_i, enable_i, audio_enable_i,
    input midi_pkg::midi_event_t midi_event_i,
    input wire midi_valid_i,
    output wire midi_ready_o, midi_delivered_o, midi_drained_o,
    input wire [3:0] flush_i,
    input wire cancel_i,
    input wire local_select_valid_i, local_select_bind_i,
    input wire [31:0] local_select_id_i,
    input wire local_switch_busy_i,
    output wire local_select_ready_o,
    output logic local_select_done_o,
    output logic [31:0] local_select_done_id_o, local_select_result_o,
    output logic selected_o,
    input wire reg_clk_i, reg_write_i,
    input wire [7:0] reg_addr_i,
    input wire [31:0] reg_data_i,
    input wire [7:0] reg_read_addr_i,
    output logic [31:0] reg_read_data_o,
    output logic signed [23:0] pcm_o,
    output logic pcm_valid_o,
    output wire quiesced_o
);
    logic [31:0] last_mode, control_reject;
    logic [2:0] local_tail, mode_local_history;
    logic [31:0] mode_payload;
    logic mode_req, mode_ack, mode_overflow;
    (* ASYNC_REG="TRUE" *) logic mode_ack_meta, mode_ack_sync;
    (* ASYNC_REG="TRUE" *) logic mode_req_meta, mode_req_sync;
    (* ASYNC_REG="TRUE" *) logic mode_overflow_meta, mode_overflow_sync;
    wire mode_write = mode_req_sync != mode_ack;
    wire mode_source_busy = mode_req != mode_ack_sync;
    // The final SPI-word edge captures the complete request. No subsequent
    // SPI edge is needed for publication or destination acknowledgement.
    // A busy source retains its payload until the returned ACK is observed.
    always_ff @(posedge reg_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            mode_payload <= 0; mode_req <= 0; mode_overflow <= 0;
            mode_ack_meta <= 0; mode_ack_sync <= 0;
        end else begin
            mode_ack_meta <= mode_ack; mode_ack_sync <= mode_ack_meta;
            if (reg_write_i && reg_addr_i == 8'hb2) begin
                if (mode_source_busy) mode_overflow <= 1;
                else begin mode_payload <= reg_data_i; mode_req <= ~mode_req; end
            end
        end
    end
    logic audio_enable_previous;
    wire audio_enable_fell = audio_enable_previous && !audio_enable_sync;
    (* ASYNC_REG="TRUE" *) logic audio_enable_meta, audio_enable_sync;
    wire local_accept = local_select_valid_i && local_select_ready_o;
    assign local_select_ready_o = rst_n_i && !cancel_i;
    wire local_fence = local_switch_busy_i || local_accept || (|local_tail);
    always_ff @(posedge control_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            selected_o <= 0; last_mode <= 0; control_reject <= 0; local_tail <= 0;
            mode_req_meta <= 0; mode_req_sync <= 0; mode_ack <= 0;
            mode_overflow_meta <= 0; mode_overflow_sync <= 0; mode_local_history <= 0;
            local_select_done_o <= 0; local_select_done_id_o <= 0; local_select_result_o <= 0;
            audio_enable_meta <= 0; audio_enable_sync <= 0; audio_enable_previous <= 0;
        end else begin
            mode_req_meta <= mode_req; mode_req_sync <= mode_req_meta;
            mode_overflow_meta <= mode_overflow; mode_overflow_sync <= mode_overflow_meta;
            // Preserve local ownership for the 2FF request journey and its
            // destination execute edge, including capture on the last tail.
            mode_local_history <= {mode_local_history[1:0], local_fence};
            audio_enable_meta <= audio_enable_i && audio_rst_n_i;
            audio_enable_sync <= audio_enable_meta;
            audio_enable_previous <= audio_enable_sync;
            local_select_done_o <= 0;
            local_tail <= {local_tail[1:0], local_switch_busy_i || local_accept};
            if (local_accept) begin
                selected_o <= local_select_bind_i;
                local_select_done_o <= 1;
                local_select_done_id_o <= local_select_id_i;
                local_select_result_o <= 0;
            end
            if (mode_write) begin
                mode_ack <= mode_req_sync;
                if (mode_payload[31:1] == 0 || local_fence || (|mode_local_history) || cancel_i)
                    control_reject <= control_reject + 1'b1;
                else if (mode_payload[31:1] == last_mode[31:1]) begin
                    // A retry is idempotent. Conflicting payloads never execute.
                    if (mode_payload != last_mode) control_reject <= control_reject + 1'b1;
                end else begin
                    selected_o <= mode_payload[0];
                    last_mode <= mode_payload;
                end
            end
        end
    end

    wire cdc_rst_n = rst_n_i && audio_rst_n_i;
    wire cdc_enable, cdc_valid, cdc_ready, cdc_quiet, cdc_src_ready, cdc_control_ready;
    logic input_cancelled;
    // A fresh packet while the enable return is synchronizing must wait, not
    // look like a pre-existing disabled packet to midi_event_cdc. Continuous
    // VALID that intersects a real mute/cancel/flush must remain visible to
    // its blocked-input logic, including through the next enable warm-up.
    // Only the synchronized falling edge cancels a held packet. A static
    // !audio_enable_sync would also cancel a fresh packet at initial enable.
    // Raw audio_enable_i is consumed only by the first synchronizer stage
    // above and by native audio-domain gates below.
    wire input_cancel_fence = !enable_i || !selected_o || audio_enable_fell ||
        !audio_rst_n_i || cancel_i || flush_i[midi_event_i.source[1:0]];
    wire input_visible = cdc_control_ready || input_cancelled || input_cancel_fence;
    assign midi_ready_o = cdc_src_ready && input_visible;
    always_ff @(posedge control_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) input_cancelled <= 0;
        else if (!midi_valid_i || midi_ready_o) input_cancelled <= 0;
        else if (input_cancel_fence) input_cancelled <= 1;
    end
    wire [3:0] cdc_flush;
    midi_pkg::midi_event_t cdc_event;
    wire run_audio = audio_enable_i && cdc_enable;
    midi_event_cdc u_midi_cdc (
        .src_clk_i(control_clk_i), .dst_clk_i(audio_clk_i), .rst_n_i(cdc_rst_n),
        .src_enable_i(enable_i && selected_o && audio_enable_sync),
        .src_cancel_i(cancel_i), .src_flush_i(flush_i),
        .src_event_i(midi_event_i), .src_valid_i(midi_valid_i && input_visible), .src_ready_o(cdc_src_ready),
        .src_delivered_o(midi_delivered_o), .src_drained_o(midi_drained_o),
        .src_return_event_o(), .src_quiesced_o(cdc_quiet), .src_control_ready_o(cdc_control_ready),
        .dst_enable_o(cdc_enable), .dst_event_o(cdc_event), .dst_valid_o(cdc_valid),
        .dst_ready_i(cdc_ready), .dst_drop_i(!audio_enable_i), .dst_flush_o(cdc_flush),
        .dst_pending_o(), .dst_capture_pending_o()
    );
    wire legal_event = cdc_event.source < 4 && cdc_event.nbytes == 3 &&
        cdc_event.data1 < 128 && cdc_event.data2 < 128;
    wire note_event = cdc_event.status[7:4] == 8 || cdc_event.status[7:4] == 9;
    wire legal_note = !note_event || (cdc_event.data1 >= 60 && cdc_event.data1 <= 84);
    wire owner_event_ready;
    wire owner_event_valid = cdc_valid && legal_event && legal_note;
    assign cdc_ready = (!legal_event || !legal_note) ? 1'b1 : owner_event_ready;
    wire command_valid, command_ready;
    wire [2:0] command_kind;
    wire [6:0] command_note;
    wire core_ready, core_initializing, core_active, core_reject, core_pcm_valid;
    wire signed [23:0] core_pcm;
    wire [31:0] core_deadline;
    logic [1:0] playing_source;
    logic playing_valid;
    wire flush_playing = playing_valid && cdc_flush[playing_source];
    wire core_rst_n = cdc_rst_n && run_audio && !flush_playing;
    // The owner retains release ownership (the core deliberately lets its tail
    // decay). No bend/gain/pan modulation is implemented by this first wrapper.
    midi_voice_owner #(.VOICES(1), .AW(1), .HAS_BEND(0)) u_owner (
        .clk(audio_clk_i), .rst_n(cdc_rst_n), .enable(run_audio),
        .clear(1'b0), .refresh(1'b0), .release_all(1'b0),
        .event_i(cdc_event), .event_valid(owner_event_valid), .event_ready(owner_event_ready),
        .flush(cdc_flush), .voice_idle(!core_active),
        .command_valid(command_valid), .command_ready(command_ready),
        .command_kind(command_kind), .command_voice(), .command_note(command_note),
        .command_velocity(), .command_volume(), .command_pan(), .command_pitch(),
        .command_stamp(), .cancel_on(), .voice_stamps(), .hard_mute()
    );
    wire core_operation = command_kind == 0 || command_kind == 1 || command_kind == 4;
    assign command_ready = core_operation ? core_ready : 1'b1;
    flute_voice u_core (
        .clk_i(audio_clk_i), .rst_n_i(core_rst_n), .sample_tick_i(sample_tick48_i),
        .command_valid_i(command_valid && core_operation && !flush_playing),
        .command_ready_o(core_ready),
        .command_op_i(command_kind == 0 ? 2'd1 : command_kind == 1 ? 2'd2 : 2'd3),
        .note_i(command_note), .preset_i(2'd1), .seed_i(32'd24680),
        .pcm_o(core_pcm), .pcm_valid_o(core_pcm_valid), .active_o(core_active),
        .initializing_o(core_initializing), .reject_o(core_reject), .deadline_count_o(core_deadline)
    );
    logic [31:0] event_reject, frame_count;
    always_ff @(posedge audio_clk_i or negedge cdc_rst_n) begin
        if (!cdc_rst_n) begin
            playing_source <= 0; playing_valid <= 0;
            pcm_o <= 0; pcm_valid_o <= 0; event_reject <= 0; frame_count <= 0;
        end else begin
            pcm_valid_o <= 0;
            if (cdc_valid && cdc_ready && (!legal_event || !legal_note))
                event_reject <= event_reject + 1'b1;
            else if (core_reject) event_reject <= event_reject + 1'b1;
            if (!run_audio || flush_playing) begin
                playing_valid <= 0; pcm_o <= 0;
            end else begin
                if (owner_event_valid && owner_event_ready &&
                    cdc_event.status[7:4] == 9 && cdc_event.data2 != 0) begin
                    playing_source <= cdc_event.source[1:0]; playing_valid <= 1;
                end
                if (core_pcm_valid) begin
                    pcm_o <= core_pcm; pcm_valid_o <= 1; frame_count <= frame_count + 1'b1;
                end
            end
        end
    end

    // Periodic bundled-data readback. The audio snapshot stays immutable until
    // its acknowledgement has returned; only req/ack traverse synchronizers.
    logic snapshot_req, snapshot_ack;
    (* ASYNC_REG="TRUE" *) logic snap_req_meta, snap_req_sync, snap_ack_meta, snap_ack_sync;
    logic [98:0] audio_snapshot;
    logic [31:0] status_reject, status_deadline, status_frames;
    logic status_ready, status_initializing, status_active;
    always_ff @(posedge audio_clk_i or negedge cdc_rst_n) begin
        if (!cdc_rst_n) begin
            snap_req_meta <= 0; snap_req_sync <= 0; snapshot_ack <= 0; audio_snapshot <= 0;
        end else begin
            snap_req_meta <= snapshot_req; snap_req_sync <= snap_req_meta;
            if (snap_req_sync != snapshot_ack) begin
                audio_snapshot <= {core_ready, core_initializing, core_active,
                    event_reject, core_deadline, frame_count};
                snapshot_ack <= snap_req_sync;
            end
        end
    end
    always_ff @(posedge control_clk_i or negedge cdc_rst_n) begin
        if (!cdc_rst_n) begin
            snapshot_req <= 0; snap_ack_meta <= 0; snap_ack_sync <= 0;
            status_reject <= 0; status_deadline <= 0; status_frames <= 0;
            status_ready <= 0; status_initializing <= 1; status_active <= 0;
        end else begin
            snap_ack_meta <= snapshot_ack; snap_ack_sync <= snap_ack_meta;
            if (snapshot_req == snap_ack_sync) begin
                {status_ready, status_initializing, status_active,
                    status_reject, status_deadline, status_frames} <= audio_snapshot;
                snapshot_req <= ~snapshot_req;
            end
        end
    end
    assign quiesced_o = cdc_quiet;
    always_comb begin
        reg_read_data_o = 0;
        case (reg_read_addr_i)
            8'hb0: reg_read_data_o = 32'h464c5531;
            8'hb1: reg_read_data_o = 32'h00010000;
            8'hb3: reg_read_data_o = last_mode;
            8'hb4: reg_read_data_o = {27'd0, (status_deadline != 0),
                status_active, (selected_o && audio_enable_sync && status_initializing),
                selected_o, (rst_n_i && audio_rst_n_i && !cancel_i)};
            8'hb5: reg_read_data_o = 1;
            8'hb6: reg_read_data_o = control_reject + status_reject;
            8'hb7: reg_read_data_o = status_deadline;
            8'hb8: reg_read_data_o = status_frames;
            8'hb9: reg_read_data_o = {31'd0, mode_overflow_sync};
            default: ;
        endcase
    end
endmodule
`default_nettype wire
