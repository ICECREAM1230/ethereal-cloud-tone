// Copyright (c) 2026 ICECREAM1230
// Thin local MIDI command producer. The FM bridge supplies all identities.
// SPDX-License-Identifier: MIT
`default_nettype none
module midi_fm_performance(
 input wire clk,rst_n,sclk,enable,clear,release_all,
 input midi_pkg::midi_event_t event_i,input wire event_valid,output wire event_ready,
 input wire [3:0] flush,
 input wire perf_write_enable,input wire [15:0] perf_write_addr,input wire [31:0] perf_write_data,
 output wire command_valid,input wire command_ready,
 output fm_contract_pkg::fm_command_t command,
 output wire [7:0] cancel_on,output wire [255:0] voice_stamps,
 output wire [31:0] command_stamp,output wire hard_mute,
 // Observes the actual destination-domain b400 mailbox application. Existing
 // users may leave these ports open; generation zero identifies reset.
 output wire [31:0] expression_applied_data_o,
 output logic [31:0] expression_generation_o
);
 import fm_contract_pkg::*;
 logic [31:0] patch_mailbox;logic patch_req,patch_ack;
 (* async_reg="true" *)logic patch_ack_meta,patch_ack_sync,patch_meta,patch_sync;
 logic [6:0] patch_volume,patch_pan;
 assign expression_applied_data_o={16'd0,1'b0,patch_pan,1'b0,patch_volume};
 always_ff @(posedge sclk or negedge rst_n)begin
  if(!rst_n)begin patch_mailbox<=32'h407f;patch_req<=0;patch_ack_meta<=0;patch_ack_sync<=0;end
  else begin
   patch_ack_meta<=patch_ack;patch_ack_sync<=patch_ack_meta;
   if(perf_write_enable&&perf_write_addr==16'hb400&&patch_req==patch_ack_sync&&perf_write_data[31:16]==0&&perf_write_data[15:8]<=127&&perf_write_data[7:0]<=127)begin
    patch_mailbox<=perf_write_data;patch_req<=~patch_req;
   end
  end
 end
 always_ff @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin patch_meta<=0;patch_sync<=0;patch_ack<=0;patch_volume<=127;patch_pan<=64;expression_generation_o<=0;end
  else begin
   patch_meta<=patch_req;patch_sync<=patch_meta;
   if(patch_sync!=patch_ack)begin
    patch_ack<=patch_sync;patch_volume<=patch_mailbox[6:0];patch_pan<=patch_mailbox[14:8];
    expression_generation_o<=expression_generation_o==32'hffffffff ? 32'd1 : expression_generation_o+32'd1;
   end
  end
 end
 wire [2:0] kind,voice;wire [6:0] note,velocity,volume,pan;wire [31:0] pitch;
 midi_voice_owner #(.VOICES(8),.HAS_BEND(1)) owner(
  .clk(clk),.rst_n(rst_n),.enable(enable),.clear(clear),.release_all(release_all),.refresh(patch_sync!=patch_ack),
  .event_i(event_i),.event_valid(event_valid),.event_ready(event_ready),.flush(flush),.voice_idle(8'b0),
  .command_valid(command_valid),.command_ready(command_ready),.command_kind(kind),.command_voice(voice),
  .command_note(note),.command_velocity(velocity),.command_volume(volume),.command_pan(pan),
  .command_pitch(pitch),.command_stamp(command_stamp),.cancel_on(cancel_on),.voice_stamps(voice_stamps),.hard_mute(hard_mute));
 wire [6:0] effective_volume,effective_pan;
 midi_fm_expression_scale expression_scale(
  .patch_volume(patch_volume),.volume(volume),.patch_pan(patch_pan),.pan(pan),
  .effective_volume(effective_volume),.effective_pan(effective_pan));
 always_comb begin
  command='0;command.scope_id=FM_SCOPE_VOICE;command.target_word={13'd0,voice,16'hffff};
  case(kind)
   0:begin command.opcode_id=FM_SPI_OPCODE_NOTE_ON;command.value_u32=pitch;command.arg0_u32={16'd0,1'b0,effective_volume[6:0],1'b0,velocity};command.arg1_u32=32'(effective_pan);end
   1:command.opcode_id=FM_SPI_OPCODE_NOTE_OFF;
   2:begin command.opcode_id=FM_SPI_OPCODE_SET_PITCH;command.value_u32=pitch;end
   default:begin command.opcode_id=FM_SPI_OPCODE_SET_GAIN_PAN;command.value_u32=32'(effective_volume);command.arg0_u32=32'(effective_pan);end
  endcase
 end
endmodule

// Exact floor(patch_volume*volume/127), with the original combinational
// handshake semantics. For the 7x7 product x<=16129, x=128*h+l and
// floor(x/127)=h+floor((h+l)/127); h+l<254 needs only one comparison.
module midi_fm_expression_scale(
 input wire [6:0] patch_volume,volume,patch_pan,pan,
 output wire [6:0] effective_volume,effective_pan
);
 wire [13:0] product=patch_volume*volume;
 wire [7:0] fold={1'b0,product[13:7]}+{1'b0,product[6:0]};
 wire signed [8:0] pan_sum=$signed({2'b00,patch_pan})+$signed({2'b00,pan})-9'sd64;
 assign effective_volume=product[13:7]+7'(fold>=8'd127);
 assign effective_pan=pan_sum<0?7'd0:pan_sum>127?7'd127:pan_sum[6:0];
endmodule
`default_nettype wire
