// SPDX-License-Identifier: MIT
`default_nettype none
// Publish persistent control state and acknowledge its destination register
// application. Intermediate desired states may coalesce before launch; an
// in-flight packet is immutable. Short pulses use an event/command channel.
module control_state_cdc #(
    parameter integer WIDTH=32,
    parameter logic [WIDTH-1:0] RESET_VALUE='0
)(
    input wire src_clk_i,dst_clk_i,rst_n_i,
    input wire [WIDTH-1:0] src_data_i,
    output wire src_busy_o,
    output logic src_applied_valid_o,
    output logic [WIDTH-1:0] src_applied_data_o,
    output logic [31:0] src_applied_id_o,
    output logic src_done_o,
    output logic [31:0] src_sent_count_o,src_applied_count_o,
    input wire dst_ready_i,
    // Bound published-state data fanout without adding a destination cycle.
    output logic [WIDTH-1:0] dst_data_o /* synthesis syn_maxfan = 16 */,
    output logic [31:0] dst_id_o,
    output logic dst_updated_o
);
    wire source_reset_n,destination_reset_n;
    jt51_reset_sync u_source_reset(.clk_i(src_clk_i),.rst_n_i(rst_n_i),.rst_n_o(source_reset_n));
    jt51_reset_sync u_destination_reset(.clk_i(dst_clk_i),.rst_n_i(rst_n_i),.rst_n_o(destination_reset_n));
    logic active,offer,launched;
    logic [31:0] next_id;
    logic [WIDTH+31:0] packet;
    wire source_ready,destination_valid;
    wire [WIDTH+31:0] received;
    assign src_busy_o=!src_applied_valid_o || active || src_data_i!=src_applied_data_o;
    held_bus_cdc #(.WIDTH(WIDTH+32)) u_payload_cdc(
        .src_clk_i(src_clk_i),.dst_clk_i(dst_clk_i),.rst_n_i(rst_n_i),
        .src_data_i(packet),.src_valid_i(offer),.src_ready_o(source_ready),
        .dst_data_o(received),.dst_valid_o(destination_valid),
        .dst_ready_i(dst_ready_i && destination_reset_n));
    always_ff @(posedge src_clk_i or negedge source_reset_n)begin
        if(!source_reset_n)begin
            active<=0;offer<=0;launched<=0;next_id<=1;packet<='0;
            src_applied_valid_o<=0;src_applied_data_o<=RESET_VALUE;src_applied_id_o<=0;
            src_done_o<=0;src_sent_count_o<=0;src_applied_count_o<=0;
        end else begin
            src_done_o<=0;
            if(!active && (!src_applied_valid_o || src_data_i!=src_applied_data_o))begin
                packet<={next_id,src_data_i};next_id<=next_id==32'hffffffff ? 32'd1 : next_id+32'd1;
                active<=1;offer<=1;launched<=0;
            end
            if(offer && source_ready)begin
                offer<=0;launched<=1;src_sent_count_o<=src_sent_count_o+32'd1;
            end
            if(active && launched && source_ready)begin
                src_applied_data_o<=packet[WIDTH-1:0];src_applied_id_o<=packet[WIDTH+31:WIDTH];
                src_applied_valid_o<=1;src_done_o<=1;active<=0;launched<=0;
                src_applied_count_o<=src_applied_count_o+32'd1;
            end
        end
    end
    always_ff @(posedge dst_clk_i or negedge destination_reset_n)begin
        if(!destination_reset_n)begin dst_data_o<=RESET_VALUE;dst_id_o<=0;dst_updated_o<=0;end
        else begin
            dst_updated_o<=0;
            if(destination_valid && dst_ready_i)begin
                dst_data_o<=received[WIDTH-1:0];dst_id_o<=received[WIDTH+31:WIDTH];dst_updated_o<=1;
            end
        end
    end
endmodule
`default_nettype wire
