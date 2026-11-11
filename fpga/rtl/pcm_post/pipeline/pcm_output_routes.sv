`default_nettype none
// AOR1: one immutable SPI request, applied atomically at an audio frame edge.
module pcm_output_routes(
    input wire cfg_clk_i,audio_clk_i,rst_ni,cfg_cs_ni,
    input wire write_i,input wire [15:0] write_addr_i,input wire [31:0] write_data_i,
    input wire read_req_i,input wire [15:0] read_addr_i,input wire frame_accept_i,
    output wire [3:0] routes_o,output logic read_selected_o,output logic [31:0] read_data_o
);
    logic cfg_reset_n,audio_reset_n;
    jt51_reset_sync u_cfg_reset(.clk_i(cfg_clk_i),.rst_n_i(rst_ni),.rst_n_o(cfg_reset_n));
    jt51_reset_sync u_audio_reset(.clk_i(audio_clk_i),.rst_n_i(rst_ni),.rst_n_o(audio_reset_n));
    logic [31:0] held_command;
    logic request_cfg,ack_audio;
    (* ASYNC_REG="TRUE" *)logic ack_meta,ack_sync,ready_meta,ready_sync,req_meta,req_sync;
    (* ASYNC_REG="TRUE" *)logic cs_meta,cs_sync;
    logic armed,ready;
    logic [3:0] active_routes;
    logic [27:0] applied_sequence;
    logic [31:0] generation;
    wire busy=req_sync!=ack_audio;
    wire legal_command=|held_command[31:4];
    // The serializer sees the new pair on exactly the edge that publishes ACK.
    assign routes_o=ready && busy && legal_command?held_command[3:0]:active_routes;
    always_ff @(posedge cfg_clk_i or negedge cfg_reset_n)begin
        if(!cfg_reset_n)begin held_command<=0;request_cfg<=0;ack_meta<=0;ack_sync<=0;ready_meta<=0;ready_sync<=0;end
        else begin
            ack_meta<=ack_audio;ack_sync<=ack_meta;ready_meta<=ready;ready_sync<=ready_meta;
            if(write_i && write_addr_i==16'h07b5 && ready_sync && request_cfg==ack_sync && |write_data_i[31:4])begin
                held_command<=write_data_i;request_cfg<=~request_cfg;
            end
        end
    end
    always_ff @(posedge audio_clk_i or negedge audio_reset_n)begin
        if(!audio_reset_n)begin
            req_meta<=0;req_sync<=0;cs_meta<=0;cs_sync<=0;armed<=0;ready<=0;
            active_routes<=4'h4;applied_sequence<=0;generation<=0;ack_audio<=0;
        end else begin
            req_meta<=request_cfg;req_sync<=req_meta;cs_meta<=cfg_cs_ni;cs_sync<=cs_meta;
            if(cs_sync)armed<=1;
            ready<=armed;
            if(ready && busy && frame_accept_i)begin
                if(legal_command)begin active_routes<=held_command[3:0];applied_sequence<=held_command[31:4];generation<=generation+1'b1;end
                ack_audio<=req_sync;
            end
        end
    end
    (* ASYNC_REG="TRUE" *)logic read_meta;
    logic read_sync,read_previous,read_pending;
    always_ff @(posedge audio_clk_i or negedge audio_reset_n)begin
        if(!audio_reset_n)begin read_meta<=0;read_sync<=0;read_previous<=0;read_pending<=0;read_selected_o<=0;read_data_o<=0;end
        else begin
            read_meta<=read_req_i;read_sync<=read_meta;read_previous<=read_sync;read_pending<=read_sync!=read_previous;
            if(read_pending)begin
                read_selected_o<=read_addr_i[15:3]==13'h0f6;
                case(read_addr_i[2:0])
                    0:read_data_o<=32'h414f5231;
                    1:read_data_o<=32'h00010000;
                    2:read_data_o<={30'd0,busy,ready};
                    3:read_data_o<={28'd0,active_routes};
                    4:read_data_o<={4'd0,applied_sequence};
                    7:read_data_o<=generation;
                    default:read_data_o<=0;
                endcase
            end
        end
    end
endmodule
`default_nettype wire
