// SPDX-License-Identifier: MIT
// Buffered observer serializer; never connected to musical ready. Type4 reports loss.
module midi_diag_serializer #(
  parameter integer CLK_HZ=48000000,
  parameter integer BAUD=250000,
  parameter integer CABLES=1
)(
  input logic clk, rst,
  input midi_pkg::midi_event_t event_data,
  input logic event_valid,
  output logic event_ready,
  input logic event_is_master,
  input midi_pkg::midi_sysex_t sysex_data,
  input logic sysex_valid,
  output logic sysex_ready,
  input logic [CABLES-1:0] source_reset,
  input logic [63:0] reset_tick,
  input logic [31:0] fault_count, compat_count, padding_count,
  input logic [31:0] dropped_event, dropped_sysex,
  output wire uart_tx
);
  // A frame is A5 5A 01 type 19 seq[0..3] payload[0..24] CRC8.
  // Multibyte integers and packed payloads are emitted least-significant byte
  // first. CRC8 polynomial 0x07 starts at zero and covers version..payload.
  // Type 1: {5'b0,event_is_master,event_data}; type 2: {3'b0,sysex_data}.
  // Type 3: {padding_count,compat_count,fault_count,reset_cycles,reset_tick,mask}.
  // reset_cycles counts cycles with any reset asserted, not individual bits.
  // While a record is active, resets coalesce by OR mask, latest tick, count.
  // A reset never truncates a frame, and is selected ahead of new data. Stable
  // pre-reset core output slots may be reported later with end_tick<=reset_tick.
  logic active, prefer_sysex, prefer_loss;
  logic [31:0] reported_event, reported_sysex;
  wire loss_waiting = dropped_event != reported_event || dropped_sysex != reported_sysex;
  wire select_loss = loss_waiting && (prefer_loss || (!event_valid && !sysex_valid));
  logic [5:0] byte_index;
  logic [7:0] record_type, crc;
  logic [199:0] payload;
  logic [31:0] sequence_next, sequence_active;
  logic [7:0] pending_mask;
  logic [63:0] pending_tick;
  logic [31:0] pending_cycles;
  wire any_reset = |source_reset;
  wire reset_waiting = pending_cycles != 0 || any_reset;
  wire select_sysex = sysex_valid && (!event_valid || prefer_sysex);
  assign event_ready = !rst && !active && !reset_waiting && !select_loss && !select_sysex;
  assign sysex_ready = !rst && !active && !reset_waiting && !select_loss && select_sysex;

  function automatic [7:0] crc8_byte(input [7:0] previous, input [7:0] data);
    reg [7:0] value;
    begin
      value = previous ^ data;
      for (integer bit_index=0; bit_index<8; bit_index=bit_index+1)
        value = value[7] ? (value << 1) ^ 8'h07 : value << 1;
      crc8_byte = value;
    end
  endfunction

  logic [7:0] uart_data;
  wire uart_ready;
  always_comb begin
    case (byte_index)
      0: uart_data=8'hA5;
      1: uart_data=8'h5A;
      2: uart_data=8'h01;
      3: uart_data=record_type;
      4: uart_data=8'd25;
      5,6,7,8: uart_data=8'(sequence_active >> ((byte_index-5)*8));
      34: uart_data=crc;
      default: uart_data=8'(payload >> ((byte_index-9)*8));
    endcase
  end
  midi_diag_tx #(.CLK_HZ(CLK_HZ),.BAUD(BAUD)) uart_transmitter (
    .clk(clk),.rstn(!rst),.data(uart_data),.valid(active && !rst),
    .ready(uart_ready),.tx(uart_tx)
  );

  always_ff @(posedge clk) begin
    if(rst) begin
      reported_event<=0; reported_sysex<=0; prefer_loss<=0;
      active<=0; prefer_sysex<=0; byte_index<=0; record_type<=0;
      payload<=0; sequence_next<=0; sequence_active<=0; crc<=0;
      pending_mask<=0; pending_tick<=0; pending_cycles<=0;
    end else begin
      if(any_reset) begin
        pending_mask<=pending_mask | 8'(source_reset);
        pending_tick<=reset_tick;
        pending_cycles<=pending_cycles+1'b1;
      end
      if(!active) begin
        if(reset_waiting) begin
          active<=1; record_type<=8'd3;
          payload<={padding_count,compat_count,fault_count,
            (pending_cycles+32'(any_reset)),
            (any_reset ? reset_tick : pending_tick),
            (pending_mask | 8'(source_reset))};
          sequence_active<=sequence_next; sequence_next<=sequence_next+1'b1;
          byte_index<=0; crc<=0;
          pending_mask<=0; pending_cycles<=0;
        end else if(select_loss) begin
          active<=1; record_type<=8'd4;
          payload<={136'b0,dropped_sysex,dropped_event};
          reported_event<=dropped_event; reported_sysex<=dropped_sysex;
          sequence_active<=sequence_next; sequence_next<=sequence_next+1'b1;
          byte_index<=0; crc<=0; prefer_loss<=0;
        end else if(event_valid && event_ready) begin
          active<=1; record_type<=8'd1;
          payload<={5'b0,event_is_master,event_data};
          sequence_active<=sequence_next; sequence_next<=sequence_next+1'b1;
          byte_index<=0; crc<=0; prefer_sysex<=1; prefer_loss<=1;
        end else if(sysex_valid && sysex_ready) begin
          active<=1; record_type<=8'd2;
          payload<={3'b0,sysex_data};
          sequence_active<=sequence_next; sequence_next<=sequence_next+1'b1;
          byte_index<=0; crc<=0; prefer_sysex<=0; prefer_loss<=1;
        end
      end else if(uart_ready) begin
        if(byte_index>=2 && byte_index<34) crc<=crc8_byte(crc,uart_data);
        if(byte_index==34) begin active<=0; byte_index<=0; end
        else byte_index<=byte_index+1'b1;
      end
    end
  end
  initial begin
    if(CABLES<1 || CABLES>8) $error("parsed_diag_uart supports 1..8 reset bits");
    if(CLK_HZ/BAUD<2) $error("parsed_diag_uart requires at least two clocks/bit");
    if($bits(midi_pkg::midi_event_t)!=194 || $bits(midi_pkg::midi_sysex_t)!=197)
      $error("Update diagnostic protocol for changed MIDI metadata widths");
  end
endmodule
