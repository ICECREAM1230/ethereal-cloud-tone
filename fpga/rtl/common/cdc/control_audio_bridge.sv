// SPDX-License-Identifier: MIT
`default_nettype none
// Persistent control generation goes forward; coherent status and counted
// frame events return. A narrow native pulse never crosses as a raw wire.
module control_audio_bridge(
    input wire control_clk_i,control_rst_n_i,subtractive_clk_i,subtractive_rst_n_i,
    input wire [47:0] state_i,
    output wire [47:0] state_o,
    output wire state_busy_o,state_applied_valid_o,
    output wire [31:0] state_applied_id_o,state_sent_o,state_applied_o,
    input wire sample_tick_i,dac_frame_tick_i,dac_frame_fm_i,dac_busy_i,dac_fm_active_i,
    input wire [31:0] template_status_i,
    input wire [15:0] control_pass_count_i,
    input wire bank_active_i,dmem_ack_i,test_tone_i,
    output wire [31:0] template_status_o,
    output wire [15:0] control_pass_count_o,
    output wire bank_active_o,dmem_ack_o,test_tone_o,dac_busy_o,dac_fm_active_o,
    output logic sample_tick_o,dac_frame_tick_o,dac_frame_fm_o,
    output logic [2:0] dac_frame_route_o,
    output logic [31:0] dac_frame_revision_o,
    output wire [31:0] frame_count_o
);
    wire link_reset_n=control_rst_n_i && subtractive_rst_n_i;
    wire state_done,state_updated;
    wire [47:0] applied_state;
    wire [31:0] destination_state_id;
    control_state_cdc #(.WIDTH(48),.RESET_VALUE(48'h000000000043)) u_control_state(
        .src_clk_i(control_clk_i),.dst_clk_i(subtractive_clk_i),.rst_n_i(link_reset_n),
        .src_data_i(state_i),.src_busy_o(state_busy_o),.src_applied_valid_o(state_applied_valid_o),
        .src_applied_data_o(applied_state),.src_applied_id_o(state_applied_id_o),.src_done_o(state_done),
        .src_sent_count_o(state_sent_o),.src_applied_count_o(state_applied_o),
        .dst_ready_i(1'b1),.dst_data_o(state_o),.dst_id_o(destination_state_id),.dst_updated_o(state_updated));
    wire native_reset_n,control_reset_n;
    jt51_reset_sync u_native_reset(.clk_i(subtractive_clk_i),.rst_n_i(link_reset_n),.rst_n_o(native_reset_n));
    jt51_reset_sync u_control_reset(.clk_i(control_clk_i),.rst_n_i(link_reset_n),.rst_n_o(control_reset_n));
    logic [31:0] frames,samples,frame_revision;
    logic frame_fm;
    logic [2:0] frame_route;
    always_ff @(posedge subtractive_clk_i or negedge native_reset_n)begin
        if(!native_reset_n)begin frames<=0;samples<=0;frame_revision<=0;frame_fm<=0;frame_route<=0;end
        else begin
            if(sample_tick_i)samples<=samples+32'd1;
            if(dac_frame_tick_i)begin
                frames<=frames+32'd1;frame_revision<=state_o[41:10];
                frame_fm<=dac_frame_fm_i;frame_route<={state_o[42],state_o[8:7]};
            end
        end
    end
    // Preserve every old status field; route[2] borrows reserved bit 152.
    wire [159:0] status_in={7'd0,frame_route[2],frames,samples,frame_revision,template_status_i,control_pass_count_i,
        bank_active_i,test_tone_i,dmem_ack_i,dac_busy_i,dac_fm_active_i,frame_fm,frame_route[1:0]};
    wire [159:0] status_out;
    wire unused_status_ready,status_valid;
    held_bus_cdc #(.WIDTH(160)) u_status_cdc(
        .src_clk_i(subtractive_clk_i),.dst_clk_i(control_clk_i),.rst_n_i(link_reset_n),
        .src_data_i(status_in),.src_valid_i(1'b1),.src_ready_o(unused_status_ready),
        .dst_data_o(status_out),.dst_valid_o(status_valid),.dst_ready_i(1'b1));
    wire [6:0] reserved;
    wire [31:0] sample_count,frame_revision_snapshot;
    wire frame_fm_snapshot;
    wire [2:0] frame_route_snapshot;
    assign {reserved,frame_route_snapshot[2],frame_count_o,sample_count,frame_revision_snapshot,template_status_o,control_pass_count_o,
        bank_active_o,test_tone_o,dmem_ack_o,dac_busy_o,dac_fm_active_o,frame_fm_snapshot,frame_route_snapshot[1:0]}=status_out;
    logic [31:0] previous_samples,previous_frames;
    always_ff @(posedge control_clk_i or negedge control_reset_n)begin
        if(!control_reset_n)begin
            previous_samples<=0;previous_frames<=0;sample_tick_o<=0;dac_frame_tick_o<=0;
            dac_frame_fm_o<=0;dac_frame_route_o<=0;dac_frame_revision_o<=0;
        end else begin
            sample_tick_o<=0;dac_frame_tick_o<=0;
            if(status_valid)begin
                previous_samples<=sample_count;previous_frames<=frame_count_o;
                sample_tick_o<=sample_count!=previous_samples;
                if(frame_count_o!=previous_frames)begin
                    dac_frame_tick_o<=1;dac_frame_fm_o<=frame_fm_snapshot;
                    dac_frame_route_o<=frame_route_snapshot;dac_frame_revision_o<=frame_revision_snapshot;
                end
            end
        end
    end
endmodule
`default_nettype wire
