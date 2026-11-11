// SPDX-License-Identifier: MIT
// One complete USB transfer bank. Reserve this bank before issuing Bulk IN.
module midi_packet_queue (
  input wire clk, rst, flush,
  input wire in_valid,
  output wire in_ready,
  input wire [6:0] in_len,
  input wire [511:0] in_payload,
  output wire [31:0] packet_data,
  output wire packet_valid,
  input wire packet_ready,
  output reg format_error
);
  reg [511:0] bank;
  reg [4:0] words;
  reg unsupported_cable;
  always @* begin
    unsupported_cable=0;
    for(integer i=0;i<16;i=i+1)
      if(i < int'(in_len[6:2]) && in_payload[i*32+4+:4]!=0) unsupported_cable=1;
  end
  assign in_ready = words == 0 && !rst && !flush;
  assign packet_valid = words != 0 && !rst && !flush;
  assign packet_data = bank[31:0];
  always @(posedge clk) begin
    if (rst || flush) begin
      bank <= 0;
      words <= 0;
      format_error <= 0;
    end else begin
      format_error <= 0;
      if (in_valid && in_ready) begin
        if (in_len > 64 || in_len[1:0] != 0 || unsupported_cable) format_error <= 1;
        else begin
          bank <= in_payload;
          words <= in_len[6:2];
        end
      end else if (packet_valid && packet_ready) begin
        bank <= bank >> 32;
        words <= words - 1'b1;
      end
    end
  end
endmodule
