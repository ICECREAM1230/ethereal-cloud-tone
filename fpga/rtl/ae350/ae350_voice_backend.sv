// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
`default_nettype none
// Prepared-command routing only. Native sinks own the sole generation check.
module ae350_voice_backend(
 input wire clk_i,rst_ni,
 input wire cmd_valid_i,output wire cmd_ready_o,input wire[159:0] cmd_data_i,input wire[31:0] cmd_sequence_i,
 output wire[31:0] cmd_result_o,
 input wire[2:0] native_ready_i,
 output wire[2:0] ready_o,output wire[31:0] fault_o,
 output wire[2:0] lane_valid_o,input wire[2:0] lane_ready_i,input wire[5:0] lane_result_i,
 output wire[159:0] lane_data_o,output wire[31:0] lane_sequence_o,
 input wire[2:0] lane_fb_valid_i,output reg[2:0] lane_fb_ready_o,
 input wire[14:0] lane_fb_voice_i,input wire[95:0] lane_fb_generation_i,
 output wire fb_valid_o,input wire fb_ready_i,output wire[1:0] fb_engine_o,output wire[4:0] fb_voice_o,output wire[31:0] fb_generation_o
);
 wire[7:0] engine=cmd_data_i[7:0],voice=cmd_data_i[15:8],opcode=cmd_data_i[23:16];
 wire[31:0] generation=cmd_data_i[159:128];
 wire global_command=opcode==8'h15;
 wire opcode_legal=opcode==1||opcode==4||opcode==8'h10||opcode==8'h11||opcode==8'h12||opcode==8'h13||global_command;
 wire voice_legal=global_command ? voice==0&&generation==0 :
   generation!=0&&((engine==0&&voice<32)||(engine==1&&voice<16)||(engine==2&&voice<32));
 wire legal=engine<=2&&opcode_legal&&voice_legal&&cmd_sequence_i!=0;
 reg selected_ready;reg[1:0] selected_result;
 always @*begin
  selected_ready=0;selected_result=2;
  case(engine)
   0:begin selected_ready=lane_ready_i[0]&&(native_ready_i[0]||global_command);selected_result=lane_result_i[1:0];end
   1:begin selected_ready=lane_ready_i[1]&&(native_ready_i[1]||global_command);selected_result=lane_result_i[3:2];end
   2:begin selected_ready=lane_ready_i[2]&&(native_ready_i[2]||global_command);selected_result=lane_result_i[5:4];end
   default:;
  endcase
 end
 assign cmd_result_o=legal?{30'd0,selected_result}:32'd2;
 assign ready_o=native_ready_i&{3{rst_ni}};
 assign fault_o=32'd0;
 assign cmd_ready_o=rst_ni&&(!legal||selected_ready);
 assign lane_data_o=cmd_data_i;assign lane_sequence_o=cmd_sequence_i;
 for(genvar e=0;e<3;e=e+1)begin:g_lane
  assign lane_valid_o[e]=rst_ni&&cmd_valid_i&&legal&&engine==8'(e)&&(native_ready_i[e]||global_command);
 end
 // Static three-way priority avoids a wide arithmetic modulo network.
 reg[1:0] next_engine,grant,pick;reg grant_valid,found;
 always @*begin
  found=|lane_fb_valid_i;pick=0;
  case(next_engine)
   0:if(lane_fb_valid_i[0])pick=0;else if(lane_fb_valid_i[1])pick=1;else pick=2;
   1:if(lane_fb_valid_i[1])pick=1;else if(lane_fb_valid_i[2])pick=2;else pick=0;
   default:if(lane_fb_valid_i[2])pick=2;else if(lane_fb_valid_i[0])pick=0;else pick=1;
  endcase
  lane_fb_ready_o=0;
  if(grant_valid)lane_fb_ready_o[grant]=fb_ready_i;
 end
 assign fb_valid_o=grant_valid&&lane_fb_valid_i[grant];
 assign fb_engine_o=grant;
 assign fb_voice_o=lane_fb_voice_i[grant*5+:5];
 assign fb_generation_o=lane_fb_generation_i[grant*32+:32];
 always @(posedge clk_i or negedge rst_ni)begin
  if(!rst_ni)begin grant_valid<=0;grant<=0;next_engine<=0;end
  else begin
   if(!grant_valid&&found)begin grant<=pick;grant_valid<=1;end
   if(fb_valid_o&&fb_ready_i)begin grant_valid<=0;next_engine<=grant==2?2'd0:grant+1'b1;end
  end
 end
endmodule
`default_nettype wire
