// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Prepared ABI2: exact core integers, no template ROM or event arithmetic.
`default_nettype none
module ae350_voice_backend_ks #(
 parameter bit LOCAL_MIDI_BOOT=0
)(
 input wire clk,rst_n,sclk,
 input wire cmd_valid_i,output wire cmd_ready_o,output wire [1:0] cmd_result_o,input wire [159:0] cmd_data_i,
 output wire ready_o,output wire fb_valid_o,input wire fb_ready_i,
 output wire [4:0] fb_voice_o,output wire [31:0] fb_generation_o,
 input wire [15:0] voice_idle,input wire core_mode,
 input wire [31:0] applied_sequence_i,
 input wire perf_write_enable,input wire [15:0] perf_write_addr,input wire [31:0] perf_write_data,
 output logic [31:0] template_status,output wire template_available,
 output wire command_valid,input wire command_ready,
 output logic [2:0] command_op,output logic [3:0] command_voice,
 output logic [14:0] command_length,output logic signed [15:0] command_tuning,
 output logic [15:0] command_loss,command_amplitude,command_gain_left,command_gain_right,
 output logic [31:0] command_seed,output wire [31:0] command_sequence
);
 logic [31:0] generations[0:15],last_revision,stage_revision,stage_gen;
 logic [15:0] occupied,released,idle_pending;
 logic stage_valid;logic [3:0] stage_voice;logic [2:0] stage_op;
 logic [127:0] params;logic [1:0] data_mask;
 logic issuing,inflight,completed,bound_sent,bound_done;
 logic [31:0] serial,inflight_serial,inflight_gen;
 logic [3:0] inflight_voice;logic [2:0] inflight_op;
 wire [7:0] vb=cmd_data_i[15:8],op=cmd_data_i[23:16],flags=cmd_data_i[31:24];
 wire [3:0] voice=vb[3:0];wire [31:0] rev=cmd_data_i[63:32],gen=cmd_data_i[159:128];
 wire [63:0] payload=cmd_data_i[127:64];
 wire matching=gen!=0&&gen==generations[voice];
 wire stage_match=stage_valid&&stage_voice==voice&&stage_gen==gen&&stage_revision==rev;
 wire legal_params=(stage_op!=1||(params[14:0]>=2&&params[14:0]<=4096&&params[30:15]!=16'h8000&&params[46:31]<=32767&&params[62:47]<=32767))&&(stage_op!=6||params[46:31]<=32767);
 logic [1:0] result;
 always_comb begin
  result=0;
  if(cmd_data_i[7:0]!=1||vb>=16)result=2;
  else case(op)
   1,4:begin if(flags!=0||cmd_data_i[127:32]!=0||gen==0)result=2;else if(!matching)result=1;end
   8'h10:begin
    if(flags==0||flags>7||rev==0||gen==0||payload!=0||stage_valid)result=2;
    else if(rev<=last_revision||(flags==1 ? gen<=generations[voice]:!matching))result=1;
   end
   8'h11:begin if(flags>1||(flags==1&&payload[63]))result=2;else if(!stage_match)result=1;end
   8'h13:begin if(flags!=0||payload!=0)result=2;else if(!stage_match)result=1;else if(data_mask!=3||!legal_params)result=2;end
   default:result=2;
  endcase
 end
 wire needs_core=op==1||op==4||op==8'h13;
 assign ready_o=bound_done&&core_mode&&rst_n;
 assign cmd_ready_o=ready_o&&(!needs_core||result!=0||completed)&&!issuing&&!inflight;
 assign cmd_result_o=result;
 wire fire=cmd_valid_i&&cmd_ready_o;
 wire start_core=cmd_valid_i&&ready_o&&needs_core&&result==0&&!issuing&&!inflight&&!completed;
 assign template_status=0;assign template_available=ready_o;
 assign command_valid=(!bound_sent)||(issuing&&!inflight);
 assign command_sequence=serial;
 always_comb begin
  command_op=!bound_sent ? 3'd4:inflight_op;
  command_voice=inflight_voice;
  command_length=params[14:0];command_tuning=$signed(params[30:15]);command_loss=params[46:31];
  command_amplitude=params[62:47];command_gain_left=params[78:63];command_gain_right=params[94:79];command_seed=params[126:95];
 end
 integer feedback_pick;
 always_comb begin feedback_pick=0;for(integer v=15;v>=0;v--)if(idle_pending[v])feedback_pick=v;end
 logic feedback_valid;logic [4:0] feedback_voice;logic [31:0] feedback_generation;
 wire feedback_capture=(!feedback_valid||fb_ready_i)&&(|idle_pending);
 assign fb_valid_o=feedback_valid;assign fb_voice_o=feedback_voice;assign fb_generation_o=feedback_generation;
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin feedback_valid<=0;feedback_voice<=0;feedback_generation<=0;end
  else if(!feedback_valid||fb_ready_i)begin feedback_valid<=|idle_pending;if(|idle_pending)begin feedback_voice<=5'(feedback_pick);feedback_generation<=generations[feedback_pick];end end
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   last_revision<=0;stage_revision<=0;stage_gen<=0;stage_valid<=0;stage_voice<=0;stage_op<=0;params<=0;data_mask<=0;
   occupied<=0;released<=0;idle_pending<=0;issuing<=0;inflight<=0;completed<=0;bound_sent<=0;bound_done<=0;
   serial<=1;inflight_serial<=0;inflight_gen<=0;inflight_voice<=0;inflight_op<=0;
   for(integer v=0;v<16;v++)generations[v]<=0;
  end else begin
   if(feedback_capture)idle_pending[feedback_pick]<=0;
   for(integer v=0;v<16;v++)if(released[v]&&voice_idle[v])begin released[v]<=0;occupied[v]<=0;idle_pending[v]<=1;end
   if(start_core)begin
    issuing<=1;inflight_voice<=voice;inflight_gen<=gen;
    inflight_op<=op==1 ? 3'd2:op==4 ? 3'd7:stage_op;
    if(stage_valid&&stage_voice==voice&&stage_gen==gen&&(op==1||op==4))stage_valid<=0;
   end
   if(command_valid&&command_ready)begin
    inflight_serial<=serial;serial<=serial==32'hffffffff ? 32'd1:serial+1'b1;
    if(!bound_sent)begin bound_sent<=1;end
    else begin issuing<=0;inflight<=1;end
   end
   if(bound_sent&&!bound_done&&applied_sequence_i==inflight_serial&&core_mode)bound_done<=1;
   if(inflight&&applied_sequence_i==inflight_serial)begin
    inflight<=0;completed<=1;
    if(inflight_op==3||inflight_op==4)released<=occupied;
    else if((inflight_op==2||inflight_op==7)&&inflight_gen==generations[inflight_voice])released[inflight_voice]<=1;
   end
   if(fire)begin
    if(result==0)case(op)
     8'h10:begin
      stage_valid<=1;stage_voice<=voice;stage_gen<=gen;stage_op<=flags[2:0];stage_revision<=rev;last_revision<=rev;data_mask<=0;params<=0;
      if(flags==1)begin generations[voice]<=gen;occupied[voice]<=1;released[voice]<=0;idle_pending[voice]<=0;end
     end
     8'h11:begin params[flags[0]*64+:64]<=payload;data_mask[flags[0]]<=1;end
     8'h13:begin stage_valid<=0;completed<=0;end
     1,4:completed<=0;
     default:;
    endcase
   end
  end
 end
endmodule
`default_nettype wire
