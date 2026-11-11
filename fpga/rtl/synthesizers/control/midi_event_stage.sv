// SPDX-License-Identifier: MIT
// One complete event at the selected engine's input boundary. Ordinary
// readiness never looks ahead to out_ready; cancellation may retract a slot.
module midi_event_stage #(
    parameter bit REGISTERED_CONTROLS = 1'b0
) (
    input  wire clk, rst_n,
    input  wire enable, cancel_all, drop_incoming,
    input  wire [3:0] source_flush,
    input  midi_pkg::midi_event_t in_event,
    input  wire in_valid,
    output wire in_ready,
    output wire slot_empty,
    output midi_pkg::midi_event_t out_event,
    output wire out_valid,
    input  wire out_ready,
    output wire delivered, drained
);
    midi_pkg::midi_event_t held_event;
    logic full;
    logic blocked_drop;
    // Registered-control integration resets full asynchronously and qualifies
    // upstream admission separately. Do not route raw reset through out_valid
    // into thousands of owner D/CE pins as another combinational condition.
    wire active = (REGISTERED_CONTROLS || rst_n) && enable && !cancel_all;
    wire input_flushed = source_flush[in_event.source[1:0]];
    wire held_flushed = full && source_flush[held_event.source[1:0]];

    // The registered-control boundary accepts input only when empty. A clear
    // that cancels a full slot remembers the waiting input until its later
    // handshake, so a one-cycle clear cannot replay that input as a fresh On.
    // Legacy users retain the immediate full-slot clear acknowledgement.
    wire discard_input = drop_incoming || (REGISTERED_CONTROLS && blocked_drop);
    assign in_ready = active && !input_flushed &&
                      (!full || (!REGISTERED_CONTROLS && drop_incoming));
    assign slot_empty = !full;
    assign out_event = held_event;
    assign out_valid = active && full && !drop_incoming && !held_flushed;
    assign delivered = out_valid && out_ready;
    assign drained = discard_input && in_valid && in_ready;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            blocked_drop <= 1'b0;
        end else if (!REGISTERED_CONTROLS || !enable || cancel_all ||
                     input_flushed || !in_valid || (in_valid && in_ready)) begin
            blocked_drop <= 1'b0;
        end else if (active && full && drop_incoming && in_valid &&
                     !input_flushed) begin
            blocked_drop <= 1'b1;
        end
    end

    generate if (REGISTERED_CONTROLS) begin : g_idle_payload
        // Data tracks the complete upstream record whenever the slot is empty.
        // Only full/valid uses admission qualification; a full slot never
        // changes its payload, including consume and cancellation edges.
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) held_event <= '0;
            else if (!full) held_event <= in_event;
        end
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) full <= 1'b0;
            else if (!enable || cancel_all || drop_incoming || held_flushed) full <= 1'b0;
            else case (full)
                // Complete both reachable state branches. Admission controls
                // belong only to the empty state's D value, not to a shared
                // clock-enable expression which also gates a full-slot hold.
                1'b0: if (in_valid && in_ready && !discard_input) full <= 1'b1;
                      else full <= 1'b0;
                1'b1: if (delivered) full <= 1'b0;
                      else full <= 1'b1;
                // Retain the legacy four-state hold for an unknown full bit.
                default: full <= full;
            endcase
        end
    end else begin : g_legacy_payload
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            full <= 1'b0;
            held_event <= '0;
        end else if (!enable || cancel_all || drop_incoming || held_flushed) begin
            full <= 1'b0;
        end else if (delivered) begin
            full <= 1'b0;
        end else if (in_valid && in_ready && !discard_input) begin
            held_event <= in_event;
            full <= 1'b1;
        end
    end
    end endgenerate
endmodule
