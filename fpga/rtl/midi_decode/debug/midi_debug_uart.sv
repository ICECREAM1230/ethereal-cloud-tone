// SPDX-License-Identifier: MIT
// Passive copies of committed hub outputs. Capacity never drives musical ready.
module midi_debug_uart #(
 parameter integer CLK_HZ=73734177, BAUD=250000, DEPTH=16
)(
 input logic clk,rst,
 input midi_pkg::midi_event_t event_data,input logic event_fire,
 input midi_pkg::midi_sysex_t sysex_data,input logic sysex_fire,
 input logic [3:0] source_reset,input logic [63:0] reset_tick,
 input logic [31:0] fault_count,padding_count,output wire uart_tx
);
 midi_pkg::midi_event_t queued_event;
 midi_pkg::midi_sysex_t queued_sysex;
 wire event_capacity,sysex_capacity,event_valid,sysex_valid,event_ready,sysex_ready;
 wire tx_drive;
 // An asynchronous reset must release the line even if its clock is absent.
 assign uart_tx=rst ? 1'b1 : tx_drive;
 logic [31:0] dropped_event,dropped_sysex;
 logic [3:0] previous_reset;
 // Announce each source's reset assertion once, including held disable.
 // This avoids continuous reset records while performance control is disabled.
 wire [3:0] reset_notice=source_reset & ~previous_reset;
 always_ff @(posedge clk) begin
  if(rst)begin dropped_event<=0;dropped_sysex<=0;previous_reset<=0;end
  else begin
   previous_reset<=source_reset;
   if(event_fire && !event_capacity)dropped_event<=dropped_event+1'b1;
   if(sysex_fire && !sysex_capacity)dropped_sysex<=dropped_sysex+1'b1;
  end
 end
 stream_fifo #(.WIDTH($bits(midi_pkg::midi_event_t)),.DEPTH(DEPTH)) events (
  .clk,.rst,.in_data(event_data),.in_valid(event_fire),.in_ready(event_capacity),
  .out_data(queued_event),.out_valid(event_valid),.out_ready(event_ready)
 );
 stream_fifo #(.WIDTH($bits(midi_pkg::midi_sysex_t)),.DEPTH(DEPTH)) sysex (
  .clk,.rst,.in_data(sysex_data),.in_valid(sysex_fire),.in_ready(sysex_capacity),
  .out_data(queued_sysex),.out_valid(sysex_valid),.out_ready(sysex_ready)
 );
 midi_diag_serializer #(.CLK_HZ(CLK_HZ),.BAUD(BAUD),.CABLES(4)) serializer (
  .clk,.rst,.event_data(queued_event),.event_valid,.event_ready,.event_is_master(1'b0),
  .sysex_data(queued_sysex),.sysex_valid,.sysex_ready,.source_reset(reset_notice),
  .reset_tick,.fault_count,.compat_count(32'd0),.padding_count,
  .dropped_event,.dropped_sysex,.uart_tx(tx_drive)
 );
endmodule
