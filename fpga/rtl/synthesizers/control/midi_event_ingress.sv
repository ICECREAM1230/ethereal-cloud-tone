// SPDX-License-Identifier: MIT
`default_nettype none
// Receiver-owned, one-event elastic boundary. Decoder ready is a local FF;
// engine readiness ends at this boundary instead of returning to the decoder.
// Cancel/block may discard events: the consumer must apply the same cancel
// fence on its capture edge. No downstream acceptance is counted here.
module midi_event_ingress #(
    parameter integer ROUTE_WIDTH=2
) (
    input wire clk_i, rst_n_i,
    input wire cancel_all_i, block_i,
    input wire [3:0] source_flush_i,
    input wire [ROUTE_WIDTH-1:0] route_i,
    input midi_pkg::midi_event_t in_event_i,
    input wire in_valid_i,
    output wire in_ready_o,
    output midi_pkg::midi_event_t out_event_o,
    output wire [ROUTE_WIDTH-1:0] out_route_o,
    output wire out_valid_o, pending_o,
    input wire out_ready_i,
    output logic drained_o
);
    midi_pkg::midi_event_t event_q;
    logic [ROUTE_WIDTH-1:0] route_q;
    logic full_q, ready_q, blocked_input_q, full_next;
    wire input_fire = in_valid_i && ready_q;
    wire input_flush = source_flush_i[in_event_i.source[1:0]];
    wire held_flush = source_flush_i[event_q.source[1:0]];
    wire cancel_input = cancel_all_i || block_i || input_flush ||
                        blocked_input_q || in_event_i.source >= 8'd4;

    assign in_ready_o = ready_q;
    assign out_event_o = event_q;
    assign out_route_o = route_q;
    assign out_valid_o = full_q;
    assign pending_o = full_q;

    always_comb begin
        full_next = full_q;
        if (full_q) begin
            if (cancel_all_i || block_i || held_flush || out_ready_i)
                full_next = 1'b0;
        end else if (input_fire) begin
            full_next = !cancel_input;
        end
    end

    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            event_q <= '0;
            route_q <= '0;
            full_q <= 1'b0;
            ready_q <= 1'b0;
            blocked_input_q <= 1'b0;
            drained_o <= 1'b0;
        end else begin
            full_q <= full_next;
            ready_q <= !full_next;
            drained_o <= input_fire && cancel_input;
            if (input_fire) begin
                event_q <= in_event_i;
                route_q <= route_i;
            end
            // A stalled input that straddles cancellation remains marked
            // until withdrawal/handshake. It cannot reappear as a fresh On.
            if (!in_valid_i || input_fire)
                blocked_input_q <= 1'b0;
            else if (cancel_all_i || block_i || input_flush)
                blocked_input_q <= 1'b1;
        end
    end
endmodule
`default_nettype wire
