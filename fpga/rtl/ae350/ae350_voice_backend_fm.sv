// Copyright (c) 2026 ICECREAM1230
// SPDX-License-Identifier: MIT
// Prepared ABI2: bounded native-register writer and voice lifecycle only.
`default_nettype none
module ae350_voice_backend_fm #(parameter bit ENABLE_AE350_FDC=1'b0, parameter integer FDC_WATCHDOG_CYCLES=10_000_000)(
 input wire clk,rst_n,
 input wire cmd_valid_i,output wire cmd_ready_o,output wire [1:0] cmd_result_o,input wire [159:0] cmd_data_i,
 output wire ready_o,output wire initialized_o,
 output wire fb_valid_o,input wire fb_ready_i,output wire [4:0] fb_voice_o,output wire [31:0] fb_generation_o,
 output wire register_valid_o,input wire register_ready_i,register_done_i,register_fault_i,
 output wire [1:0] register_bank_o,
 output wire [7:0] register_address_o,register_data_o,
 input wire sample_valid_i,core_ready_i,input wire [31:0] voice_idle_i,
 output logic core_reset_o,output wire [31:0] mute_mask_o,output logic fault_o
);
 logic [31:0] generations[0:31],last_revision,stage_revision,stage_gen;
 logic [31:0] occupied,released,idle_pending;
 logic stage_valid,stage_written,initialized;
 logic [4:0] stage_voice;
 logic stage_new,stage_sensor,stage_silent;
 logic [31:0] base_mute;
 wire [31:0] sensor_owned;wire sensor_alive;
 typedef enum logic [2:0] {IDLE,WRITE,WAIT_WRITE,RESETTING,WARMUP,DONE} state_t;
 state_t state;
 logic [95:0] pairs;
 logic [2:0] pair_count,pair_index;
 logic [8:0] reset_count;
 logic [3:0] warm_count;
 logic [1:0] saved_result;
 logic [7:0] saved_op;logic [4:0] saved_voice;
 logic [31:0] saved_gen;
 logic [1:0] saved_global_bank;
 logic global_finish;
 wire [7:0] vb=cmd_data_i[15:8],op=cmd_data_i[23:16],flags=cmd_data_i[31:24];
 wire [4:0] voice=vb[4:0];wire [31:0] gen=cmd_data_i[159:128],rev=cmd_data_i[63:32];
 wire matching=gen!=0&&gen==generations[voice];
 wire stage_match=stage_valid&&stage_voice==voice&&stage_gen==gen&&stage_revision==rev;
 wire [95:0] incoming_pairs=op==8'h15 ? cmd_data_i[127:32]:{32'd0,cmd_data_i[127:64]};
 wire [2:0] incoming_count=op==8'h15 ? flags[4:2]:flags[2:0];
 logic bad_pairs;
 always_comb begin
  bad_pairs=0;
  for(integer i=0;i<6;i++)begin
   if(i<int'(incoming_count))begin
    if(op!=8'h15)begin
     if(incoming_pairs[i*16+:8]==8'h08)begin
      if(incoming_pairs[i*16+8+:3]!=voice[2:0])bad_pairs=1;
     end else if(incoming_pairs[i*16+:8]<8'h20||incoming_pairs[i*16+:3]!=voice[2:0])bad_pairs=1;
    end
   end else if(incoming_pairs[i*16+:16]!=0)bad_pairs=1;
  end
 end
 logic [1:0] result;
 generate if(ENABLE_AE350_FDC)begin:g_fdc_watchdog
  localparam integer W=$clog2(FDC_WATCHDOG_CYCLES+1);
  logic [W-1:0] remaining;
  logic [31:0] owned;
  wire accepted=state==IDLE&&cmd_valid_i&&ready_o&&result==0;
  assign sensor_owned=owned;assign sensor_alive=remaining!=0;
  always_ff @(posedge clk or negedge rst_n)begin
   if(!rst_n)begin remaining<=0;owned<=0;end
   else begin
    if(remaining!=0)remaining<=remaining-1'b1;
    if(accepted&&op==8'h10)begin
     if(!flags[0])owned[voice]<=flags[2];
     if(flags[2]&&flags[3])remaining<=W'(FDC_WATCHDOG_CYCLES);
    end
    if(accepted&&op==8'h15&&flags[1:0]==0)begin remaining<=0;owned<=0;end
   end
  end
 end else begin:g_no_fdc_watchdog
  assign sensor_owned=0;assign sensor_alive=0;
 end endgenerate
 assign mute_mask_o=base_mute|(sensor_alive?32'd0:sensor_owned);

 always_comb begin
  result=0;
  if(cmd_data_i[7:0]!=2||vb>=32)result=2;
  else case(op)
   1,4:begin if(flags!=0||cmd_data_i[127:32]!=0||gen==0)result=2;else if(!matching)result=1;end
   8'h10:begin
    if((ENABLE_AE350_FDC ? ((flags&8'he2)!=0 || (!flags[2]&&(|flags[4:3]))) : flags>1)||gen==0||rev==0||cmd_data_i[127:64]!=0||stage_valid||!initialized)result=2;
    else if(rev<=last_revision||(!flags[0] ? gen<=generations[voice]:!matching))result=1;
   end
   8'h11:begin
    if(flags<1||flags>4||bad_pairs)result=2;else if(!stage_match)result=1;
   end
   8'h13:begin
    if(flags!=0||cmd_data_i[127:64]!=0)result=2;
    else if(!stage_match)result=1;else if(!stage_written)result=2;
   end
   8'h15:begin
    if(vb!=0||gen!=0||flags[7])result=2;
    else if(flags[1:0]==1)begin if(incoming_count<1||incoming_count>6||bad_pairs||core_reset_o)result=2;end
    else if((flags[1:0]!=0&&flags[1:0]!=2)||flags[6:2]!=0||cmd_data_i[127:32]!=0)result=2;
    else if(flags[1:0]==2&&core_reset_o)result=2;
   end
   default:result=2;
  endcase
 end
 assign ready_o=rst_n&&!fault_o;
 assign initialized_o=initialized;
 assign cmd_ready_o=state==DONE;
 assign cmd_result_o=saved_result;
 assign register_valid_o=state==WRITE;
 assign register_bank_o=saved_op==8'h15 ? saved_global_bank:saved_voice[4:3];
 assign register_address_o=pairs[pair_index*16+:8];
 assign register_data_o=pairs[pair_index*16+8+:8];
 integer feedback_pick;
 always_comb begin feedback_pick=0;for(integer v=31;v>=0;v--)if(idle_pending[v])feedback_pick=v;end
 logic feedback_valid;logic [4:0] feedback_voice;logic [31:0] feedback_generation;
 wire feedback_capture=(!feedback_valid||fb_ready_i)&&(|idle_pending);
 assign fb_valid_o=feedback_valid;assign fb_voice_o=feedback_voice;assign fb_generation_o=feedback_generation;
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin feedback_valid<=0;feedback_voice<=0;feedback_generation<=0;end
  else if(!feedback_valid||fb_ready_i)begin feedback_valid<=|idle_pending;if(|idle_pending)begin feedback_voice<=5'(feedback_pick);feedback_generation<=generations[feedback_pick];end end
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   last_revision<=0;stage_revision<=0;stage_gen<=0;stage_valid<=0;stage_written<=0;stage_voice<=0;stage_new<=0;stage_sensor<=0;stage_silent<=0;
   occupied<=0;released<=0;idle_pending<=0;initialized<=0;state<=IDLE;pairs<=0;pair_count<=0;pair_index<=0;
   reset_count<=0;warm_count<=0;saved_result<=0;saved_op<=0;saved_voice<=0;saved_global_bank<=0;saved_gen<=0;global_finish<=0;
   core_reset_o<=1;base_mute<=32'hffffffff;fault_o<=0;
   for(integer v=0;v<32;v++)generations[v]<=0;
  end else begin
   if(feedback_capture)idle_pending[feedback_pick]<=0;
   for(integer v=0;v<32;v++)if(released[v]&&voice_idle_i[v])begin released[v]<=0;occupied[v]<=0;idle_pending[v]<=1;end
   if(register_fault_i)begin fault_o<=1;base_mute<=32'hffffffff;initialized<=0;end
   case(state)
    IDLE:if(cmd_valid_i&&ready_o)begin
     saved_result<=result;saved_op<=op;saved_voice<=voice;saved_global_bank<=flags[6:5];saved_gen<=gen;state<=DONE;
     if(result==0)case(op)
      8'h10:begin
       last_revision<=rev;stage_revision<=rev;stage_gen<=gen;stage_voice<=voice;stage_new<=!flags[0];stage_sensor<=ENABLE_AE350_FDC&&flags[2];stage_silent<=ENABLE_AE350_FDC&&flags[4];stage_valid<=1;stage_written<=0;
       if(!flags[0])begin generations[voice]<=gen;occupied[voice]<=1;released[voice]<=0;idle_pending[voice]<=0;base_mute[voice]<=1;end
      end
      8'h11:begin pairs<=incoming_pairs;pair_count<=incoming_count;pair_index<=0;state<=WRITE;end
      8'h13:begin
       stage_valid<=0;
       if(stage_new)begin warm_count<=2;global_finish<=0;state<=WARMUP;end
       else if(stage_sensor)base_mute[voice]<=stage_silent;
      end
      1,4:begin
       if(stage_valid&&stage_voice==voice&&stage_gen==gen)begin stage_valid<=0;base_mute[voice]<=1;end
       if(op==4)base_mute[voice]<=1;
       pairs<={80'd0,5'd0,voice[2:0],8'h08};pair_count<=1;pair_index<=0;state<=WRITE;
      end
      8'h15:case(flags[1:0])
       0:begin core_reset_o<=1;reset_count<=511;initialized<=0;base_mute<=32'hffffffff;released<=occupied;stage_valid<=0;state<=RESETTING;end
       1:begin pairs<=incoming_pairs;pair_count<=incoming_count;pair_index<=0;state<=WRITE;end
       2:begin warm_count<=8;global_finish<=1;state<=WARMUP;end
       default:;
      endcase
      default:;
     endcase
    end
    WRITE:if(register_ready_i)state<=WAIT_WRITE;
    WAIT_WRITE:if(register_done_i)begin
     if(pair_index+1==pair_count)begin
      state<=DONE;
      if(saved_op==8'h11)stage_written<=1;
      if((saved_op==1||saved_op==4)&&saved_gen==generations[saved_voice])released[saved_voice]<=1;
     end else begin pair_index<=pair_index+1'b1;state<=WRITE;end
    end
    RESETTING:if(reset_count==0)begin core_reset_o<=0;state<=DONE;end else reset_count<=reset_count-1'b1;
    WARMUP:if(sample_valid_i&&core_ready_i)begin
     if(warm_count==1)begin
      if(global_finish)initialized<=1;else if(saved_gen==generations[saved_voice])base_mute[saved_voice]<=stage_sensor&&stage_silent;
      state<=DONE;
     end else warm_count<=warm_count-1'b1;
    end
    DONE:if(cmd_valid_i&&cmd_ready_o)state<=IDLE;
    default:state<=IDLE;
   endcase
  end
 end
endmodule
`default_nettype wire
