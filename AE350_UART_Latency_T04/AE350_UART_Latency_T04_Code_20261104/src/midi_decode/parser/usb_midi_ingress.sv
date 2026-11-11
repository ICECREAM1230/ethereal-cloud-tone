// SPDX-License-Identifier: MIT
module usb_midi_ingress #(
  parameter int CABLES=4,
  parameter int PACKET_DEPTH=4,
  parameter bit COMPAT_CIN5_REALTIME=1
)(
  input logic clk,rst,
  input logic [31:0] packet_data,
  input logic packet_valid,
  output logic packet_ready,
  input logic [CABLES-1:0] flush,
  output logic [CABLES-1:0][7:0] byte_data,
  output logic [CABLES-1:0] byte_valid,
  input logic [CABLES-1:0] byte_ready,
  output logic [CABLES-1:0] source_reset,
  output logic packet_error,
  output logic [31:0] compat_count,
  // Payload/context classification only: no VALID/reset/flush qualification.
  // Callers must qualify this sideband before using it as a cancellation.
  output logic packet_known_invalid
);
  localparam int INDEX_W=CABLES>1 ? $clog2(CABLES) : 1;
  logic [INDEX_W-1:0] cable_index;
  logic [CABLES-1:0] expected_sysex,fifo_ready;
  logic [3:0] cable,cin;
  logic [7:0] first,second,third;
  logic known,packet_ok,compat_packet,next_sysex,accepted,error_eligible;
  logic [1:0] length;
  assign cable=packet_data[7:4];
  assign cin=packet_data[3:0];
  assign cable_index=INDEX_W'(cable);
  assign first=packet_data[15:8];
  assign second=packet_data[23:16];
  assign third=packet_data[31:24];
  assign known=int'(cable)<CABLES;

  // Validate the entire packet before placing any payload in a source FIFO.
  // This state describes accepted input order, including bytes still queued
  // behind a stalled consumer; it is deliberately ahead of the serializer.
  always_comb begin
    logic in_sysex;
    in_sysex=0;
    if(known) in_sysex=expected_sysex[cable_index];
    packet_ok=0; length=0; compat_packet=0;
    case(cin)
      4'h2: begin
        length=2;
        packet_ok=(first==8'hF1 || first==8'hF3) && !second[7] && third==0;
      end
      4'h3: begin length=3; packet_ok=first==8'hF2 && !second[7] && !third[7]; end
      4'h4: begin
        length=3;
        packet_ok=(first==8'hF0 || (in_sysex && !first[7])) && !second[7] && !third[7];
      end
      4'h5: begin
        length=1;
        compat_packet=COMPAT_CIN5_REALTIME && first>=8'hF8;
        packet_ok=(first==8'hF6 || (in_sysex && first==8'hF7) || compat_packet) && second==0 && third==0;
      end
      4'h6: begin
        length=2;
        packet_ok=(first==8'hF0 || (in_sysex && !first[7])) && second==8'hF7 && third==0;
      end
      4'h7: begin
        length=3;
        packet_ok=(first==8'hF0 || (in_sysex && !first[7])) && !second[7] && third==8'hF7;
      end
      4'h8,4'h9,4'hA,4'hB,4'hE: begin
        length=3; packet_ok=first[7:4]==cin && !second[7] && !third[7];
      end
      4'hC,4'hD: begin
        length=2; packet_ok=first[7:4]==cin && !second[7] && third==0;
      end
      4'hF: begin length=1; packet_ok=second==0 && third==0; end
      default: begin end
    endcase
    packet_ok=packet_ok && known;
    next_sysex=in_sysex;
    for(int b=0;b<3;b++) begin
      logic [7:0] value;
      value=8'(packet_data>>(8*(b+1)));
      // All F8..FF, including reserved F9/FD, preserve stream context.
      if(b<int'(length) && value[7] && value<8'hF8) next_sysex=value==8'hF0;
    end
  end

  always_comb begin
    packet_ready=0;
    if(!rst) begin
      if(!known) packet_ready=1;
      else if(!flush[cable_index]) packet_ready=!packet_ok || fifo_ready[cable_index];
    end
  end
  assign accepted=packet_valid && packet_ready;
  // Invalid packets are always ready in the same eligible branch, regardless
  // of FIFO backpressure. Absorb (!packet_ok || fifo_ready) && !packet_ok
  // before crossing hierarchy, so downstream error clears never depend on
  // a musical consumer's ready cone. Preserve the procedural four-state
  // reset/cable/flush guards and the original same-edge error semantics.
  always_comb begin
    error_eligible=0;
    if(!rst) begin
      if(!known) error_eligible=1;
      else if(!flush[cable_index]) error_eligible=1;
    end
  end
  assign packet_known_invalid=known && !packet_ok;
  assign packet_error=packet_valid && error_eligible && !packet_ok;
  always_comb begin
    source_reset='0;
    if(!rst) begin
      source_reset=flush;
      if(packet_error && known) source_reset[cable_index]=1;
    end
  end
  always_ff @(posedge clk) begin
    if(rst) compat_count<=0;
    else if(accepted && packet_ok && compat_packet) compat_count<=compat_count+1'b1;
  end

  for(genvar p=0;p<CABLES;p++) begin : sources
    logic [25:0] queued_packet;
    logic queued_valid,queued_ready,serial_valid;
    logic [23:0] serial_data;
    logic [1:0] remaining;
    stream_fifo #(.WIDTH(26),.DEPTH(PACKET_DEPTH)) packets(
      .clk,.rst(rst||source_reset[p]),
      .in_data({length,packet_data[31:8]}),
      .in_valid(accepted && packet_ok && cable==4'(p)),.in_ready(fifo_ready[p]),
      .out_data(queued_packet),.out_valid(queued_valid),.out_ready(queued_ready));
    assign queued_ready=!serial_valid;
    assign byte_data[p]=serial_data[7:0];
    assign byte_valid[p]=serial_valid && !rst && !source_reset[p];
    always_ff @(posedge clk) begin
      if(rst || source_reset[p]) begin
        expected_sysex[p]<=0;
        serial_valid<=0; serial_data<=0; remaining<=0;
      end else begin
        if(accepted && packet_ok && cable==4'(p)) expected_sysex[p]<=next_sysex;
        if(queued_valid && queued_ready) begin
          serial_data<=queued_packet[23:0]; remaining<=queued_packet[25:24]; serial_valid<=1;
        end
        if(byte_valid[p] && byte_ready[p]) begin
          if(remaining==1) serial_valid<=0;
          else begin serial_data<=serial_data>>8; remaining<=remaining-1'b1; end
        end
      end
    end
  end
  initial begin
    if(CABLES<1 || CABLES>16) $fatal(1,"CABLES must be 1..16");
    if(PACKET_DEPTH<1) $fatal(1,"PACKET_DEPTH must be positive");
  end
endmodule
