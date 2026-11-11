// SPDX-License-Identifier: MIT
// Complete MIDI-event slot plus an independent, higher-priority control slot.
// Like midi_packet_cdc, only req/ack cross through 2FF synchronizers. Payloads
// remain held until acknowledgement; constrain their bundled-data paths to the
// synchronized requests. Per-source epochs keep delayed events behind flushes.
// Epoch reuse after 2^32 changes for one source is outside this slot's contract.
module midi_event_cdc (
    input  logic                       src_clk_i,
    input  logic                       dst_clk_i,
    input  logic                       rst_n_i,
    input  logic                       src_enable_i,
    input  logic                       src_cancel_i,
    input  logic [3:0]                 src_flush_i,
    input  midi_pkg::midi_event_t       src_event_i,
    input  logic                       src_valid_i,
    output logic                       src_ready_o,
    output logic                       src_delivered_o,
    output logic                       src_drained_o,
    output midi_pkg::midi_event_t       src_return_event_o,
    output logic                       src_quiesced_o,
    output logic                       src_control_ready_o,
    output logic                       dst_enable_o,
    output midi_pkg::midi_event_t       dst_event_o,
    output logic                       dst_valid_o,
    input  logic                       dst_ready_i,
    input  logic                       dst_drop_i,
    output logic [3:0]                 dst_flush_o,
    output logic                       dst_pending_o,
    output logic                       dst_capture_pending_o
);
    midi_pkg::midi_event_t held_event;
    logic [31:0] held_event_epoch;
    logic held_event_drop;
    logic [31:0] source_epoch [0:3];
    logic [31:0] held_control_epoch [0:3];
    logic [31:0] destination_epoch [0:3];
    logic source_enable, held_control_enable;
    logic [3:0] flush_seen, flush_edges, changed_sources;
    logic cancel_seen, enable_changed, control_changed;
    logic event_req, event_ack, control_req, control_ack;
    logic event_busy, control_busy, control_dirty, blocked_input;
    logic event_pending, control_pending, dst_present;
    logic ack_delivered, source_pending;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_meta, src_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic dst_reset_meta, dst_reset_sync;
    (* ASYNC_REG = "TRUE" *) logic event_ack_meta, event_ack_sync;
    (* ASYNC_REG = "TRUE" *) logic control_ack_meta, control_ack_sync;
    (* ASYNC_REG = "TRUE" *) logic event_req_meta, event_req_sync;
    (* ASYNC_REG = "TRUE" *) logic control_req_meta, control_req_sync;
    (* ASYNC_REG = "TRUE" *) logic source_pending_meta, source_pending_sync;
    (* ASYNC_REG = "TRUE" *) logic dst_pending_meta, dst_pending_sync;

    always_ff @(posedge src_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            src_reset_meta <= 1'b0;
            src_reset_sync <= 1'b0;
        end else begin
            src_reset_meta <= 1'b1;
            src_reset_sync <= src_reset_meta;
        end
    end

    always_ff @(posedge dst_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            dst_reset_meta <= 1'b0;
            dst_reset_sync <= 1'b0;
        end else begin
            dst_reset_meta <= 1'b1;
            dst_reset_sync <= dst_reset_meta;
        end
    end

    assign flush_edges = src_flush_i & ~flush_seen;
    assign enable_changed = src_enable_i != source_enable;
    assign changed_sources = flush_edges |
        {4{enable_changed || (src_cancel_i && !cancel_seen)}};
    assign control_changed = |changed_sources;
    assign src_control_ready_o = src_reset_sync && src_enable_i && !src_cancel_i &&
        !(|src_flush_i) && !control_busy && !control_dirty &&
        !control_changed;
    assign src_ready_o = src_control_ready_o && !event_busy;

    // The source-domain aggregate is synchronized as a scalar. Neither meta
    // stage nor an asynchronous source-domain request may drive a capture fence.
    assign source_pending = event_busy || control_busy || control_dirty ||
        control_changed || (src_valid_i && src_enable_i && !src_cancel_i);
    assign event_pending = event_req_sync != event_ack;
    assign control_pending = control_req_sync != control_ack;
    assign dst_pending_o = dst_reset_sync && (source_pending_sync ||
        event_pending || control_pending || dst_present || (|dst_flush_o));
    // Only the local payload-capture phase fences a native runtime copy.
    // A presented packet may wait for owner readiness without holding this.
    assign dst_capture_pending_o = dst_reset_sync && event_pending && !dst_present;
    // Source work blocks immediately, including a new cancel/flush edge before
    // it becomes busy. The returning native pending level also covers flush
    // and the tail of source_pending_sync after the last mailbox ACK.
    assign src_quiesced_o = src_reset_sync && !source_pending && !dst_pending_sync;
    assign dst_valid_o = dst_present && event_pending && dst_enable_o &&
        !control_pending && !(|dst_flush_o) && !dst_drop_i;

    always_ff @(posedge src_clk_i or negedge src_reset_sync) begin
        if (!src_reset_sync) begin
            held_event <= '0;
            held_event_epoch <= '0;
            held_event_drop <= 1'b0;
            source_enable <= 1'b0;
            held_control_enable <= 1'b0;
            flush_seen <= '0;
            cancel_seen <= 1'b0;
            event_req <= 1'b0;
            control_req <= 1'b0;
            event_busy <= 1'b0;
            control_busy <= 1'b0;
            control_dirty <= 1'b1;
            blocked_input <= 1'b0;
            event_ack_meta <= 1'b0;
            event_ack_sync <= 1'b0;
            control_ack_meta <= 1'b0;
            control_ack_sync <= 1'b0;
            dst_pending_meta <= 1'b1;
            dst_pending_sync <= 1'b1;
            src_delivered_o <= 1'b0;
            src_drained_o <= 1'b0;
            src_return_event_o <= '0;
            for (integer s = 0; s < 4; s = s + 1) begin
                source_epoch[s] <= '0;
                held_control_epoch[s] <= '0;
            end
        end else begin
            event_ack_meta <= event_ack;
            event_ack_sync <= event_ack_meta;
            control_ack_meta <= control_ack;
            control_ack_sync <= control_ack_meta;
            dst_pending_meta <= dst_pending_o;
            dst_pending_sync <= dst_pending_meta;
            src_delivered_o <= 1'b0;
            src_drained_o <= 1'b0;
            flush_seen <= src_flush_i;
            cancel_seen <= src_cancel_i;
            source_enable <= src_enable_i;

            // A waiting upstream record must not become a fresh Note On after
            // its source was cancelled. Preserve the mark until withdrawal or
            // handshake, then send that complete record only for draining.
            if (!src_valid_i || (src_valid_i && src_ready_o))
                blocked_input <= 1'b0;
            else if (!src_enable_i || src_cancel_i ||
                     src_flush_i[src_event_i.source[1:0]] ||
                     changed_sources[src_event_i.source[1:0]])
                blocked_input <= 1'b1;

            if (control_changed) begin
                control_dirty <= 1'b1;
                for (integer s = 0; s < 4; s = s + 1)
                    if (changed_sources[s])
                        source_epoch[s] <= source_epoch[s] + 32'd1;
            end
            if (control_busy && (control_ack_sync == control_req))
                control_busy <= 1'b0;
            // Do not overwrite a busy control payload. New edges update the
            // desired epochs above and are sent after the current ACK, even
            // when the pulse was only one source-clock long.
            if (!control_busy && control_dirty && !control_changed) begin
                for (integer s = 0; s < 4; s = s + 1)
                    held_control_epoch[s] <= source_epoch[s];
                held_control_enable <= source_enable;
                control_req <= ~control_req;
                control_busy <= 1'b1;
                control_dirty <= 1'b0;
            end

            if (event_busy && (event_ack_sync == event_req)) begin
                event_busy <= 1'b0;
                // ack_delivered is held in the destination from before the
                // ACK starts its 2FF return journey until the next completion.
                src_delivered_o <= ack_delivered;
                src_drained_o <= !ack_delivered;
                src_return_event_o <= held_event;
            end
            if (src_valid_i && src_ready_o) begin
                held_event <= src_event_i;
                held_event_epoch <= source_epoch[src_event_i.source[1:0]];
                held_event_drop <= blocked_input;
                event_req <= ~event_req;
                event_busy <= 1'b1;
            end
        end
    end

    always_ff @(posedge dst_clk_i or negedge dst_reset_sync) begin
        if (!dst_reset_sync) begin
            event_req_meta <= 1'b0;
            event_req_sync <= 1'b0;
            control_req_meta <= 1'b0;
            control_req_sync <= 1'b0;
            source_pending_meta <= 1'b0;
            source_pending_sync <= 1'b0;
            event_ack <= 1'b0;
            control_ack <= 1'b0;
            ack_delivered <= 1'b0;
            dst_enable_o <= 1'b0;
            dst_event_o <= '0;
            dst_present <= 1'b0;
            dst_flush_o <= '0;
            for (integer s = 0; s < 4; s = s + 1)
                destination_epoch[s] <= '0;
        end else begin
            event_req_meta <= event_req;
            event_req_sync <= event_req_meta;
            control_req_meta <= control_req;
            control_req_sync <= control_req_meta;
            source_pending_meta <= source_pending;
            source_pending_sync <= source_pending_meta;
            dst_flush_o <= '0;

            if (control_pending) begin
                for (integer s = 0; s < 4; s = s + 1) begin
                    dst_flush_o[s] <= held_control_epoch[s] != destination_epoch[s];
                    destination_epoch[s] <= held_control_epoch[s];
                end
                dst_enable_o <= held_control_enable;
                control_ack <= control_req_sync;
                dst_present <= 1'b0;
            end else if (event_pending) begin
                // A stale, disabled or explicitly dropped event is completed
                // independently of owner readiness. This prevents a full owner
                // or stopped consumer from blocking a flush indefinitely.
                if (!dst_enable_o || held_event_drop || dst_drop_i ||
                    (held_event_epoch != destination_epoch[held_event.source[1:0]])) begin
                    event_ack <= event_req_sync;
                    ack_delivered <= 1'b0;
                    dst_present <= 1'b0;
                end else if (dst_valid_o && dst_ready_i) begin
                    event_ack <= event_req_sync;
                    ack_delivered <= 1'b1;
                    dst_present <= 1'b0;
                end else if (!dst_present && !(|dst_flush_o)) begin
                    dst_event_o <= held_event;
                    dst_present <= 1'b1;
                end
            end else begin
                dst_present <= 1'b0;
            end
        end
    end
endmodule
