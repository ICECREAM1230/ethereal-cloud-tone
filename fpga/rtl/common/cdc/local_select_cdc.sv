`default_nettype none
// One stable command and one stable completion cross independent clocks.
// A shared asynchronous reset cancels both endpoints; cancelled commands are
// never replayed when either clock returns.
module local_select_cdc (
    input wire src_clk_i,dst_clk_i,rst_n_i,
    input wire src_valid_i,src_bind_i,
    input wire [31:0] src_id_i,
    output wire src_ready_o,
    output logic src_done_o,
    output logic [31:0] src_done_id_o,src_result_o,
    output wire dst_valid_o,
    output wire dst_bind_o,
    output wire [31:0] dst_id_o,
    input wire dst_ready_i,dst_done_i,
    input wire [31:0] dst_done_id_i,dst_result_i
);
    logic req,ack,src_active,dst_waiting,dst_offer;
    logic [32:0] payload,captured;
    logic [63:0] response;
    (* ASYNC_REG="TRUE" *) logic [1:0] req_sync,ack_sync,ready_sync;
    (* ASYNC_REG="TRUE" *) logic [1:0] src_reset_sync,dst_reset_sync;
    always_ff @(posedge src_clk_i or negedge rst_n_i)
        if(!rst_n_i)src_reset_sync<=0;else src_reset_sync<={src_reset_sync[0],1'b1};
    always_ff @(posedge dst_clk_i or negedge rst_n_i)
        if(!rst_n_i)dst_reset_sync<=0;else dst_reset_sync<={dst_reset_sync[0],1'b1};
    wire src_rst_n=src_reset_sync[1],dst_rst_n=dst_reset_sync[1];
    assign src_ready_o=src_rst_n && ready_sync[1] && !src_active;
    assign dst_valid_o=dst_rst_n && dst_offer;
    assign {dst_bind_o,dst_id_o}=captured;
    always_ff @(posedge src_clk_i or negedge src_rst_n)begin
        if(!src_rst_n)begin
            req<=0;src_active<=0;payload<=0;ack_sync<=0;ready_sync<=0;
            src_done_o<=0;src_done_id_o<=0;src_result_o<=0;
        end else begin
            ack_sync<={ack_sync[0],ack};ready_sync<={ready_sync[0],dst_rst_n};src_done_o<=0;
            if(src_valid_i && src_ready_o)begin
                payload<={src_bind_i,src_id_i};req<=~req;src_active<=1;
            end else if(src_active && ack_sync[1]==req)begin
                {src_done_id_o,src_result_o}<=response;src_done_o<=1;src_active<=0;
            end
        end
    end
    always_ff @(posedge dst_clk_i or negedge dst_rst_n)begin
        if(!dst_rst_n)begin
            ack<=0;req_sync<=0;captured<=0;response<=0;dst_offer<=0;dst_waiting<=0;
        end else begin
            req_sync<={req_sync[0],req};
            if(!dst_offer && !dst_waiting && req_sync[1]!=ack)begin
                captured<=payload;dst_offer<=1;
            end
            if(dst_offer && dst_ready_i)begin dst_offer<=0;dst_waiting<=1;end
            if(dst_waiting && dst_done_i && dst_done_id_i==captured[31:0])begin
                response<={dst_done_id_i,dst_result_i};ack<=req_sync[1];dst_waiting<=0;
            end
        end
    end
endmodule
`default_nettype wire
