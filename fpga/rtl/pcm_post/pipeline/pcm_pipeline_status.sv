`default_nettype none
// Read-only coherent diagnostic image. Read 0x0740 to freeze a fresh image;
// Default 0741..074F belong to that image until the next BASE_ADDR fetch.
// The main project overrides BASE_ADDR=0780 to preserve the sensor ABI.
module pcm_pipeline_status #(
    parameter bit SPLIT_SYNTH_STATUS=1'b0,
    parameter logic [15:0] BASE_ADDR=16'h0740
)(
    input wire sys_clk_i,fm_clk_i,audio_clk_i,rst_n_i,
    input wire subtractive_clk_i,
    input wire [127:0] synth_stats_i,fm_stats_i,audio_stats_i,
    input wire route_fm_i,audio_reset_n_i,
    input wire read_req_toggle_i,
    input wire [15:0] read_addr_i,
    output logic read_selected_o,
    output logic [31:0] read_data_o
);
    wire [127:0] fm_snapshot,audio_snapshot,synth_snapshot;
    wire route_control;
    generate if(SPLIT_SYNTH_STATUS)begin:g_subtractive_status
        wire unused_ready,unused_valid;
        held_bus_cdc #(.WIDTH(128)) u_snapshot_cdc(
            .src_clk_i(subtractive_clk_i),.dst_clk_i(sys_clk_i),.rst_n_i(rst_n_i),
            .src_data_i(synth_stats_i),.src_valid_i(1'b1),.src_ready_o(unused_ready),
            .dst_data_o(synth_snapshot),.dst_valid_o(unused_valid),.dst_ready_i(1'b1));
        (* ASYNC_REG="TRUE" *)logic route_meta,route_sync;
        always_ff @(posedge sys_clk_i or negedge rst_n_i)
            if(!rst_n_i)begin route_meta<=0;route_sync<=0;end
            else begin route_meta<=route_fm_i;route_sync<=route_meta;end
        assign route_control=route_sync;
    end else begin:g_same_status_clock
        assign synth_snapshot=synth_stats_i;
        assign route_control=route_fm_i;
    end endgenerate
    wire unused_fm_ready,unused_audio_ready,unused_fm_valid,unused_audio_valid;
    held_bus_cdc #(.WIDTH(128)) u_fm_snapshot_cdc(
        .src_clk_i(fm_clk_i),.dst_clk_i(sys_clk_i),.rst_n_i(rst_n_i),
        .src_data_i(fm_stats_i),.src_valid_i(1'b1),.src_ready_o(unused_fm_ready),
        .dst_data_o(fm_snapshot),.dst_valid_o(unused_fm_valid),.dst_ready_i(1'b1));
    held_bus_cdc #(.WIDTH(128)) u_audio_snapshot_cdc(
        .src_clk_i(audio_clk_i),.dst_clk_i(sys_clk_i),.rst_n_i(rst_n_i),
        .src_data_i(audio_stats_i),.src_valid_i(1'b1),.src_ready_o(unused_audio_ready),
        .dst_data_o(audio_snapshot),.dst_valid_o(unused_audio_valid),.dst_ready_i(1'b1));
    (* ASYNC_REG="TRUE" *)logic req_meta;
    logic req_sync,req_previous,read_pending;
    (* ASYNC_REG="TRUE" *)logic audio_reset_meta,audio_reset_sync;
    logic [127:0] frozen_synth,frozen_fm,frozen_audio;
    logic [31:0] generation,frozen_flags;
    always_ff @(posedge sys_clk_i or negedge rst_n_i)begin
        if(!rst_n_i)begin
            req_meta<=0;req_sync<=0;req_previous<=0;read_pending<=0;
            audio_reset_meta<=0;audio_reset_sync<=0;
            frozen_synth<=0;frozen_fm<=0;frozen_audio<=0;
            generation<=0;frozen_flags<=0;read_selected_o<=0;read_data_o<=0;
        end else begin
            req_meta<=read_req_toggle_i;req_sync<=req_meta;req_previous<=req_sync;
            read_pending<=req_sync!=req_previous;
            audio_reset_meta<=audio_reset_n_i;audio_reset_sync<=audio_reset_meta;
            if(read_pending)begin
                read_selected_o<=read_addr_i[15:4]==BASE_ADDR[15:4];
                case(read_addr_i[3:0])
                    4'h0:begin
                        read_data_o<=32'h50434d31;
                        if(read_addr_i[15:4]==BASE_ADDR[15:4])begin
                            frozen_synth<=synth_snapshot;frozen_fm<=fm_snapshot;frozen_audio<=audio_snapshot;
                            generation<=generation+32'd1;
                            frozen_flags<={30'd0,audio_reset_sync,route_control};
                        end
                    end
                    4'h1:read_data_o<=generation;
                    4'h2:read_data_o<=frozen_flags;
                    4'h3:read_data_o<=frozen_synth[127:96];
                    4'h4:read_data_o<=frozen_synth[95:64];
                    4'h5:read_data_o<=frozen_synth[63:32];
                    4'h6:read_data_o<=frozen_synth[31:0];
                    4'h7:read_data_o<=frozen_fm[127:96];
                    4'h8:read_data_o<=frozen_fm[95:64];
                    4'h9:read_data_o<=frozen_fm[63:32];
                    4'ha:read_data_o<=frozen_fm[31:0];
                    4'hb:read_data_o<=frozen_audio[127:96];
                    4'hc:read_data_o<=frozen_audio[95:64];
                    4'hd:read_data_o<=frozen_audio[63:32];
                    4'he:read_data_o<=frozen_audio[31:0];
                    default:read_data_o<=0;
                endcase
            end
        end
    end
endmodule
`default_nettype wire
