// SPDX-License-Identifier: MIT
module stream_fifo #(
  parameter int unsigned WIDTH=8,
  parameter int unsigned DEPTH=4
) (
  input logic clk, rst,
  input logic [WIDTH-1:0] in_data,
  input logic in_valid,
  output logic in_ready,
  output logic [WIDTH-1:0] out_data,
  output logic out_valid,
  input logic out_ready
);
  localparam int PTR_W=(DEPTH>1) ? $clog2(DEPTH) : 1;
  localparam int COUNT_W=$clog2(DEPTH+1);
  logic [WIDTH-1:0] memory [0:DEPTH-1];
  logic [PTR_W-1:0] read_ptr, write_ptr;
  logic [COUNT_W-1:0] count;
  logic push, pop;

  // Deliberately conservative at full capacity, even when a pop is ready.
  // Neither reset nor downstream ready feeds back into upstream capacity.
  assign in_ready=count<COUNT_W'(DEPTH);
  assign out_valid=(count!=0) && !rst;
  assign out_data=memory[read_ptr];
  assign push=in_valid && in_ready;
  assign pop=out_valid && out_ready;

  always_ff @(posedge clk) begin
    if (rst) begin
      read_ptr<='0; write_ptr<='0; count<='0;
    end else begin
      if (push) begin
        memory[write_ptr]<=in_data;
        if (write_ptr==PTR_W'(DEPTH-1)) write_ptr<='0;
        else write_ptr<=write_ptr+PTR_W'(1);
      end
      if (pop) begin
        if (read_ptr==PTR_W'(DEPTH-1)) read_ptr<='0;
        else read_ptr<=read_ptr+PTR_W'(1);
      end
      case ({push,pop})
        2'b10: count<=count+COUNT_W'(1);
        2'b01: count<=count-COUNT_W'(1);
        default: begin end
      endcase
    end
  end
endmodule
