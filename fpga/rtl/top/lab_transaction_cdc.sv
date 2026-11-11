`timescale 1ns/1ps
`default_nettype none
// A source handshake means the destination has accepted the transaction.
// Unlike an enqueue-ack mailbox, ready returns only after the destination's
// ready/valid edge and the response have crossed back. Both sides share the
// coordinated ownership reset; the two local releases are synchronized.
module lab_transaction_cdc #(
    parameter integer WIDTH=192,
    parameter integer RESPONSE_WIDTH=32
) (
    input wire src_clk_i,dst_clk_i,rst_ni,
    input wire [WIDTH-1:0] src_data_i,
    input wire src_valid_i,
    output wire src_ready_o,
    output wire [RESPONSE_WIDTH-1:0] src_response_o,
    output logic [WIDTH-1:0] dst_data_o,
    output logic dst_valid_o,
    input wire dst_ready_i,
    input wire [RESPONSE_WIDTH-1:0] dst_response_i
);
    (* async_reg="true" *) logic src_reset_meta,src_reset_n,dst_reset_meta,dst_reset_n;
    (* async_reg="true" *) logic req_meta,req_sync,ack_meta,ack_sync;
    logic request,ack;
    logic [1:0] src_state;
    logic [WIDTH-1:0] payload_hold;
    logic [RESPONSE_WIDTH-1:0] response_hold;
    always_ff @(posedge src_clk_i or negedge rst_ni)
        if(!rst_ni)begin src_reset_meta<=0;src_reset_n<=0;end
        else begin src_reset_meta<=1;src_reset_n<=src_reset_meta;end
    always_ff @(posedge dst_clk_i or negedge rst_ni)
        if(!rst_ni)begin dst_reset_meta<=0;dst_reset_n<=0;end
        else begin dst_reset_meta<=1;dst_reset_n<=dst_reset_meta;end
    assign src_ready_o=src_reset_n && src_state==1 && ack_sync;
    assign src_response_o=response_hold;
    always_ff @(posedge src_clk_i or negedge src_reset_n)begin
        if(!src_reset_n)begin
            src_state<=0;request<=0;ack_meta<=0;ack_sync<=0;payload_hold<=0;
        end else begin
            ack_meta<=ack;ack_sync<=ack_meta;
            case(src_state)
                0:if(src_valid_i)begin payload_hold<=src_data_i;request<=1;src_state<=1;end
                1:if(ack_sync)begin request<=0;src_state<=2;end
                2:if(!ack_sync)src_state<=0;
                default:src_state<=0;
            endcase
        end
    end
    always_ff @(posedge dst_clk_i or negedge dst_reset_n)begin
        if(!dst_reset_n)begin
            req_meta<=0;req_sync<=0;ack<=0;dst_valid_o<=0;dst_data_o<=0;response_hold<=0;
        end else begin
            req_meta<=request;req_sync<=req_meta;
            if(!req_sync)ack<=0;
            if(dst_valid_o && dst_ready_i)begin
                dst_valid_o<=0;response_hold<=dst_response_i;ack<=1;
            end else if(req_sync && !ack && !dst_valid_o)begin
                dst_data_o<=payload_hold;dst_valid_o<=1;
            end
        end
    end
endmodule
`default_nettype wire
