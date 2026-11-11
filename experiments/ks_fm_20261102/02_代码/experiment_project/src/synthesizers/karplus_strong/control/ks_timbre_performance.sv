// Copyright (c) 2026 ICECREAM1230
// KS-specific template and delivered-note adapter. Generic ownership is unchanged.
// SPDX-License-Identifier: MIT
`default_nettype none
module ks_timbre_performance #(
 parameter bit LOCAL_MIDI_BOOT=0
)(
 input wire clk,rst_n,sclk,enable,clear,
 input midi_pkg::midi_event_t event_i,input wire event_valid,output wire event_ready,
 input wire [3:0] flush,input wire [15:0] voice_idle,
 input wire perf_write_enable,input wire [15:0] perf_write_addr,input wire [31:0] perf_write_data,
 output wire [31:0] template_status,output wire template_available,
 output wire command_valid,input wire command_ready,
 output logic [2:0] command_op,output logic [3:0] command_voice,
 output logic [14:0] command_length,output logic signed [15:0] command_tuning,
 output logic [15:0] command_loss,output wire [15:0] command_amplitude,command_gain_left,command_gain_right,
 output logic [31:0] command_seed,output wire hard_mute,
 output wire [31:0] command_model_generation,command_template_generation,command_note_identity,
 output wire bank_busy,cancel_panic,
 output wire [31:0] image_crc,request_generation,requested_template_generation,completed_template,result,
 output wire model_valid,input wire model_ready,output wire [2047:0] model_words,
 input wire [31:0] model_completed_generation,model_result,model_applied_generation,
 input wire [1:0] model_active_id
);
 typedef enum logic[3:0]{IDLE,R0,W0,R1,W1,RG,WG,SCALE_START,SCALE_WAIT,EMIT} fetch_t;
 fetch_t state;
 logic [8:0] read_index;
 wire [31:0] ram_word,boot_word;
 wire [127:0] valid_notes;
 wire owner_clear,refresh;
 ks_timbre_bank #(.LOCAL_MIDI_BOOT(LOCAL_MIDI_BOOT)) templates(
  .clk(clk),.rst_n(rst_n),.sclk(sclk),.clear(clear||hard_mute),.flush(flush),
  .fetch_busy(state!=IDLE),.read_index_i(read_index),.read_data_o(ram_word),
  .perf_write_enable(perf_write_enable),.perf_write_addr(perf_write_addr),.perf_write_data(perf_write_data),
  .template_status(template_status),.template_available(template_available),.busy_o(bank_busy),
  .owner_clear_o(owner_clear),.refresh_o(refresh),.cancel_panic_o(cancel_panic),.valid_notes_o(valid_notes),
  .image_crc_o(image_crc),.request_generation_o(request_generation),.requested_template_generation_o(requested_template_generation),
  .completed_template_o(completed_template),.result_o(result),.model_valid_o(model_valid),.model_ready_i(model_ready),
  .model_words_o(model_words),.model_completed_generation_i(model_completed_generation),.model_result_i(model_result),
  .model_applied_generation_i(model_applied_generation),.model_active_id_i(model_active_id));
 generate if(LOCAL_MIDI_BOOT)begin:g_boot
  ks_boot_template boot(.clk(clk),.address_i(read_index),.word_o(boot_word));
 end else begin:g_no_boot assign boot_word=0;end endgenerate
 wire note_message=event_i.status[7:4]==4'h8||event_i.status[7:4]==4'h9;
 wire ignore_note=note_message&&(event_i.data1[7]||!valid_notes[event_i.data1[6:0]]);
 wire owner_event_ready,owner_valid;logic owner_ready;
 wire [2:0] kind;wire [3:0] voice;wire [6:0] note,velocity,volume,pan;
 wire [31:0] pitch,stamp;wire [15:0] cancel_on;wire [511:0] voice_stamps;
 assign event_ready=(!enable||clear||!template_available)?1'b1:bank_busy?1'b0:ignore_note?1'b1:owner_event_ready;
 // Busy is backpressure only. Dropping owner ENABLE would erase release/held
 // ownership and generate a hard_mute that cancels the in-flight model switch.
 midi_voice_owner #(.VOICES(16),.HAS_BEND(0),.HAS_LOSS(1)) owner(
  .clk(clk),.rst_n(rst_n),.enable(enable),.clear(clear||owner_clear),.release_all(1'b0),.refresh(refresh),
  .event_i(event_i),.event_valid(event_valid&&!ignore_note&&!bank_busy&&template_available),.event_ready(owner_event_ready),
  .flush(flush),.voice_idle(voice_idle),.command_valid(owner_valid),.command_ready(owner_ready),
  .command_kind(kind),.command_voice(voice),.command_note(note),.command_velocity(velocity),
  .command_volume(volume),.command_pan(pan),.command_pitch(pitch),.command_stamp(stamp),
  .cancel_on(cancel_on),.voice_stamps(voice_stamps),.hard_mute(hard_mute));
 logic [31:0] delivered_identity[0:15];
 logic [2:0] captured_kind;logic [3:0] captured_voice;logic [6:0] captured_note,captured_velocity,captured_volume,captured_pan;
 logic [31:0] captured_stamp,captured_identity,captured_model,captured_template,word0,word1,gains;
 logic captured_boot;
 wire [31:0] fetched_word=captured_boot?boot_word:ram_word;
 wire same_identity=kind==captured_kind&&voice==captured_voice&&stamp==captured_stamp&&volume==captured_volume&&pan==captured_pan;
 wire same_request=owner_valid&&same_identity&&captured_model==model_applied_generation&&captured_template==template_status;
 wire legal_note=word0[31]&&word0[30:15]==0&&word0[14:0]>=2&&word0[14:0]<=4096;
 wire valid_target=captured_kind==0||captured_identity!=0||captured_model==0;
 wire scale_done;
 midi_ks_gain_scale scaler(.clk(clk),.rst_n(rst_n),.cancel(!enable||clear||owner_clear||bank_busy),.start(state==SCALE_START),
  .gain_left(gains[15:0]),.gain_right(gains[31:16]),.volume(captured_volume),.velocity(captured_velocity),
  .done(scale_done),.scaled_left(command_gain_left),.scaled_right(command_gain_right),.amplitude(command_amplitude));
 assign command_valid=enable&&!clear&&!owner_clear&&!bank_busy&&template_available&&state==EMIT&&same_request&&valid_target&&(captured_kind!=0||legal_note);
 assign command_model_generation=captured_model;
 assign command_template_generation=captured_template;
 assign command_note_identity=captured_identity;
 always_comb begin
  read_index={1'b0,captured_note,1'b0};
  if(state==R1||state==W1)read_index={1'b0,captured_note,1'b1};
  if(state==RG||state==WG)read_index={2'b10,captured_pan};
  owner_ready=enable&&!clear&&!owner_clear&&!bank_busy&&template_available&&state==EMIT&&same_request&&
    ((!valid_target)||(captured_kind==0&&!legal_note)||command_ready);
  command_op=captured_kind==0?3'd1:captured_kind==1?3'd2:captured_kind==5?3'd6:3'd5;
  command_voice=captured_voice;command_length=word0[14:0];command_tuning=$signed(word1[15:0]);command_loss=word1[31:16];
  command_seed=captured_stamp^32'h9e3779b9^{9'd0,captured_note,16'd0};if(command_seed==0)command_seed=1;
 end
 integer v;
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   state<=IDLE;captured_kind<=0;captured_voice<=0;captured_note<=0;captured_velocity<=0;captured_volume<=0;captured_pan<=0;
   captured_stamp<=0;captured_identity<=0;captured_model<=0;captured_template<=0;captured_boot<=0;word0<=0;word1<=0;gains<=0;
   for(v=0;v<16;v=v+1)delivered_identity[v]<=0;
  end else begin
   if(!enable||clear||owner_clear)begin
    state<=IDLE;for(v=0;v<16;v=v+1)delivered_identity[v]<=0;
   end else if(bank_busy||!template_available)state<=IDLE;
   else case(state)
    IDLE:if(owner_valid)begin
     captured_kind<=kind;captured_voice<=voice;captured_note<=note;captured_velocity<=velocity;captured_volume<=volume;captured_pan<=pan;
     captured_stamp<=stamp;captured_boot<=LOCAL_MIDI_BOOT&&template_status==0;
     captured_model<=model_applied_generation;captured_template<=template_status;
     // A stolen voice already has the NEW owner stamp while its old OFF is
     // being fetched. Preserve the actually delivered identity independently.
     captured_identity<=kind==0?(stamp==0?32'd1:stamp):delivered_identity[voice];state<=R0;
    end
    R0:state<=W0;
    W0:begin word0<=fetched_word;state<=R1;end
    R1:state<=W1;
    W1:begin word1<=fetched_word;state<=RG;end
    RG:state<=WG;
    WG:begin gains<=fetched_word;state<=SCALE_START;end
    SCALE_START:state<=SCALE_WAIT;
    SCALE_WAIT:if(scale_done)state<=EMIT;
    EMIT:begin
     if(command_valid&&command_ready&&captured_kind==0)delivered_identity[captured_voice]<=captured_identity;
     // OFF starts a release; keep its identity for release-tail GAIN/LOSS until
     // the next delivered ON or a safe clear. No future owner stamp is copied.
     if(!same_request||owner_ready)state<=IDLE;
    end
    default:state<=IDLE;
   endcase
  end
 end
endmodule
`default_nettype wire
