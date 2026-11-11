`default_nettype none
// Source-paced complete stereo frames. The return path carries the exact
// epoch/sequence accepted by the worker; reset cancels both CDC endpoints.
module pcm_frame_link #(
    parameter integer INPUT_DELAY_CYCLES=1
)(
    input wire global_rst_n_i, link_reset_n_i,
    input wire src_clk_i, dst_clk_i, sample_tick_i,
    input wire signed [23:0] left_i,right_i,
    output logic signed [23:0] left_o,right_o,
    output logic valid_o,
    output wire online_o,
    output wire [111:0] worker_data_o,
    output wire worker_valid_o,
    input wire worker_ready_i,
    input wire [111:0] result_data_i,
    input wire result_valid_i,
    output wire result_ready_o,
    output wire [127:0] stats_o
);
    wire src_online,source_global_reset_n;
    jt51_reset_sync u_source_global_reset(.clk_i(src_clk_i),
        .rst_n_i(global_rst_n_i),.rst_n_o(source_global_reset_n));
    jt51_reset_sync u_source_reset(.clk_i(src_clk_i),.rst_n_i(link_reset_n_i),.rst_n_o(src_online));
    assign online_o=src_online;
    logic [INPUT_DELAY_CYCLES-1:0] tick_delay;
    logic was_online;
    logic [31:0] epoch, sequence_r;
    logic pending;
    logic [111:0] packet;
    wire source_ready;
    wire capture=src_online && was_online && tick_delay[INPUT_DELAY_CYCLES-1];
    wire input_overflow=capture && pending && !source_ready;
    logic [31:0] captured_count,sent_count,returned_count,error_count;
    logic error_pending;
    wire [111:0] returned_packet;
    wire returned_valid;
    logic [31:0] return_sequence;
    wire good_return=returned_packet[111:80]==epoch &&
                     returned_packet[79:48]==return_sequence+32'd1;
    // A new source frame is a real deadline, not a DSP worker clock tick.
    // Register diagnostic qualification before the counter carry chain.
    wire deadline_miss=sample_tick_i && src_online && was_online &&
                       (sequence_r!=return_sequence) &&
                       !(returned_valid && good_return && returned_packet[79:48]==sequence_r);
    function automatic [31:0] bump(input [31:0] v);
        bump = v == 32'hffffffff ? 32'd1 : v + 32'd1;
    endfunction
    always_ff @(posedge src_clk_i or negedge source_global_reset_n)begin
        if(!source_global_reset_n)begin
            was_online<=0;epoch<=0;sequence_r<=0;
            captured_count<=0;sent_count<=0;returned_count<=0;error_count<=0;error_pending<=0;
        end else begin
            was_online<=src_online;
            if(src_online && !was_online)begin epoch<=bump(epoch);sequence_r<=0;end
            else if(capture)begin sequence_r<=sequence_r+32'd1;captured_count<=captured_count+32'd1;end
            if(pending && source_ready)sent_count<=sent_count+32'd1;
            if(returned_valid && src_online)begin
                if(good_return)returned_count<=returned_count+32'd1;
            end
            error_pending<=input_overflow || deadline_miss || (returned_valid && src_online && !good_return);
            if(error_pending && error_count!=32'hffffffff)error_count<=error_count+32'd1;
        end
    end
    always_ff @(posedge src_clk_i or negedge src_online)begin
        if(!src_online)begin tick_delay<='0;pending<=0;packet<='0;end
        else begin
            if(!was_online)tick_delay<='0;
            // Preserve the old OR's Z-to-X behavior in four-state simulation.
            else tick_delay<=INPUT_DELAY_CYCLES'({tick_delay,1'b0 | sample_tick_i});
            if(pending && source_ready)pending<=0;
            if(capture && (!pending || source_ready))begin
                packet<={epoch,sequence_r+32'd1,left_i,right_i};pending<=1;
            end
        end
    end
    held_bus_cdc #(.WIDTH(112)) u_input_cdc(
        .src_clk_i(src_clk_i),.dst_clk_i(dst_clk_i),.rst_n_i(link_reset_n_i),
        .src_data_i(packet),.src_valid_i(pending),.src_ready_o(source_ready),
        .dst_data_o(worker_data_o),.dst_valid_o(worker_valid_o),.dst_ready_i(worker_ready_i));
    held_bus_cdc #(.WIDTH(112)) u_return_cdc(
        .src_clk_i(dst_clk_i),.dst_clk_i(src_clk_i),.rst_n_i(link_reset_n_i),
        .src_data_i(result_data_i),.src_valid_i(result_valid_i),.src_ready_o(result_ready_o),
        .dst_data_o(returned_packet),.dst_valid_o(returned_valid),.dst_ready_i(1'b1));
    always_ff @(posedge src_clk_i or negedge src_online)begin
        if(!src_online)begin left_o<=0;right_o<=0;valid_o<=0;return_sequence<=0;end
        else begin
            valid_o<=0;
            if(returned_valid && good_return)begin
                left_o<=$signed(returned_packet[47:24]);right_o<=$signed(returned_packet[23:0]);
                return_sequence<=returned_packet[79:48];valid_o<=1;
            end
        end
    end
    assign stats_o={captured_count,sent_count,returned_count,error_count};
`ifndef SYNTHESIS
    initial if(INPUT_DELAY_CYCLES<1)$fatal(1,"PCM source delay must be positive");
`endif
endmodule
`default_nettype wire
