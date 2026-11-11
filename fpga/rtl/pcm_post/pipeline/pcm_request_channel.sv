`default_nettype none
// One source's request/complete transport. Only complete tagged stereo
// packets cross clock domains; an engine's one-cycle completion is retained
// until the bundled-data handshake accepts it.
module pcm_request_channel #(
    parameter bit ENABLE=1'b1
)(
    input wire global_rst_n_i,audio_clk_i,audio_rst_n_i,
    input wire native_clk_i,native_rst_n_i,
    input wire request_tick_i,
    input wire [63:0] request_tag_i,
    output wire request_valid_o,
    input wire request_ready_i,
    output wire [63:0] request_tag_o,
    input wire response_valid_i,
    input wire [63:0] response_tag_i,
    input wire signed [23:0] left_i,right_i,
    output wire response_valid_o,
    output wire [111:0] response_o,
    output wire error_o
);
    generate if(ENABLE)begin:g_enabled
        wire link_reset_n=global_rst_n_i && audio_rst_n_i && native_rst_n_i;
        wire audio_reset_n,native_reset_n;
        jt51_reset_sync u_audio_reset(.clk_i(audio_clk_i),.rst_n_i(link_reset_n),.rst_n_o(audio_reset_n));
        jt51_reset_sync u_native_reset(.clk_i(native_clk_i),.rst_n_i(link_reset_n),.rst_n_o(native_reset_n));
        logic request_pending,request_error;
        logic [63:0] held_request;
        wire request_ready;
        always_ff @(posedge audio_clk_i or negedge audio_reset_n)begin
            if(!audio_reset_n)begin request_pending<=0;held_request<=0;request_error<=0;end
            else begin
                if(request_pending && request_ready)request_pending<=0;
                if(request_tick_i)begin
                    if(!request_pending || request_ready)begin
                        held_request<=request_tag_i;request_pending<=1;
                    end else request_error<=1;
                end
            end
        end
        held_bus_cdc #(.WIDTH(64)) u_request(
            .src_clk_i(audio_clk_i),.dst_clk_i(native_clk_i),.rst_n_i(link_reset_n),
            .src_data_i(held_request),.src_valid_i(request_pending),.src_ready_o(request_ready),
            .dst_data_o(request_tag_o),.dst_valid_o(request_valid_o),.dst_ready_i(request_ready_i));
        logic response_pending,response_error;
        logic [111:0] held_response;
        wire response_ready;
        always_ff @(posedge native_clk_i or negedge native_reset_n)begin
            if(!native_reset_n)begin response_pending<=0;held_response<=0;response_error<=0;end
            else begin
                if(response_pending && response_ready)response_pending<=0;
                if(response_valid_i)begin
                    if(!response_pending || response_ready)begin
                        held_response<={response_tag_i,left_i,right_i};response_pending<=1;
                    end else response_error<=1;
                end
            end
        end
        held_bus_cdc #(.WIDTH(112)) u_response(
            .src_clk_i(native_clk_i),.dst_clk_i(audio_clk_i),.rst_n_i(link_reset_n),
            .src_data_i(held_response),.src_valid_i(response_pending),.src_ready_o(response_ready),
            .dst_data_o(response_o),.dst_valid_o(response_valid_o),.dst_ready_i(1'b1));
        (* ASYNC_REG="TRUE" *)logic response_error_meta,response_error_sync;
        always_ff @(posedge audio_clk_i or negedge audio_reset_n)
            if(!audio_reset_n)begin response_error_meta<=0;response_error_sync<=0;end
            else begin response_error_meta<=response_error;response_error_sync<=response_error_meta;end
        assign error_o=request_error || response_error_sync;
    end else begin:g_disabled
        assign request_valid_o=0;assign request_tag_o=0;
        assign response_valid_o=0;assign response_o=0;assign error_o=0;
    end endgenerate
endmodule
`default_nettype wire
