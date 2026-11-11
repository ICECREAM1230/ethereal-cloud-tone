`default_nettype none
// A coordinated reset is required for BOTH clocks. Payloads are bundled data,
// not bit synchronizers: constrain held_* paths against synchronized requests.
// One flush is in flight until CPU semantic completion; return completions may
// coalesce masks, retaining the most recent sequence for diagnostics only.
module ae350_ingress_cdc (
    input wire src_clk_i,dst_clk_i,link_rst_ni,
    input wire src_byte_valid_i,
    output wire src_byte_ready_o,
    input wire [1:0] src_byte_source_i,
    input wire [7:0] src_byte_data_i,
    input wire [63:0] src_byte_tick_i,
    input wire src_flush_valid_i,
    output wire src_flush_ready_o,
    input wire [3:0] src_flush_mask_i,
    input wire [63:0] src_flush_tick_i,
    output wire dst_byte_valid_o,
    input wire dst_byte_ready_i,
    output wire [1:0] dst_byte_source_o,
    output wire [7:0] dst_byte_data_o,
    output wire [63:0] dst_byte_tick_o,
    output wire dst_flush_valid_o,
    input wire dst_flush_ready_i,
    output wire [3:0] dst_flush_mask_o,
    output wire [63:0] dst_flush_tick_o,
    input wire dst_flush_done_valid_i,
    input wire [3:0] dst_flush_done_mask_i,
    input wire [31:0] dst_flush_done_sequence_i,
    output logic src_flush_done_valid_o,
    output logic [3:0] src_flush_done_mask_o,
    output logic [31:0] src_flush_done_sequence_o
);
    (* ASYNC_REG="TRUE" *) logic src_reset_meta,src_reset_sync,dst_reset_meta,dst_reset_sync;
    always_ff @(posedge src_clk_i or negedge link_rst_ni)begin
        if(!link_rst_ni)begin src_reset_meta<=0;src_reset_sync<=0;end
        else begin src_reset_meta<=1;src_reset_sync<=src_reset_meta;end
    end
    always_ff @(posedge dst_clk_i or negedge link_rst_ni)begin
        if(!link_rst_ni)begin dst_reset_meta<=0;dst_reset_sync<=0;end
        else begin dst_reset_meta<=1;dst_reset_sync<=dst_reset_meta;end
    end
    logic [3:0][31:0] source_epoch,received_epoch,recovery_epoch,source_recovery;
    logic [3:0][31:0] held_flush_epoch,held_done_recovery;
    logic [31:0] held_byte_epoch,held_byte_recovery;
    logic [1:0] held_byte_source;
    logic [7:0] held_byte_data;
    logic [63:0] held_byte_tick,held_flush_tick;
    logic [3:0] held_flush_mask,held_done_mask,done_pending_mask;
    logic [31:0] held_done_sequence,done_pending_sequence;
    logic byte_req,byte_ack,flush_req,flush_ack,done_req,done_ack;
    (* ASYNC_REG="TRUE" *) logic byte_ack_meta,byte_ack_sync,flush_ack_meta,flush_ack_sync;
    (* ASYNC_REG="TRUE" *) logic byte_req_meta,byte_req_sync,flush_req_meta,flush_req_sync;
    (* ASYNC_REG="TRUE" *) logic done_req_meta,done_req_sync,done_ack_meta,done_ack_sync;
    logic byte_present,flush_present,flush_accepted;
    // Capture bundled data in the existing request-priming cycle. All byte
    // consumers then see destination-clock registers, not a wide asynchronous
    // payload through APB protocol/fault decode. Valid latency is unchanged.
    logic [1:0] byte_source_local;
    logic [7:0] byte_data_local;
    logic [63:0] byte_tick_local;
    // Prime the mask alongside destination valid, breaking the source-mask
    // -> APB fault/capture -> byte CE round trip. Tick stays held until POP.
    logic [3:0] flush_mask_local;
    logic [3:0] source_active_mask;
    wire byte_pending=byte_req_sync!=byte_ack;
    wire flush_pending=flush_req_sync!=flush_ack;
    wire flush_fire=dst_flush_valid_o && dst_flush_ready_i;
    wire byte_stale=held_byte_epoch!=received_epoch[held_byte_source] ||
                    held_byte_recovery!=recovery_epoch[held_byte_source];
    wire cancel_byte=flush_fire && flush_mask_local[held_byte_source];
    wire done_cancel=dst_flush_done_valid_i && dst_flush_done_mask_i[held_byte_source];
    wire source_flush_busy=flush_req!=flush_ack_sync;
    // A source remains blocked until both the CPU completion and its transfer
    // acknowledgement have crossed back. Other source IDs can still progress.
    assign src_byte_ready_o=src_reset_sync && byte_req==byte_ack_sync &&
        !source_active_mask[src_byte_source_i] &&
        !(source_flush_busy && held_flush_mask[src_byte_source_i]) &&
        !(src_flush_valid_i && src_flush_mask_i[src_byte_source_i]);
    assign src_flush_ready_o=src_reset_sync && !source_flush_busy && source_active_mask==0 && (|src_flush_mask_i);
    assign dst_byte_valid_o=dst_reset_sync && byte_present && byte_pending && !cancel_byte;
    assign dst_byte_source_o=byte_source_local;
    assign dst_byte_data_o=byte_data_local;
    assign dst_byte_tick_o=byte_tick_local;
    assign dst_flush_valid_o=dst_reset_sync && flush_present && !flush_accepted;
    assign dst_flush_mask_o=flush_mask_local;
    assign dst_flush_tick_o=held_flush_tick;

    always_ff @(posedge src_clk_i or negedge src_reset_sync) begin
        if(!src_reset_sync) begin
            byte_req<=0;flush_req<=0;done_ack<=0;source_active_mask<=0;
            byte_ack_meta<=0;byte_ack_sync<=0;flush_ack_meta<=0;flush_ack_sync<=0;
            done_req_meta<=0;done_req_sync<=0;
            source_epoch<='0;source_recovery<='0;held_flush_epoch<='0;
            held_byte_epoch<=0;held_byte_recovery<=0;held_byte_source<=0;
            held_byte_data<=0;held_byte_tick<=0;held_flush_mask<=0;held_flush_tick<=0;
            src_flush_done_valid_o<=0;src_flush_done_mask_o<=0;src_flush_done_sequence_o<=0;
        end else begin
            byte_ack_meta<=byte_ack;byte_ack_sync<=byte_ack_meta;
            flush_ack_meta<=flush_ack;flush_ack_sync<=flush_ack_meta;
            done_req_meta<=done_req;done_req_sync<=done_req_meta;
            src_flush_done_valid_o<=0;
            if(done_req_sync!=done_ack) begin
                done_ack<=done_req_sync;
                source_active_mask<=source_active_mask & ~held_done_mask;
                src_flush_done_valid_o<=1;
                src_flush_done_mask_o<=held_done_mask;
                src_flush_done_sequence_o<=held_done_sequence;
                for(integer s=0;s<4;s=s+1)
                    if(held_done_mask[s])source_recovery[s]<=held_done_recovery[s];
            end
            if(src_flush_valid_i && src_flush_ready_o) begin
                source_active_mask<=src_flush_mask_i;
                held_flush_mask<=src_flush_mask_i;held_flush_tick<=src_flush_tick_i;
                flush_req<=~flush_req;
                for(integer s=0;s<4;s=s+1) begin
                    held_flush_epoch[s]<=source_epoch[s]+(src_flush_mask_i[s] ? 32'd1 : 32'd0);
                    if(src_flush_mask_i[s])source_epoch[s]<=source_epoch[s]+32'd1;
                end
            end
            if(src_byte_valid_i && src_byte_ready_o) begin
                held_byte_source<=src_byte_source_i;held_byte_data<=src_byte_data_i;held_byte_tick<=src_byte_tick_i;
                held_byte_epoch<=source_epoch[src_byte_source_i];
                held_byte_recovery<=source_recovery[src_byte_source_i];
                byte_req<=~byte_req;
            end
        end
    end

    always_ff @(posedge dst_clk_i or negedge dst_reset_sync) begin
        if(!dst_reset_sync) begin
            byte_ack<=0;flush_ack<=0;done_req<=0;
            byte_req_meta<=0;byte_req_sync<=0;flush_req_meta<=0;flush_req_sync<=0;
            done_ack_meta<=0;done_ack_sync<=0;
            received_epoch<='0;recovery_epoch<='0;held_done_recovery<='0;
            held_done_mask<=0;held_done_sequence<=0;done_pending_mask<=0;done_pending_sequence<=0;
            byte_present<=0;flush_present<=0;flush_accepted<=0;
            byte_source_local<=0;byte_data_local<=0;byte_tick_local<=0;
            flush_mask_local<=0;
        end else begin
            byte_req_meta<=byte_req;byte_req_sync<=byte_req_meta;
            flush_req_meta<=flush_req;flush_req_sync<=flush_req_meta;
            done_ack_meta<=done_ack;done_ack_sync<=done_ack_meta;
            // Capture in the existing priming cycle independently of ready.
            // Cancellation/stale checks below still suppress public valid;
            // a discarded private capture must not add APB ready to this CE.
            if(!byte_present && byte_pending)begin
                byte_source_local<=held_byte_source;
                byte_data_local<=held_byte_data;
                byte_tick_local<=held_byte_tick;
            end
            if(!flush_pending) begin flush_present<=0;flush_accepted<=0;end
            else if(!flush_present && !flush_accepted)begin
                flush_mask_local<=held_flush_mask;
                flush_present<=1;
            end
            if(flush_fire) begin
                flush_present<=0;flush_accepted<=1;
                for(integer s=0;s<4;s=s+1)
                    if(flush_mask_local[s])received_epoch[s]<=held_flush_epoch[s];
            end
            // Only a real CPU POP completion releases the flush transfer.
            if(dst_flush_done_valid_i && flush_accepted &&
               (dst_flush_done_mask_i & flush_mask_local)==flush_mask_local) begin
                flush_ack<=flush_req_sync;flush_present<=0;flush_accepted<=0;
            end
            if(cancel_byte || done_cancel || (byte_pending && byte_stale)) begin
                byte_present<=0;
                if(byte_pending)byte_ack<=byte_req_sync;
            end else if(dst_byte_valid_o && dst_byte_ready_i) begin
                byte_present<=0;byte_ack<=byte_req_sync;
            end else if(!byte_present && byte_pending)begin
                byte_present<=1;
            end
            // Recovery changes a separate epoch, so it cannot roll back a
            // concurrently launched source flush epoch. Even late requests
            // that have not reached byte_req_sync are rejected after recovery.
            if(dst_flush_done_valid_i) begin
                for(integer s=0;s<4;s=s+1)
                    if(dst_flush_done_mask_i[s])recovery_epoch[s]<=recovery_epoch[s]+32'd1;
                done_pending_mask<=done_pending_mask | dst_flush_done_mask_i;
                done_pending_sequence<=dst_flush_done_sequence_i;
            end
            if(done_req==done_ack_sync && (|done_pending_mask)) begin
                held_done_mask<=done_pending_mask;held_done_sequence<=done_pending_sequence;
                held_done_recovery<=recovery_epoch;done_req<=~done_req;
                // A concurrent completion belongs to the NEXT return slot.
                done_pending_mask<=dst_flush_done_valid_i ? dst_flush_done_mask_i : 4'd0;
            end
        end
    end
endmodule
`default_nettype wire
