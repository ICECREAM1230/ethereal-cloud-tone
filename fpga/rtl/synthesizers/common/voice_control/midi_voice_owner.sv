// Copyright (c) 2026 ICECREAM1230
// Source/channel isolated performance state; all outputs use synth clock.
// Local requests are retractable until accepted: event/flush edges always win.
// SPDX-License-Identifier: MIT
`default_nettype none
module midi_voice_owner #(
 parameter integer VOICES=8, parameter bit HAS_BEND=1, parameter bit HAS_LOSS=0,
 parameter integer AW=$clog2(VOICES)
)(
 input wire clk,rst_n,enable,clear,refresh,release_all,
 input midi_pkg::midi_event_t event_i,
 input wire event_valid, output wire event_ready,
 input wire [3:0] flush, input wire [VOICES-1:0] voice_idle,
 output logic command_valid, input wire command_ready,
 output logic [2:0] command_kind, output logic [AW-1:0] command_voice,
 output logic [6:0] command_note,command_velocity,command_volume,command_pan,
 output logic [31:0] command_pitch,command_stamp,
 output logic [VOICES-1:0] cancel_on,
 output logic [32*VOICES-1:0] voice_stamps,
 output wire hard_mute
);
 localparam [1:0] IDLE=0,HELD=1,RELEASE=2;
 // kind 0 On,1 Off,2 pitch,3 gain; each accepted Off precedes a stolen On.
 logic [1:0] state[0:VOICES-1],source[0:VOICES-1];
 logic [3:0] channel[0:VOICES-1];
 logic [6:0] notes[0:VOICES-1],velocities[0:VOICES-1];
 logic [31:0] stamps[0:VOICES-1],pitches[0:VOICES-1];
 logic [VOICES-1:0] down,actual_on,on_pending,off_pending,pitch_pending,gain_pending,kill_pending,loss_pending;
 logic sustain[0:63];
 // One write and one synchronous read per bank. Valid bits supply the
 // original clear/flush defaults without resetting the memory arrays.
 (* syn_ramstyle="block_ram" *) logic [13:0] bend_data[0:63];
 (* syn_ramstyle="block_ram" *) logic [6:0] volume_data[0:63],expression_data[0:63],pan_data[0:63];
 logic [63:0] bend_valid,volume_valid,expression_valid,pan_valid;
 logic [13:0] bend_read_data;
 logic [6:0] volume_read_data,expression_read_data,pan_read_data;
 logic bend_read_valid,volume_read_valid,expression_read_valid,pan_read_valid;
`ifndef SYNTHESIS
 // Logical views for existing cycle-lockstep checks; these are not hardware
 // read ports. Every production consumer below uses the synchronous ports.
 wire [13:0] bend[0:63];
 wire [6:0] volume[0:63],expression[0:63],pan[0:63];
 for(genvar view=0;view<64;view++)begin:g_channel_view
  assign bend[view]=bend_valid[view]?bend_data[view]:14'd8192;
  assign volume[view]=volume_valid[view]?volume_data[view]:7'd127;
  assign expression[view]=expression_valid[view]?expression_data[view]:7'd127;
  assign pan[view]=pan_valid[view]?pan_data[view]:7'd64;
 end
`endif
 logic [31:0] stamp_counter;
 logic enabled_previous;
 wire [5:0] event_channel={event_i.source[1:0],event_i.status[3:0]};
 wire incoming=event_valid&&event_ready;
 assign hard_mute=enabled_previous&&!enable;
 function automatic logic [31:0] pitch_for(input logic [6:0] n,input logic [13:0] b);
  logic signed [31:0] delta;
  delta=HAS_BEND ? ($signed({18'b0,b})-32'sd8192)*32'sd16 : 32'sd0;
  return {9'd0,n,16'd0}+delta;
 endfunction
 function automatic logic legal_pitch(input logic [31:0] p);
  return !HAS_BEND || (p>=32'h000d0000&&p<=32'h006cfc00);
 endfunction
 function automatic logic older(input logic[31:0] a,b);
  return $signed(a-b)<0;
 endfunction
 // Selection is a read-ahead transaction: upstream holds VALID/payload
 // until EV_COMMIT raises READY. In particular, a held Off keeps the FM
 // bridge's existing event barrier asserted until its exact FIFO target is
 // cancelled. Backend backpressure never prevents accepting another event.
 typedef enum logic [2:0] {EV_IDLE,EV_CHANNEL,EV_PITCH,EV_RANGE,EV_READ,EV_COMPARE,EV_COMMIT} event_state_t;
 event_state_t event_state;
 logic pending_on;
 logic [5:0] pending_channel;
 logic [6:0] pending_note,pending_velocity;
 logic [13:0] pending_bend;
 logic [31:0] prepared_pitch;
 logic prepared_legal;
 logic [AW-1:0] scan_index,best_index;
 logic best_valid,candidate_valid;
 logic [1:0] best_rank,candidate_rank;
 logic [31:0] best_stamp,candidate_stamp;
 wire input_header=event_i.source<4&&event_i.data1<128&&event_i.data2<128;
 wire needs_scan=input_header&&(event_i.status[7:4]==8||event_i.status[7:4]==9);
 assign event_ready=(!enable||clear||release_all) ? 1'b1 :
   flush!=0 ? 1'b0 : event_state==EV_COMMIT||(event_state==EV_IDLE&&!needs_scan);
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   event_state<=EV_IDLE;pending_on<=0;pending_channel<=0;pending_note<=0;pending_velocity<=0;pending_bend<=8192;
   prepared_pitch<=0;prepared_legal<=0;scan_index<=0;best_index<=0;best_valid<=0;candidate_valid<=0;
   best_rank<=3;candidate_rank<=3;best_stamp<=0;candidate_stamp<=0;
  end else if(!enable||clear||release_all)begin event_state<=EV_IDLE;best_valid<=0;end
  else if(flush!=0)begin
   if(event_state!=EV_IDLE)begin
    best_valid<=0;scan_index<=0;best_rank<=3;
    if(flush[pending_channel[5:4]])event_state<=EV_IDLE;
    else event_state<=pending_on?EV_CHANNEL:EV_READ;
   end
  end else if(event_state!=EV_IDLE&&!event_valid)begin event_state<=EV_IDLE;best_valid<=0;end
  else case(event_state)
   EV_IDLE:if(event_valid&&needs_scan)begin
    pending_on<=event_i.status[7:4]==9&&event_i.data2!=0;
    pending_channel<=event_channel;pending_note<=event_i.data1[6:0];pending_velocity<=event_i.data2[6:0];
    scan_index<=0;best_index<=0;best_valid<=0;best_rank<=3;
    event_state<=event_i.status[7:4]==9&&event_i.data2!=0?EV_CHANNEL:EV_READ;
   end
   EV_CHANNEL:begin pending_bend<=bend_read_valid?bend_read_data:14'd8192;event_state<=EV_PITCH;end
   EV_PITCH:begin prepared_pitch<=pitch_for(pending_note,pending_bend);event_state<=EV_RANGE;end
   EV_RANGE:begin prepared_legal<=legal_pitch(prepared_pitch);event_state<=legal_pitch(prepared_pitch)?EV_READ:EV_COMMIT;end
   EV_READ:begin
    candidate_stamp<=stamps[scan_index];
    candidate_rank<=pending_on?(state[scan_index]==IDLE?2'd0:state[scan_index]==RELEASE?2'd1:2'd2):2'd0;
    candidate_valid<=pending_on||(state[scan_index]==HELD&&down[scan_index]&&{source[scan_index],channel[scan_index]}==pending_channel&&notes[scan_index]==pending_note);
    event_state<=EV_COMPARE;
   end
   EV_COMPARE:begin
    if(candidate_valid&&(!best_valid||candidate_rank<best_rank||(candidate_rank==best_rank&&older(candidate_stamp,best_stamp))))begin
     best_valid<=1;best_index<=scan_index;best_rank<=candidate_rank;best_stamp<=candidate_stamp;
    end
    if(scan_index==AW'(VOICES-1))event_state<=EV_COMMIT;
    else begin scan_index<=scan_index+1'b1;event_state<=EV_READ;end
   end
   EV_COMMIT:if(incoming)event_state<=EV_IDLE;
   default:event_state<=EV_IDLE;
  endcase
 end

 // Select only a small pending mask first. A single subsequent voice read,
 // channel read, narrow multiply and exact /127 pipeline is shared by every
 // voice, rather than replicating the expression datapath in each priority arm.
 typedef enum logic [2:0] {CMD_PICK,CMD_VOICE,CMD_CHANNEL,CMD_MULT,CMD_BIAS,CMD_SUM,CMD_READY} command_state_t;
 command_state_t command_state;
 logic [VOICES-1:0] arbitration_mask;
 logic [2:0] arbitration_kind;
 logic [AW-1:0] arbitration_voice;
 logic selected_pending;
 logic [5:0] command_channel;
 logic [6:0] volume_operand,expression_operand;
 logic [13:0] expression_product;
 logic [14:0] division_t;
 // Read ahead within the existing pipeline; no event/command edge moves.
 wire bend_read=enable&&!clear&&!release_all&&flush==0&&event_state==EV_IDLE&&
   event_valid&&needs_scan&&event_i.status[7:4]==9&&event_i.data2!=0;
 wire channel_read=enable&&!clear&&!release_all&&flush==0&&!event_valid&&
   event_state==EV_IDLE&&command_state==CMD_VOICE;
 wire [5:0] channel_read_address={source[command_voice],channel[command_voice]};
 wire channel_update=rst_n&&enable&&!clear&&!release_all&&incoming&&input_header;
 always_ff @(posedge clk)begin
  if(bend_read)bend_read_data<=bend_data[event_channel];
  if(channel_read)begin
   volume_read_data<=volume_data[channel_read_address];
   expression_read_data<=expression_data[channel_read_address];
   pan_read_data<=pan_data[channel_read_address];
  end
  if(channel_update)begin
   if(event_i.status[7:4]==11)case(event_i.data1)
    7:volume_data[event_channel]<=event_i.data2[6:0];
    10:pan_data[event_channel]<=event_i.data2[6:0];
    11:expression_data[event_channel]<=event_i.data2[6:0];
    default:;
   endcase
   else if(HAS_BEND&&event_i.status[7:4]==14)
    bend_data[event_channel]<={event_i.data2[6:0],event_i.data1[6:0]};
  end
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   bend_read_valid<=0;volume_read_valid<=0;expression_read_valid<=0;pan_read_valid<=0;
  end else begin
   if(bend_read)bend_read_valid<=bend_valid[event_channel];
   if(channel_read)begin
    volume_read_valid<=volume_valid[channel_read_address];
    expression_read_valid<=expression_valid[channel_read_address];
    pan_read_valid<=pan_valid[channel_read_address];
   end
  end
 end
`ifndef SYNTHESIS
 always @(posedge clk)if(rst_n)begin
  assert(!(channel_update&&channel_read))else $fatal(1,"owner channel RAM port overlap");
  assert(!(bend_read&&channel_update&&HAS_BEND&&event_i.status[7:4]==14))
    else $fatal(1,"owner bend RAM port overlap");
 end
`endif
 always_comb begin
  arbitration_mask='0;arbitration_kind=0;arbitration_voice='0;
  if(|kill_pending)begin arbitration_mask=kill_pending;arbitration_kind=4;end
  else if(|off_pending)begin arbitration_mask=off_pending;arbitration_kind=1;end
  else if(|on_pending)begin arbitration_mask=on_pending;arbitration_kind=0;end
  else if(|pitch_pending)begin arbitration_mask=pitch_pending;arbitration_kind=2;end
  else if(|gain_pending)begin arbitration_mask=gain_pending;arbitration_kind=3;end
  else if(|loss_pending)begin arbitration_mask=loss_pending;arbitration_kind=5;end
  for(integer v=VOICES-1;v>=0;v=v-1)if(arbitration_mask[v])arbitration_voice=AW'(v);
  selected_pending=0;
  case(command_kind)
   0:selected_pending=on_pending[command_voice]&&!off_pending[command_voice];
   1:selected_pending=off_pending[command_voice];
   2:selected_pending=pitch_pending[command_voice]&&!on_pending[command_voice];
   3:selected_pending=gain_pending[command_voice]&&!on_pending[command_voice];
   4:selected_pending=kill_pending[command_voice];
   5:selected_pending=loss_pending[command_voice]&&!on_pending[command_voice];
   default:;
  endcase
  command_valid=command_state==CMD_READY&&selected_pending&&enable&&!clear&&!release_all&&
    event_state==EV_IDLE&&!event_valid&&flush==0;
  for(integer v=0;v<VOICES;v=v+1)begin
   cancel_on[v]=!enable||clear||release_all||state[v]!=HELD||off_pending[v];
   voice_stamps[v*32+:32]=stamps[v];
  end
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   command_state<=CMD_PICK;command_kind<=0;command_voice<=0;command_note<=0;command_velocity<=0;
   command_volume<=127;command_pan<=64;command_pitch<=0;command_stamp<=0;command_channel<=0;
   volume_operand<=127;expression_operand<=127;expression_product<=0;division_t<=0;
  end else if(!enable||clear||release_all||event_valid||event_state!=EV_IDLE||flush!=0)command_state<=CMD_PICK;
  else case(command_state)
   CMD_PICK:if(|arbitration_mask)begin command_kind<=arbitration_kind;command_voice<=arbitration_voice;command_state<=CMD_VOICE;end
   CMD_VOICE:begin
    command_note<=notes[command_voice];command_velocity<=velocities[command_voice];command_pitch<=pitches[command_voice];command_stamp<=stamps[command_voice];
    command_channel<={source[command_voice],channel[command_voice]};command_state<=CMD_CHANNEL;
   end
   CMD_CHANNEL:begin
    volume_operand<=volume_read_valid?volume_read_data:7'd127;
    expression_operand<=expression_read_valid?expression_read_data:7'd127;
    command_pan<=pan_read_valid?pan_read_data:7'd64;command_state<=CMD_MULT;
   end
   CMD_MULT:begin expression_product<=volume_operand*expression_operand;command_state<=CMD_BIAS;end
   CMD_BIAS:begin division_t<={1'b0,expression_product}+15'd1;command_state<=CMD_SUM;end
   CMD_SUM:begin
    // Exact floor(x/127), x=CC7*CC11 in 0..16129; independently exhausted by the runner.
    command_volume<=command_kind==4?7'd0:7'((division_t+(division_t>>7))>>7);command_state<=CMD_READY;
   end
   CMD_READY:if(!selected_pending||command_ready)command_state<=CMD_PICK;
   default:command_state<=CMD_PICK;
  endcase
 end
 // Constant banks keep channel writes out of the voice/command update cone.
 // RELEASE_ALL clears pedals only; it also suppresses a simultaneous flush
 // or controller write, preserving the other channel values on that edge.
 for(genvar c=0;c<64;c=c+1)begin:g_channel_bank
  always_ff @(posedge clk or negedge rst_n)begin
   if(!rst_n)begin
    sustain[c]<=0;bend_valid[c]<=0;volume_valid[c]<=0;expression_valid[c]<=0;pan_valid[c]<=0;
   end else if(!enable||clear)begin
    sustain[c]<=0;bend_valid[c]<=0;volume_valid[c]<=0;expression_valid[c]<=0;pan_valid[c]<=0;
   end else if(release_all)sustain[c]<=0;
   else if(flush[c/16])begin
    sustain[c]<=0;bend_valid[c]<=0;volume_valid[c]<=0;expression_valid[c]<=0;pan_valid[c]<=0;
   end else if(incoming&&input_header&&event_channel==6'(c))begin
    if(event_i.status[7:4]==11)begin
     case(event_i.data1)
      64:sustain[c]<=event_i.data2>=64;
      120,123:sustain[c]<=0;
      7:volume_valid[c]<=1;
      10:pan_valid[c]<=1;
      11:expression_valid[c]<=1;
      default:;
     endcase
    end else if(HAS_BEND&&event_i.status[7:4]==14)
     bend_valid[c]<=1;
   end
  end
 end
 // Keep each physical voice's writes local. A variable-index write first
 // formed a shared command-accept mux, then decoded the voice again at every
 // D/CE pin. The late backend READY now qualifies one constant voice only.
 // Event updates still follow command/refresh updates and source flush is last.
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin enabled_previous<=0;stamp_counter<=0;end
  else begin
   enabled_previous<=enable;
   if(enable&&!clear&&!release_all&&incoming&&input_header&&!flush[event_i.source[1:0]]&&
      event_i.status[7:4]==9&&event_i.data2!=0&&event_state==EV_COMMIT&&best_valid&&prepared_legal)
    stamp_counter<=stamp_counter+1'b1;
  end
 end
 for(genvar v=0;v<VOICES;v=v+1)begin:g_voice_bank
  // ENABLE/CLEAR/RELEASE_ALL are the higher-priority branches below, so do
  // not route those global cancellation conditions through command acceptance
  // and back through another pending read before applying them locally.
  wire command_target=command_state==CMD_READY&&command_ready&&command_voice==AW'(v)&&
    event_state==EV_IDLE&&!event_valid&&flush==0;
  always_ff @(posedge clk or negedge rst_n)begin
   if(!rst_n)begin
    down[v]<=0;actual_on[v]<=0;on_pending[v]<=0;off_pending[v]<=0;pitch_pending[v]<=0;gain_pending[v]<=0;kill_pending[v]<=0;loss_pending[v]<=0;
    state[v]<=IDLE;source[v]<=0;channel[v]<=0;notes[v]<=0;velocities[v]<=0;stamps[v]<=0;pitches[v]<=0;
   end else if(!enable||clear)begin
    down[v]<=0;actual_on[v]<=0;on_pending[v]<=0;off_pending[v]<=0;pitch_pending[v]<=0;gain_pending[v]<=0;kill_pending[v]<=0;loss_pending[v]<=0;state[v]<=IDLE;
   end else if(release_all)begin
    // The shared FM backend executes RELEASE_ALL. Retain release ownership
    // for tail reuse/expression, but cancel every key, pedal and queued On.
    down[v]<=0;actual_on[v]<=0;on_pending[v]<=0;off_pending[v]<=0;
    pitch_pending[v]<=0;gain_pending[v]<=0;kill_pending[v]<=0;loss_pending[v]<=0;
    if(state[v]!=IDLE)state[v]<=RELEASE;
   end else begin
    if(event_state==EV_IDLE&&voice_idle[v]&&state[v]==RELEASE&&!on_pending[v]&&!off_pending[v])begin state[v]<=IDLE;gain_pending[v]<=0;pitch_pending[v]<=0;loss_pending[v]<=0;end
    if(command_target)begin
     case(command_kind)
      0:if(on_pending[v]&&!off_pending[v])begin on_pending[v]<=0;actual_on[v]<=1;end
      1:if(off_pending[v])begin off_pending[v]<=0;actual_on[v]<=0;end
      2:if(pitch_pending[v]&&!on_pending[v])pitch_pending[v]<=0;
      3:if(gain_pending[v]&&!on_pending[v])gain_pending[v]<=0;
      4:if(kill_pending[v])begin kill_pending[v]<=0;state[v]<=IDLE;gain_pending[v]<=0;pitch_pending[v]<=0;loss_pending[v]<=0;end
      5:if(loss_pending[v]&&!on_pending[v])loss_pending[v]<=0;
      default:;
     endcase
    end
    if(refresh&&state[v]!=IDLE&&!kill_pending[v])begin gain_pending[v]<=1;if(HAS_LOSS)loss_pending[v]<=1;end
    if(incoming&&input_header&&!flush[event_i.source[1:0]])begin
     if(event_i.status[7:4]==9&&event_i.data2!=0)begin
      if(event_state==EV_COMMIT&&best_valid&&prepared_legal&&best_index==AW'(v))begin
       stamps[v]<=stamp_counter+1'b1;
       state[v]<=HELD;source[v]<=pending_channel[5:4];channel[v]<=pending_channel[3:0];
       notes[v]<=pending_note;velocities[v]<=pending_velocity;down[v]<=1;
       pitches[v]<=prepared_pitch;
       on_pending[v]<=1;off_pending[v]<=actual_on[v];pitch_pending[v]<=0;
       gain_pending[v]<=0;loss_pending[v]<=0;kill_pending[v]<=0;
      end
     end else if(event_i.status[7:4]==8||(event_i.status[7:4]==9&&event_i.data2==0))begin
      if(event_state==EV_COMMIT&&best_valid&&best_index==AW'(v))begin
       down[v]<=0;
       if(!sustain[pending_channel])begin state[v]<=RELEASE;on_pending[v]<=0;off_pending[v]<=actual_on[v];pitch_pending[v]<=0;gain_pending[v]<=0;end
      end
     end else if(event_i.status[7:4]==11)begin
      case(event_i.data1)
       64:begin
        if(event_i.data2<64&&state[v]==HELD&&!down[v]&&{source[v],channel[v]}==event_channel)begin
         state[v]<=RELEASE;on_pending[v]<=0;off_pending[v]<=actual_on[v];pitch_pending[v]<=0;gain_pending[v]<=0;
        end
       end
       120,123:begin
        if(state[v]!=IDLE&&{source[v],channel[v]}==event_channel)begin
         down[v]<=0;state[v]<=RELEASE;on_pending[v]<=0;off_pending[v]<=actual_on[v];pitch_pending[v]<=0;gain_pending[v]<=0;
         if(event_i.data1==120)kill_pending[v]<=actual_on[v];
        end
       end
       7,10,11:if(state[v]!=IDLE&&{source[v],channel[v]}==event_channel)gain_pending[v]<=1;
       default:;
      endcase
     end else if(HAS_BEND&&event_i.status[7:4]==14)begin
      if(state[v]!=IDLE&&{source[v],channel[v]}==event_channel&&legal_pitch(pitch_for(notes[v],{event_i.data2[6:0],event_i.data1[6:0]})))begin
       pitches[v]<=pitch_for(notes[v],{event_i.data2[6:0],event_i.data1[6:0]});pitch_pending[v]<=1;
      end
     end
    end
    // Flush also wins every simultaneous event for voice state.
    if(flush[source[v]])begin
     state[v]<=RELEASE;down[v]<=0;on_pending[v]<=0;off_pending[v]<=actual_on[v];pitch_pending[v]<=0;gain_pending[v]<=0;
    end
   end
  end
 end
endmodule
`default_nettype wire
