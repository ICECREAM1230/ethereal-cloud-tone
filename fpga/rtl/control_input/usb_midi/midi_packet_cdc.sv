// SPDX-License-Identifier: MIT
// Single packet slot plus an independent, higher-priority session-flush slot.
// Both held payloads are bundled data protected by request/acknowledge toggles.
// Source flush edges coalesce while the flush slot is busy; no packet can enter
// during that interval. Epoch reuse across 2^32 flushes is not guaranteed safe.
// Constrain each held payload path relative to its synchronized request; do not
// treat the 64-bit packet or 32-bit flush epoch as separately synchronized bits.
module midi_packet_cdc (
    input  logic        src_clk,
    input  logic        dst_clk,
    input  logic        rst_n,
    input  logic [31:0] src_data,
    input  logic        src_valid,
    output logic        src_ready,
    input  logic        src_flush,
    output logic [31:0] dst_data,
    output logic        dst_valid,
    input  logic        dst_ready,
    output logic        dst_flush
);
    logic [63:0] held_packet;
    logic [31:0] source_epoch, held_flush_epoch, destination_epoch;
    logic packet_req, packet_ack, flush_req, flush_ack;
    logic src_flush_seen, flush_pulse;
    logic packet_pending, flush_pending, dst_present;
    (* ASYNC_REG = "TRUE" *) logic packet_ack_meta, packet_ack_sync;
    (* ASYNC_REG = "TRUE" *) logic flush_ack_meta, flush_ack_sync;
    (* ASYNC_REG = "TRUE" *) logic packet_req_meta, packet_req_sync;
    (* ASYNC_REG = "TRUE" *) logic flush_req_meta, flush_req_sync;

    // Assert the source readiness mask asynchronously, but remove it only
    // through source-clocked registers. Raw PLL/reset release must not feed
    // the Host packet-bank clock-enable combinationally across clock domains.
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_meta;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_sync;
    always_ff @(posedge src_clk or negedge rst_n) begin
        if (!rst_n) begin
            src_reset_meta <= 0;
            src_reset_sync <= 0;
        end else begin
            src_reset_meta <= 1;
            src_reset_sync <= src_reset_meta;
        end
    end

    assign src_ready = src_reset_sync && !src_flush && packet_req == packet_ack_sync
                     && flush_req == flush_ack_sync;
    assign packet_pending = packet_req_sync != packet_ack;
    assign flush_pending = flush_req_sync != flush_ack;
    assign dst_data = held_packet[31:0];
    // Qualify the held epoch locally before publishing. Keep the wide epoch
    // comparison out of the downstream valid/ready/flush control network.
    assign dst_valid = rst_n && dst_present && packet_pending && !flush_pending && !flush_pulse;
    // Reset is a sustained USB-source clear even when either clock has stopped.
    assign dst_flush = !rst_n || flush_pulse;

    always_ff @(posedge src_clk or negedge src_reset_sync) begin
        if (!src_reset_sync) begin
            held_packet <= 0;
            source_epoch <= 0;
            held_flush_epoch <= 0;
            packet_req <= 0;
            flush_req <= 0;
            src_flush_seen <= 0;
            packet_ack_meta <= 0;
            packet_ack_sync <= 0;
            flush_ack_meta <= 0;
            flush_ack_sync <= 0;
        end else begin
            packet_ack_meta <= packet_ack;
            packet_ack_sync <= packet_ack_meta;
            flush_ack_meta <= flush_ack;
            flush_ack_sync <= flush_ack_meta;
            src_flush_seen <= src_flush;
            if (src_flush && !src_flush_seen && flush_req == flush_ack_sync) begin
                source_epoch <= source_epoch + 32'd1;
                held_flush_epoch <= source_epoch + 32'd1;
                flush_req <= ~flush_req;
            end
            // A flush edge while busy is already covered by the in-flight
            // flush, because readiness excludes all packets until both acks.
            if (src_valid && src_ready) begin
                held_packet <= {source_epoch, src_data};
                packet_req <= ~packet_req;
            end
        end
    end

    always_ff @(posedge dst_clk or negedge rst_n) begin
        if (!rst_n) begin
            packet_req_meta <= 0;
            packet_req_sync <= 0;
            flush_req_meta <= 0;
            flush_req_sync <= 0;
            packet_ack <= 0;
            flush_ack <= 0;
            destination_epoch <= 0;
            flush_pulse <= 0;
            dst_present <= 0;
        end else begin
            packet_req_meta <= packet_req;
            packet_req_sync <= packet_req_meta;
            flush_req_meta <= flush_req;
            flush_req_sync <= flush_req_meta;
            flush_pulse <= 0;
            if (flush_pending) begin
                destination_epoch <= held_flush_epoch;
                flush_pulse <= 1;
                flush_ack <= flush_req_sync;
            end
            // Every ACK must revoke presentation before the next request can
            // arrive. The exclusive chain prevents the still-pending old
            // request from requalifying on the same edge as its ACK.
            if (flush_pending || flush_pulse)
                dst_present <= 0;
            else if (dst_valid && dst_ready)
                dst_present <= 0;
            else if (packet_pending && held_packet[63:32] != destination_epoch)
                dst_present <= 0;
            else if (!dst_present && packet_pending
                     && held_packet[63:32] == destination_epoch)
                dst_present <= 1;
            if (packet_pending && !flush_pending) begin
                // Stale requests can arrive after the flush. Discard and ack
                // without needing dst_ready, including during the flush pulse.
                if (held_packet[63:32] != destination_epoch
                    || (dst_valid && dst_ready))
                    packet_ack <= packet_req_sync;
            end
        end
    end
endmodule
