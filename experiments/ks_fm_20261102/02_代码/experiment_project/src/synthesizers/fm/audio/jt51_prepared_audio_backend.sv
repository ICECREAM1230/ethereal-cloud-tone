// SPDX-License-Identifier: GPL-3.0-or-later
// Prepared host only. The original native96 renderer and tagged PCM cadence
// are unchanged; this module contains no MIDI/patch/operator parameter compiler.
`default_nettype none
module jt51_prepared_audio_backend(
 input wire clk_i,rst_n_i,
 input wire cmd_valid_i,output wire cmd_ready_o,output wire [1:0] cmd_result_o,input wire [159:0] cmd_data_i,
 output wire ready_o,initialized_o,fault_o,
 output wire fb_valid_o,input wire fb_ready_i,output wire [4:0] fb_voice_o,output wire [31:0] fb_generation_o,
 input wire pcm_request_valid_i,output wire pcm_request_ready_o,input wire [63:0] pcm_request_tag_i,
 output logic pcm_response_valid_o,output logic [63:0] pcm_response_tag_o,
 output logic signed [23:0] pcm_response_left_o,pcm_response_right_o
);
 wire native_rst_n;
 jt51_reset_sync native_reset(.clk_i(clk_i),.rst_n_i(rst_n_i),.rst_n_o(native_rst_n));
 wire core_reset,core_ready,sample_valid,writer_ready,writer_done,writer_fault,wr_valid;
 wire [7:0] wr_addr,wr_data,status,bus_data,mute_mask,idle_mask;
 wire bus_cs,bus_wr,bus_a0,operator_step,chip_request_ready,request_done;
 wire signed [15:0] left,right;
 wire [63:0] request_done_tag;
 logic request_pending;
 logic [63:0] pending_tag;
 assign pcm_request_ready_o=native_rst_n&&!request_pending&&(core_reset||chip_request_ready);
 wire chip_request_valid=pcm_request_valid_i&&pcm_request_ready_o&&!core_reset;
 ae350_voice_backend_fm u_executor(.clk(clk_i),.rst_n(native_rst_n),
  .cmd_valid_i,.cmd_ready_o,.cmd_result_o,.cmd_data_i,.ready_o,.initialized_o,
  .fb_valid_o,.fb_ready_i,.fb_voice_o,.fb_generation_o,
  .register_valid_o(wr_valid),.register_ready_i(writer_ready),.register_done_i(writer_done),.register_fault_i(writer_fault),
  .register_address_o(wr_addr),.register_data_o(wr_data),.sample_valid_i(sample_valid),.core_ready_i(core_ready),
  .voice_idle_i(idle_mask),.core_reset_o(core_reset),.mute_mask_o(mute_mask),.fault_o);
 jt51_native96_top #(.ENABLE_AE350_VOICE_OWNER(1'b1),.PCM_REQUEST_DRIVEN(1'b1)) chip(
  .ae350_mute_i(mute_mask),.ae350_idle_o(idle_mask),.clk_i,.rst_i(core_reset),.panic_i(1'b0),
  .cs_n_i(bus_cs),.wr_n_i(bus_wr),.a0_i(bus_a0),.din_i(bus_data),.status_o(status),
  .irq_n_o(),.ready_o(core_ready),.sample_valid_o(sample_valid),.left_o(left),.right_o(right),.chip_left_o(),.chip_right_o(),
  .pcm_request_valid_i(chip_request_valid),.pcm_request_ready_o(chip_request_ready),.pcm_request_tag_i,
  .pcm_request_done_o(request_done),.pcm_request_tag_o(request_done_tag),.operator_step_o(operator_step));
 jt51_register_writer #(.CLOCK_ENABLE(1'b1)) writer(.clk_i,.step_i(operator_step),.rst_i(core_reset),
  .valid_i(wr_valid),.addr_i(wr_addr),.data_i(wr_data),.status_i(status),.ready_o(writer_ready),
  .done_o(writer_done),.fault_o(writer_fault),.cs_n_o(bus_cs),.wr_n_o(bus_wr),.a0_o(bus_a0),.data_o(bus_data));
 always_ff @(posedge clk_i or negedge native_rst_n)begin
  if(!native_rst_n)begin request_pending<=0;pending_tag<=0;pcm_response_valid_o<=0;pcm_response_tag_o<=0;pcm_response_left_o<=0;pcm_response_right_o<=0;end
  else begin
   pcm_response_valid_o<=0;
   if(request_pending&&(core_reset||request_done))begin
    request_pending<=0;pcm_response_valid_o<=1;pcm_response_tag_o<=pending_tag;
    pcm_response_left_o<=!core_reset&&initialized_o&&!fault_o ? {left,8'b0}:24'sd0;
    pcm_response_right_o<=!core_reset&&initialized_o&&!fault_o ? {right,8'b0}:24'sd0;
   end
   if(pcm_request_valid_i&&pcm_request_ready_o)begin
    if(core_reset)begin pcm_response_valid_o<=1;pcm_response_tag_o<=pcm_request_tag_i;pcm_response_left_o<=0;pcm_response_right_o<=0;end
    else begin request_pending<=1;pending_tag<=pcm_request_tag_i;end
   end
  end
 end
endmodule
`default_nettype wire
