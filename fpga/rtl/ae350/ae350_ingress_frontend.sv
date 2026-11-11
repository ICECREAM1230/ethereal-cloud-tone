`default_nettype none
// USB packet validation/serialization and RAW byte arbitration stay at 48 MHz.
// All output tuples are registered and stable under backpressure. Source 3 is
// native sensors; no DIN/raw stream is assigned to it in this adapter.
module ae350_ingress_frontend (
    input wire clk_i,rst_ni,
    input wire [31:0] usb_packet_data_i,
    input wire usb_packet_valid_i,
    output wire usb_packet_ready_o,
    input wire usb_session_flush_i,usb_flush_busy_i,
    output logic usb_local_flush_o,
    input wire [1:0] raw_source_i,
    input wire [7:0] raw_data_i,
    input wire raw_valid_i,
    output wire raw_ready_o,
    input wire raw_enabled_i,
    input wire [3:0] mailbox_flush_i,
    input wire flush_done_valid_i,
    input wire [3:0] flush_done_mask_i,
    input wire [31:0] flush_done_sequence_i,
    output wire byte_valid_o,
    input wire byte_ready_i,
    output logic [1:0] byte_source_o,
    output logic [7:0] byte_data_o,
    output logic [63:0] byte_tick_o,
    output logic flush_valid_o,
    input wire flush_ready_i,
    output logic [3:0] flush_mask_o,
    output logic [63:0] flush_tick_o,
    output logic [3:0] blocked_sources_o,
    output logic [31:0] accepted_count_o,packet_error_count_o,padding_count_o
);
    logic [63:0] tick;
    logic slot_valid,last_was_raw,packet_classified,packet_invalid_q,startup_pending;
    logic [3:0] pending_mask;
    logic [63:0] pending_tick;
    wire [0:0][7:0] usb_byte;
    wire [0:0] usb_valid,usb_ready,usb_reset;
    wire ingress_ready,packet_error,known_invalid;
    wire [31:0] unused_compat;
    wire padding=usb_packet_data_i==0;
    wire invalid_boundary=usb_packet_valid_i && packet_classified && packet_invalid_q &&
                         !blocked_sources_o[0] && !usb_session_flush_i;
    wire [3:0] requested_mask=mailbox_flush_i | {3'd0,(usb_session_flush_i || invalid_boundary)} |
        (startup_pending ? 4'b0111 : 4'd0);
    // No bytes from a blocked source enter either slot. Repeated requests for
    // an already blocked source are covered by the existing boundary.
    wire [3:0] fresh_mask=requested_mask & ~blocked_sources_o;
    wire [3:0] completion_mask=flush_done_valid_i ? flush_done_mask_i : 4'd0;
    wire [3:0] cancel_mask=requested_mask | completion_mask;
    wire usb_clear=blocked_sources_o[0] || cancel_mask[0];
    wire packet_admit=packet_classified && !padding && !usb_clear;
    wire usb_available=usb_valid[0] && !usb_clear;
    wire raw_legal=raw_source_i==1 || raw_source_i==2;
    wire raw_available=raw_valid_i && raw_enabled_i && raw_legal &&
                       !blocked_sources_o[raw_source_i] && !cancel_mask[raw_source_i];
    wire choose_raw=raw_available && (!usb_available || !last_was_raw);
    wire choose_usb=usb_available && !choose_raw;
    // Conservative single output slot. The low-valid bubble after cancellation
    // also satisfies control_link_apb's cancelled-record replay fence.
    assign byte_valid_o=rst_ni && slot_valid && !cancel_mask[byte_source_o];
    assign raw_ready_o=rst_ni && !slot_valid && choose_raw;
    assign usb_ready[0]=rst_ni && !slot_valid && choose_usb;
    assign usb_packet_ready_o=rst_ni && !usb_clear &&
                              (padding || (packet_classified && ingress_ready));
    wire local_flush_request=mailbox_flush_i[0] ||
        (fresh_mask[0] && !usb_session_flush_i) ||
        (completion_mask[0] && !blocked_sources_o[0]);
    usb_midi_ingress #(.CABLES(1),.PACKET_DEPTH(8)) u_usb_ingress (
        .clk(clk_i),.rst(!rst_ni),.packet_data(usb_packet_data_i),
        .packet_valid(usb_packet_valid_i && packet_admit),.packet_ready(ingress_ready),
        .flush(usb_clear),.byte_data(usb_byte),.byte_valid(usb_valid),.byte_ready(usb_ready),
        .source_reset(usb_reset),.packet_error,.compat_count(unused_compat),.packet_known_invalid(known_invalid));
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if(!rst_ni) begin
            usb_local_flush_o<=0;startup_pending<=1;tick<=0;slot_valid<=0;last_was_raw<=0;byte_source_o<=0;byte_data_o<=0;byte_tick_o<=0;
            packet_classified<=0;packet_invalid_q<=0;pending_mask<=0;pending_tick<=0;
            flush_valid_o<=0;flush_mask_o<=0;flush_tick_o<=0;blocked_sources_o<=0;
            accepted_count_o<=0;packet_error_count_o<=0;padding_count_o<=0;
        end else begin
            startup_pending<=0;tick<=tick+64'd1;
            // The real Host gates packet_valid with !local_flush. This must
            // be a FF boundary, otherwise invalid_boundary feeds itself.
            // Hold the request until the Host observes it and raises busy.
            if(usb_local_flush_o && usb_flush_busy_i)usb_local_flush_o<=0;
            else if(!usb_local_flush_o && local_flush_request && !usb_flush_busy_i)usb_local_flush_o<=1;
            if(!usb_packet_valid_i || usb_clear || padding || usb_packet_ready_o) begin
                packet_classified<=0;packet_invalid_q<=0;
            end else if(!packet_classified) begin
                packet_classified<=1;packet_invalid_q<=known_invalid;
            end
            if(usb_packet_valid_i && usb_packet_ready_o && padding)padding_count_o<=padding_count_o+32'd1;
            if(packet_error || invalid_boundary)packet_error_count_o<=packet_error_count_o+32'd1;
            if(slot_valid && (cancel_mask[byte_source_o] || byte_ready_i))slot_valid<=0;
            if(!slot_valid && (choose_raw || choose_usb)) begin
                slot_valid<=1;byte_source_o<=choose_raw ? raw_source_i : 2'd0;
                byte_data_o<=choose_raw ? raw_data_i : usb_byte[0];byte_tick_o<=tick;
                last_was_raw<=choose_raw;
            end
            if(byte_valid_o && byte_ready_i)accepted_count_o<=accepted_count_o+32'd1;
            pending_mask<=pending_mask | fresh_mask;
            if(|fresh_mask)pending_tick<=tick;
            if(flush_valid_o && flush_ready_i)flush_valid_o<=0;
            if(!flush_valid_o && (|(pending_mask | fresh_mask))) begin
                flush_valid_o<=1;flush_mask_o<=pending_mask | fresh_mask;
                flush_tick_o<=|fresh_mask ? tick : pending_tick;pending_mask<=0;
            end
            blocked_sources_o<=(blocked_sources_o & ~completion_mask) | fresh_mask | pending_mask |
                (flush_valid_o ? flush_mask_o : 4'd0);
        end
    end
    // USB busy is required at the MID1 completion barrier in the transport;
    // completion sequence is diagnostic, never a new source transaction token.
    wire unused_inputs=^{flush_done_sequence_i,usb_reset,unused_compat};
endmodule
`default_nettype wire
