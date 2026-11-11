// Copyright (c) 2026 ICECREAM1230
// KS floating-point values are compiled by S3, double buffered in official SDP RAM.
// SPDX-License-Identifier: MIT
`default_nettype none
module midi_ks_performance #(
 parameter bit LOCAL_MIDI_BOOT=0
)(
 input wire clk,rst_n,sclk,enable,clear,
 input midi_pkg::midi_event_t event_i,input wire event_valid,output wire event_ready,
 input wire [3:0] flush,input wire [15:0] voice_idle,
 input wire perf_write_enable,input wire [15:0] perf_write_addr,input wire [31:0] perf_write_data,
 output logic [31:0] template_status,output wire template_available,
 output wire command_valid,input wire command_ready,
 output logic [2:0] command_op,output logic [3:0] command_voice,
 output logic [14:0] command_length,output logic signed [15:0] command_tuning,
 output logic [15:0] command_loss,command_amplitude,command_gain_left,command_gain_right,
 output logic [31:0] command_seed,output wire hard_mute
);
 logic bank;logic commit_req,commit_ack;logic [31:0] generation_mailbox;
 (* async_reg="true" *)logic bank_meta,bank_sync,ack_meta,ack_sync,req_meta,req_sync;
 wire table_write=perf_write_enable&&((perf_write_addr>=16'hb000&&perf_write_addr<=16'hb0ff)||(perf_write_addr>=16'hb200&&perf_write_addr<=16'hb27f))&&commit_req==ack_sync;
 wire [8:0] write_index=perf_write_addr[9]?{2'b10,perf_write_addr[6:0]}:perf_write_addr[8:0];
 wire [9:0] write_address={!bank_sync,write_index};
 logic [9:0] read_address;
 wire [17:0] ram_lo,ram_hi;
 wire [31:0] ram_word={ram_hi[13:0],ram_lo};
 wire [31:0] boot_word;
 generate if(LOCAL_MIDI_BOOT)begin:g_boot
  ks_boot_template boot(.clk(clk),.address_i(read_address[8:0]),.word_o(boot_word));
 end else begin:g_no_boot
  assign boot_word=32'd0;
 end endgenerate
 // Availability is internal; 0708 remains the real host COMMIT generation.
 assign template_available=LOCAL_MIDI_BOOT||template_status!=0;
 sdp_ram_1024x18 lo(.dout(ram_lo),.clka(sclk),.cea(table_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(!rst_n),.ada(write_address),.din(perf_write_data[17:0]),.adb(read_address));
 sdp_ram_1024x18 hi(.dout(ram_hi),.clka(sclk),.cea(table_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(!rst_n),.ada(write_address),.din({4'b0,perf_write_data[31:18]}),.adb(read_address));
 always_ff @(posedge sclk or negedge rst_n)begin
  if(!rst_n)begin bank_meta<=0;bank_sync<=0;ack_meta<=0;ack_sync<=0;commit_req<=0;generation_mailbox<=0;end
  else begin
   bank_meta<=bank;bank_sync<=bank_meta;ack_meta<=commit_ack;ack_sync<=ack_meta;
   if(perf_write_enable&&perf_write_addr==16'hb3ff&&perf_write_data!=0&&commit_req==ack_sync)begin generation_mailbox<=perf_write_data;commit_req<=~commit_req;end
  end
 end
 // Reject out-of-range note traffic before ownership allocation. Controllers
 // remain source/channel scoped and other engines retain the generic owner.
 wire ignore_note=(event_i.status[7:4]==4'h8||event_i.status[7:4]==4'h9)&&event_i.data1<8'd19;
 wire owner_event_ready;
 assign event_ready=ignore_note?1'b1:owner_event_ready;
 wire owner_valid;logic owner_ready;wire [2:0] kind;wire [3:0] voice;wire [6:0] note,velocity,volume,pan;
 wire [31:0] pitch,stamp;wire [15:0] cancel_on;wire [511:0] voice_stamps;
 midi_voice_owner #(.VOICES(16),.HAS_BEND(0),.HAS_LOSS(1)) owner(
  .clk(clk),.rst_n(rst_n),.enable(enable&&template_available),.clear(clear),.release_all(1'b0),.refresh(req_sync!=commit_ack),
  .event_i(event_i),.event_valid(event_valid&&!ignore_note),.event_ready(owner_event_ready),.flush(flush),.voice_idle(voice_idle),
  .command_valid(owner_valid),.command_ready(owner_ready),.command_kind(kind),.command_voice(voice),
  .command_note(note),.command_velocity(velocity),.command_volume(volume),.command_pan(pan),
  .command_pitch(pitch),.command_stamp(stamp),.cancel_on(cancel_on),.voice_stamps(voice_stamps),.hard_mute(hard_mute));
 typedef enum logic[3:0]{IDLE,R0,W0,R1,W1,RG,WG,SCALE_START,SCALE_WAIT,EMIT} fetch_t;
 fetch_t state;
 logic [2:0] captured_kind;logic [3:0] captured_voice;logic [6:0] captured_note,captured_velocity,captured_volume,captured_pan;
 logic [31:0] captured_stamp,word0,word1,gains;logic captured_bank,captured_boot;
 wire [31:0] fetched_word=captured_boot?boot_word:ram_word;
 wire same_identity=kind==captured_kind&&voice==captured_voice&&stamp==captured_stamp&&volume==captured_volume&&pan==captured_pan;
 wire same_request=owner_valid&&same_identity;
 wire legal_note=word0[31]&&word0[14:0]>=2&&word0[14:0]<=4096;
 wire scale_done;
 // Complete scaling before exposing a command to the bridge/core. The owner
 // identity is still checked at EMIT, including after a cancelled RAM fetch.
 midi_ks_gain_scale scaler(
  .clk(clk),.rst_n(rst_n),.cancel(!enable||clear),.start(state==SCALE_START),
  .gain_left(gains[15:0]),.gain_right(gains[31:16]),
  .volume(captured_volume),.velocity(captured_velocity),.done(scale_done),
  .scaled_left(command_gain_left),.scaled_right(command_gain_right),.amplitude(command_amplitude));
 assign command_valid=state==EMIT&&same_request&&(captured_kind!=0||legal_note);
 always_comb begin
  read_address={captured_bank,1'b0,captured_note,1'b0};
  if(state==R1||state==W1)read_address={captured_bank,1'b0,captured_note,1'b1};
  if(state==RG||state==WG)read_address={captured_bank,2'b10,captured_pan};
  // The owner qualifies acceptance with its own VALID. Keep that forward
  // cancellation guard out of READY's return path; identity must still match.
  owner_ready=state==EMIT&&same_identity&&((captured_kind==0&&!legal_note)||command_ready);
  command_op=captured_kind==0?3'd1:captured_kind==1?3'd2:captured_kind==5?3'd6:3'd5;
  command_voice=captured_voice;command_length=word0[14:0];command_tuning=$signed(word1[15:0]);command_loss=word1[31:16];
  command_seed=captured_stamp^32'h9e3779b9^{9'd0,captured_note,16'd0};if(command_seed==0)command_seed=1;
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   bank<=0;req_meta<=0;req_sync<=0;commit_ack<=0;template_status<=0;state<=IDLE;
   captured_kind<=0;captured_voice<=0;captured_note<=0;captured_velocity<=0;captured_volume<=0;captured_pan<=0;captured_stamp<=0;captured_bank<=0;captured_boot<=0;word0<=0;word1<=0;gains<=0;
  end else begin
   req_meta<=commit_req;req_sync<=req_meta;
   if(state==IDLE&&req_sync!=commit_ack)begin bank<=!bank;commit_ack<=req_sync;template_status<=generation_mailbox;end
   if(!enable||clear)state<=IDLE;
   else case(state)
    IDLE:if(owner_valid&&req_sync==commit_ack)begin
     captured_kind<=kind;captured_voice<=voice;captured_note<=note;captured_velocity<=velocity;captured_volume<=volume;captured_pan<=pan;
     captured_stamp<=stamp;captured_bank<=bank;captured_boot<=LOCAL_MIDI_BOOT&&template_status==0;state<=R0;
    end
    R0:state<=W0;
    W0:begin word0<=fetched_word;state<=R1;end
    R1:state<=W1;
    W1:begin word1<=fetched_word;state<=RG;end
    RG:state<=WG;
    WG:begin gains<=fetched_word;state<=SCALE_START;end
    SCALE_START:state<=SCALE_WAIT;
    SCALE_WAIT:if(scale_done)state<=EMIT;
    EMIT:if(!same_request||owner_ready)state<=IDLE;
    default:state<=IDLE;
   endcase
  end
 end
endmodule

// Exact unsigned floor(x/127), with a register between each bounded fold.
// For x=128*h+l, floor(x/127)=h+floor((h+l)/127). Three
// folds reduce every 23-bit product to an 8-bit remainder in [0,131].
// The final comparison supplies the remaining quotient bit. Keeping the
// original floor at each gain stage preserves the uploaded KS sound values.
module midi_ks_gain_scale(
 input wire clk,rst_n,cancel,start,
 input wire [15:0] gain_left,gain_right,input wire [6:0] volume,velocity,
 output wire done,output logic [15:0] scaled_left,scaled_right,amplitude
);
 logic [4:0] valid;
 logic [22:0] product[0:2];
 logic [16:0] quotient1[0:2],quotient2[0:2],quotient3[0:2],fold1[0:2];
 logic [9:0] fold2[0:2];logic [7:0] remainder3[0:2];
 wire [16:0] result[0:2];
 assign done=valid[4];
 for(genvar lane=0;lane<3;lane++)begin:g_result
  assign result[lane]=quotient3[lane]+17'(remainder3[lane]>=127);
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   valid<=0;scaled_left<=0;scaled_right<=0;amplitude<=0;
   for(integer lane=0;lane<3;lane++)begin
    product[lane]<=0;quotient1[lane]<=0;quotient2[lane]<=0;quotient3[lane]<=0;
    fold1[lane]<=0;fold2[lane]<=0;remainder3[lane]<=0;
   end
  end else begin
   if(cancel)valid<=0;
   else valid<={valid[3:0],start};
   if(start&&!cancel)begin
    product[0]<=gain_left*volume;product[1]<=gain_right*volume;
    product[2]<={3'b000,velocity,13'b0};
   end
   for(integer lane=0;lane<3;lane++)begin
    if(valid[0])begin
     quotient1[lane]<={1'b0,product[lane][22:7]};
     fold1[lane]<=17'(product[lane][22:7])+17'(product[lane][6:0]);
    end
    if(valid[1])begin
     quotient2[lane]<=quotient1[lane]+17'(fold1[lane][16:7]);
     fold2[lane]<=10'(fold1[lane][16:7])+10'(fold1[lane][6:0]);
    end
    if(valid[2])begin
     quotient3[lane]<=quotient2[lane]+17'(fold2[lane][9:7]);
     remainder3[lane]<=8'(fold2[lane][9:7])+8'(fold2[lane][6:0]);
    end
   end
   if(valid[3]&&!cancel)begin
    scaled_left<=result[0][15:0];scaled_right<=result[1][15:0];amplitude<=result[2][15:0];
   end
  end
 end
endmodule
`default_nettype wire
